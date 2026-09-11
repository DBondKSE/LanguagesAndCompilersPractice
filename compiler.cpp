#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace llvm;

static LLVMContext ctx;
static IRBuilder<> builder(ctx);
static Module *mod;
static Type *i32;
static std::map<std::string, Value *> vars;

static void error(int line, const std::string &msg) {
  std::cerr << "compilation error: line " << line << ": " << msg << "\n";
  std::exit(1);
}

static std::vector<std::string> tokenize(const std::string &s) {
  std::vector<std::string> t;
  size_t i = 0;
  while (i < s.size()) {
    char c = s[i];
    if (isspace((unsigned char)c)) {
      i++;
    } else if (isalnum((unsigned char)c) || c == '_') {
      size_t j = i;
      while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_'))
        j++;
      t.push_back(s.substr(i, j - i));
      i = j;
    } else if (c == ':' && i + 1 < s.size() && s[i + 1] == '=') {
      t.push_back(":=");
      i += 2;
    } else {
      t.push_back(std::string(1, c));
      i++;
    }
  }
  return t;
}

static bool isNumber(const std::string &s) {
  for (size_t i = 0; i < s.size(); i++)
    if (!isdigit((unsigned char)s[i]))
      return false;
  return !s.empty();
}

static Value *slot(const std::string &name, int line) {
  if (!vars.count(name))
    error(line, "undeclared variable '" + name + "'");
  return vars[name];
}

static Value *value(const std::string &tok, int line) {
  if (isNumber(tok))
    return ConstantInt::get(i32, atoi(tok.c_str()));
  return builder.CreateLoad(i32, slot(tok, line), tok + ".val");
}

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <source> <output.ll>\n";
    return 1;
  }

  std::ifstream in(argv[1]);
  if (!in) {
    std::cerr << "error: cannot open " << argv[1] << "\n";
    return 1;
  }

  i32 = Type::getInt32Ty(ctx);
  mod = new Module("practice1", ctx);
  mod->setTargetTriple(sys::getDefaultTargetTriple());

  Function *mainFn = Function::Create(FunctionType::get(i32, false),
                                      Function::ExternalLinkage, "main", mod);
  builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", mainFn));

  Type *i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
  Function *printfFn =
      Function::Create(FunctionType::get(i32, {i8ptr}, true),
                       Function::ExternalLinkage, "printf", mod);
  Value *fmt = builder.CreateGlobalStringPtr("Program exit with result %d\n");

  std::string line;
  int n = 0;
  bool hasExit = false;

  while (std::getline(in, line)) {
    n++;
    std::vector<std::string> t = tokenize(line);

    if (t.empty())
      continue;

    if (t[0] == "int" && t.size() == 2) {
      if (vars.count(t[1]))
        error(n, "variable '" + t[1] + "' is already declared");
      vars[t[1]] = builder.CreateAlloca(i32, nullptr, t[1]);
    } else if (t[0] == "exit" && t.size() == 2) {
      builder.CreateCall(printfFn, {fmt, value(t[1], n)});
      builder.CreateRet(ConstantInt::get(i32, 0));
      hasExit = true;
      break;
    } else if (t.size() == 3 && t[1] == ":=") {
      builder.CreateStore(value(t[2], n), slot(t[0], n));
    } else if (t.size() == 5 && t[1] == ":=") {
      Value *l = value(t[2], n);
      Value *r = value(t[4], n);
      Value *res = nullptr;
      if (t[3] == "+")
        res = builder.CreateAdd(l, r, "add");
      else if (t[3] == "-")
        res = builder.CreateSub(l, r, "sub");
      else if (t[3] == "*")
        res = builder.CreateMul(l, r, "mul");
      else
        error(n, "unknown operator '" + t[3] + "'");
      builder.CreateStore(res, slot(t[0], n));
    } else {
      error(n, "cannot parse line");
    }
  }

  if (!hasExit)
    error(n, "missing exit statement");

  std::error_code ec;
  raw_fd_ostream out(argv[2], ec);
  if (ec) {
    std::cerr << "error: cannot write " << argv[2] << "\n";
    return 1;
  }
  mod->print(out, nullptr);
  return 0;
}
