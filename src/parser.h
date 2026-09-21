#pragma once

#include "ast.h"
#include "lexer.h"

#include <memory>
#include <string>
#include <vector>

class Parser {
  std::vector<std::vector<token>> lines;
  std::vector<token> toks;
  size_t pos = 0;

  const token *peek() const;
  const token &eat();
  [[noreturn]] void fail(const std::string &msg) const;
  const token &expect(tok_kind kind, const std::string &text,
                      const std::string &what);
  void expect_end() const;

  std::unique_ptr<ExprNode> parse_factor();
  std::unique_ptr<ExprNode> parse_term();
  std::unique_ptr<ExprNode> parse_expr();
  std::unique_ptr<StmtNode> parse_decl();
  std::unique_ptr<StmtNode> parse_assign();
  std::unique_ptr<StmtNode> parse_statement();
  std::unique_ptr<ExitNode> parse_exit();

public:
  explicit Parser(std::vector<std::vector<token>> lines);
  std::unique_ptr<ProgramNode> parse_program();
};
