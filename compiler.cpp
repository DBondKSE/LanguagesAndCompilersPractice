#include "llvm/Config/llvm-config.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace llvm;

static LLVMContext ctx;
static IRBuilder<> builder(ctx);
static Module *theModule;
static Type *i32;
typedef struct {
  Value *slot;
  bool mut;
} variable;

static std::map<std::string, variable> vars;

static void error(uint64_t line, uint64_t col, const std::string &msg) {
  std::cerr << "compilation error: line " << line << ":" << col << ": " << msg
            << "\n";
  std::exit(1);
}

enum tok_state {
  START,
  IDENT,
  NUMBER,
  COLON
};

enum tok_kind {
  TK_KEYWORD,
  TK_IDENT,
  TK_NUMBER,
  TK_BLOCK,
  TK_OPERATOR,
  TK_ENDLINE
};

typedef struct {
  tok_kind kind;
  std::string text;
  uint64_t line;
  uint64_t col;
} token;

static const std::set<std::string> keywords = {"i32", "mut", "exit"};

static const char *kind_name(tok_kind k) {
  switch (k) {
  case TK_KEYWORD:
    return "keyword";
  case TK_IDENT:
    return "identifier";
  case TK_NUMBER:
    return "number";
  case TK_BLOCK:
    return "block";
  case TK_OPERATOR:
    return "operator";
  case TK_ENDLINE:
    return "endline";
  }
  return "?";
}

static bool is_alpha(unsigned char b) {
  return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || b == '_';
}

static bool is_digit(unsigned char b) { return b >= '0' && b <= '9'; }

static std::string show_byte(unsigned char b) {
  if (b >= 32 && b < 127)
    return std::string(1, (char)b);
  char buf[8];
  snprintf(buf, sizeof buf, "\\x%02X", b);
  return buf;
}

static std::vector<std::vector<token>> tokenize(const std::string &s) {
  std::vector<std::vector<token>> lines;
  std::vector<token> t;
  tok_state state = START;
  size_t i = 0, start = 0;
  uint64_t line = 1, col = 1, start_col = 0;
  uint64_t brace_col = 0;

  while (i <= s.size()) {
    bool eof = i == s.size();
    unsigned char b = eof ? 0 : (unsigned char)s[i];

    if (state == START) {
      if (eof || b == '\n') {
        if (brace_col)
          error(line, brace_col, "'{' is not closed before the end of the line");
        if (eof)
          break;
        t.push_back({TK_ENDLINE, "\n", line, col});
        lines.push_back(t);
        t.clear();
        line++;
        col = 0;
      } else if (b == ' ' || b == '\t') {
      } else if (is_alpha(b)) {
        state = IDENT, start = i, start_col = col;
      } else if (is_digit(b)) {
        state = NUMBER, start = i, start_col = col;
      } else if (b == ':') {
        state = COLON, start_col = col;
      } else if (b == '{') {
        if (!brace_col)
          brace_col = col;
        t.push_back({TK_BLOCK, "{", line, col});
      } else if (b == '}') {
        brace_col = 0;
        t.push_back({TK_BLOCK, "}", line, col});
      } else if (b == '+' || b == '-' || b == '*') {
        t.push_back({TK_OPERATOR, std::string(1, (char)b), line, col});
      } else {
        error(line, col, "unexpected byte '" + show_byte(b) + "'");
      }
    } else if (state == IDENT) {
      if (!eof && (is_alpha(b) || is_digit(b))) {
      } else {
        std::string word = s.substr(start, i - start);
        t.push_back({keywords.count(word) ? TK_KEYWORD : TK_IDENT, word, line,
                     start_col});
        state = START;
        continue;
      }
    } else if (state == NUMBER) {
      if (!eof && is_digit(b)) {
      } else if (!eof && is_alpha(b)) {
        error(line, start_col,
              "letter '" + show_byte(b) + "' inside number '" +
                  s.substr(start, i - start) + "'");
      } else {
        t.push_back({TK_NUMBER, s.substr(start, i - start), line, start_col});
        state = START;
        continue;
      }
    } else if (state == COLON) {
      if (!eof && b == '=') {
        t.push_back({TK_OPERATOR, ":=", line, start_col});
        state = START;
      } else {
        error(line, start_col, "':' is not followed by '='");
      }
    }
    i++;
    col++;
  }
  if (!t.empty())
    lines.push_back(t);
  return lines;
}

static void print_tokens(const std::vector<std::vector<token>> &lines) {
  for (const std::vector<token> &t : lines) {
    for (const token &k : t)
      std::cout << "(" << (k.kind == TK_ENDLINE ? "\\n" : k.text) << ", "
                << kind_name(k.kind) << ", " << k.line << ":" << k.col << ") ";
    std::cout << "\n";
  }
}

static std::string describe(const token &tok) {
  return tok.kind == TK_ENDLINE ? "the end of the line" : "'" + tok.text + "'";
}

static bool is(const token &tok, tok_kind kind, const std::string &text) {
  return tok.kind == kind && tok.text == text;
}

static void expect_end(const token &tok) {
  if (tok.kind != TK_ENDLINE)
    error(tok.line, tok.col,
          "unexpected " + describe(tok) + " after the end of the statement");
}

static const variable &lookup(const token &tok) {
  auto it = vars.find(tok.text);
  if (it == vars.end())
    error(tok.line, tok.col,
          "variable '" + tok.text + "' is used before its declaration");
  return it->second;
}

static Value *operand(const token &tok) {
  if (tok.kind == TK_NUMBER) {
    uint64_t v = 0;
    for (char c : tok.text) {
      v = v * 10 + (c - '0');
      if (v > INT32_MAX)
        error(tok.line, tok.col,
              "number '" + tok.text + "' does not fit in i32");
    }
    return ConstantInt::get(i32, v);
  }
  if (tok.kind != TK_IDENT)
    error(tok.line, tok.col,
          "expected a number or a variable, got " + describe(tok));
  return builder.CreateLoad(i32, lookup(tok).slot, tok.text);
}

static Value *expression(const std::vector<token> &t, size_t &i) {
  Value *l = operand(t[i++]);
  const token &op = t[i];
  if (op.kind != TK_OPERATOR || op.text == ":=")
    return l;
  i++;
  Value *r = operand(t[i++]);
  if (op.text == "+")
    return builder.CreateAdd(l, r, "add");
  if (op.text == "-")
    return builder.CreateSub(l, r, "sub");
  return builder.CreateMul(l, r, "mul");
}

static void declaration(const std::vector<token> &t) {
  size_t i = 1;
  bool mut = is(t[i], TK_KEYWORD, "mut");
  if (mut)
    i++;

  const token &name = t[i++];
  if (name.kind != TK_IDENT)
    error(name.line, name.col,
          "expected a variable name, got " + describe(name));
  if (vars.count(name.text))
    error(name.line, name.col,
          "variable '" + name.text + "' is already declared");

  if (t[i].kind == TK_ENDLINE || (is(t[i], TK_BLOCK, "{") &&
                                  is(t[i + 1], TK_BLOCK, "}")))
    error(name.line, name.col,
          "variable '" + name.text + "' needs an initialiser in {}");
  if (!is(t[i], TK_BLOCK, "{"))
    error(t[i].line, t[i].col, "expected '{' after '" + name.text +
                                   "', got " + describe(t[i]));
  i++;

  Value *init = expression(t, i);
  if (!is(t[i], TK_BLOCK, "}"))
    error(t[i].line, t[i].col, "expected '}', got " + describe(t[i]));
  expect_end(t[i + 1]);

  Value *slot = builder.CreateAlloca(i32, nullptr, name.text);
  builder.CreateStore(init, slot);
  vars[name.text] = {slot, mut};
}

static void assignment(const std::vector<token> &t) {
  const token &name = t[0];
  if (!is(t[1], TK_OPERATOR, ":="))
    error(t[1].line, t[1].col,
          "expected ':=' after '" + name.text + "', got " + describe(t[1]));
  const variable &var = lookup(name);
  if (!var.mut)
    error(name.line, name.col,
          "cannot assign to '" + name.text + "': it is not mut");

  size_t i = 2;
  Value *v = expression(t, i);
  expect_end(t[i]);
  builder.CreateStore(v, var.slot);
}

int main(int argc, char **argv) {
  bool dumpTokens = argc == 3 && std::string(argv[1]) == "--tokens";
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <source> <output.ll>\n"
              << "       " << argv[0] << " --tokens <source>\n";
    return 1;
  }

  const char *srcPath = dumpTokens ? argv[2] : argv[1];
  std::ifstream in(srcPath, std::ios::binary);
  if (!in) {
    std::cerr << "error: cannot open " << srcPath << "\n";
    return 1;
  }
  std::ostringstream src;
  src << in.rdbuf();

  std::vector<std::vector<token>> lines = tokenize(src.str());
  if (dumpTokens) {
    print_tokens(lines);
    return 0;
  }

  i32 = Type::getInt32Ty(ctx);
  theModule = new Module("practice1", ctx);
#if LLVM_VERSION_MAJOR >= 21
  theModule->setTargetTriple(Triple(sys::getDefaultTargetTriple()));
#else
  theModule->setTargetTriple(sys::getDefaultTargetTriple());
#endif

  Function *mainFn =
      Function::Create(FunctionType::get(i32, false), Function::ExternalLinkage,
                       "main", theModule);
  builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", mainFn));

  Type *i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
  Function *printfFn =
      Function::Create(FunctionType::get(i32, {i8ptr}, true),
                       Function::ExternalLinkage, "printf", theModule);
  Value *fmt = builder.CreateGlobalStringPtr("Program exit with result %d\n");

  bool hasExit = false;

  for (std::vector<token> t : lines) {
    const token &last = t.back();
    if (last.kind != TK_ENDLINE)
      t.push_back({TK_ENDLINE, "", last.line, last.col + last.text.size()});
    if (t.size() == 1)
      continue;

    const token &first = t[0];
    if (hasExit)
      error(first.line, first.col, "exit must be the last statement");

    if (is(first, TK_KEYWORD, "i32")) {
      declaration(t);
    } else if (is(first, TK_KEYWORD, "exit")) {
      Value *v = operand(t[1]);
      expect_end(t[2]);
      builder.CreateCall(printfFn, {fmt, v});
      builder.CreateRet(ConstantInt::get(i32, 0));
      hasExit = true;
    } else if (first.kind == TK_IDENT) {
      assignment(t);
    } else {
      error(first.line, first.col,
            "expected a declaration, an assignment or exit, got " +
                describe(first));
    }
  }

  if (!hasExit)
    error(lines.empty() ? 1 : lines.back().front().line, 1,
          "the program has no exit");

  std::error_code ec;
  raw_fd_ostream out(argv[2], ec);
  if (ec) {
    std::cerr << "error: cannot write " << argv[2] << "\n";
    return 1;
  }
  theModule->print(out, nullptr);
  return 0;
}
