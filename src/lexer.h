#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

std::vector<std::vector<token>> tokenize(const std::string &s);
void print_tokens(const std::vector<std::vector<token>> &lines);
std::string describe(const token &tok);
bool is(const token &tok, tok_kind kind, const std::string &text);
