#pragma once

#include "ast.h"
#include "lexer.h"

#include <memory>
#include <string>
#include <vector>

class Parser {
  std::vector<std::vector<token>> lines;
  size_t next = 0;
  std::vector<token> toks;
  size_t pos = 0;

  const std::vector<token> *peek_line() const;
  void next_line();
  const token *peek(size_t ahead = 0) const;
  const token &eat();
  [[noreturn]] void fail(const std::string &msg) const;
  const token &expect(tok_kind kind, const std::string &text,
                      const std::string &what);
  bool match(tok_kind kind, const std::string &text);
  const token &expect_type(const std::string &what);
  void expect_end(const std::string &after = "the statement") const;

  std::unique_ptr<ExprNode> parse_chain(const token &name);
  std::vector<std::unique_ptr<ExprNode>> parse_args();
  std::unique_ptr<ExprNode> parse_factor();
  std::unique_ptr<ExprNode> parse_term();
  std::unique_ptr<ExprNode> parse_arith();
  std::unique_ptr<ExprNode> parse_expr();
  token open_brace(const std::string &after);
  std::unique_ptr<FieldNode> parse_field();
  std::unique_ptr<StructNode> parse_struct();
  template <class Fn> std::unique_ptr<Fn> parse_fn();
  template <class Fn>
  std::unique_ptr<Fn> parse_signature(const token &name,
                                      const std::string &after);
  bool at_lambda() const;
  std::unique_ptr<StmtNode> parse_lambda();
  std::unique_ptr<InitNode> parse_init(const token &brace);
  std::unique_ptr<StmtNode> parse_decl();
  std::unique_ptr<StmtNode> parse_assign();
  std::unique_ptr<StmtNode> parse_if();
  std::unique_ptr<StmtNode> parse_while();
  std::unique_ptr<StmtNode> parse_print();
  std::unique_ptr<BlockNode> parse_block(const std::string &after);
  std::unique_ptr<StmtNode> parse_statement();
  std::unique_ptr<ExitNode> parse_exit();

public:
  explicit Parser(const std::vector<std::vector<token>> &lines);
  std::unique_ptr<ProgramNode> parse_program();
};
