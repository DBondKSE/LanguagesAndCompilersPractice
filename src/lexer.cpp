#include "lexer.h"
#include "error.h"

#include <cstdio>
#include <iostream>
#include <set>

enum tok_state {
  START,
  IDENT,
  NUMBER,
  COLON
};

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

std::vector<std::vector<token>> tokenize(const std::string &s) {
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

void print_tokens(const std::vector<std::vector<token>> &lines) {
  for (const std::vector<token> &t : lines) {
    for (const token &k : t)
      std::cout << "(" << (k.kind == TK_ENDLINE ? "\\n" : k.text) << ", "
                << kind_name(k.kind) << ", " << k.line << ":" << k.col << ") ";
    std::cout << "\n";
  }
}

std::string describe(const token &tok) {
  return tok.kind == TK_ENDLINE ? "the end of the line" : "'" + tok.text + "'";
}

bool is(const token &tok, tok_kind kind, const std::string &text) {
  return tok.kind == kind && tok.text == text;
}
