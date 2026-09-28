#include "parser.h"
#include "error.h"

#include <cstdint>

Parser::Parser(std::vector<std::vector<token>> lines)
    : lines(std::move(lines)) {}

const token *Parser::peek() const {
  return pos < toks.size() ? &toks[pos] : nullptr;
}

const token &Parser::eat() { return toks[pos++]; }

void Parser::fail(const std::string &msg) const {
  const token *tok = peek();
  if (tok)
    error(tok->line, tok->col, msg + ", got " + describe(*tok));
  const token &last = toks.back();
  error(last.line, last.col + last.text.size(), msg + ", found end of line");
}

const token &Parser::expect(tok_kind kind, const std::string &text,
                            const std::string &what) {
  const token *tok = peek();
  if (!tok || tok->kind != kind || (!text.empty() && tok->text != text))
    fail("expected " + what);
  return eat();
}

void Parser::expect_end() const {
  const token *tok = peek();
  if (tok)
    error(tok->line, tok->col,
          "unexpected " + describe(*tok) + " after the statement");
}

std::unique_ptr<ExprNode> Parser::parse_factor() {
  const token *tok = peek();
  if (tok && tok->kind == TK_NUMBER) {
    eat();
    uint64_t v = 0;
    for (char c : tok->text) {
      v = v * 10 + (c - '0');
      if (v > INT32_MAX)
        error(tok->line, tok->col,
              "number '" + tok->text + "' does not fit in i32");
    }
    return std::make_unique<ConstNode>(tok->line, tok->col, (int32_t)v);
  }
  if (tok && tok->kind == TK_IDENT) {
    eat();
    return std::make_unique<VarNode>(tok->line, tok->col, tok->text);
  }
  fail("expected a constant or a variable");
}

std::unique_ptr<ExprNode> Parser::parse_term() {
  std::unique_ptr<ExprNode> node = parse_factor();
  const token *tok;
  while ((tok = peek()) && is(*tok, TK_OPERATOR, "*")) {
    eat();
    std::unique_ptr<ExprNode> right = parse_factor();
    node = std::make_unique<BinOpNode>(tok->line, tok->col, '*',
                                       std::move(node), std::move(right));
  }
  return node;
}

std::unique_ptr<ExprNode> Parser::parse_expr() {
  std::unique_ptr<ExprNode> node = parse_term();
  const token *tok;
  while ((tok = peek()) &&
         (is(*tok, TK_OPERATOR, "+") || is(*tok, TK_OPERATOR, "-"))) {
    eat();
    std::unique_ptr<ExprNode> right = parse_term();
    node = std::make_unique<BinOpNode>(tok->line, tok->col, tok->text[0],
                                       std::move(node), std::move(right));
  }
  return node;
}

std::unique_ptr<StmtNode> Parser::parse_decl() {
  eat();
  const token *tok = peek();
  bool mut = tok && is(*tok, TK_KEYWORD, "mut");
  if (mut)
    eat();
  const token &name = expect(TK_IDENT, "", "a variable name");
  std::string needs_init =
      "variable '" + name.text + "' needs an initialiser in {}";
  if (!peek())
    error(name.line, name.col, needs_init);
  expect(TK_BLOCK, "{", "'{' after '" + name.text + "'");
  tok = peek();
  if (tok && is(*tok, TK_BLOCK, "}"))
    error(name.line, name.col, needs_init);
  std::unique_ptr<ExprNode> init = parse_expr();
  expect(TK_BLOCK, "}", "'}'");
  return std::make_unique<DeclNode>(name.line, name.col, name.text, mut,
                                    std::move(init));
}

std::unique_ptr<StmtNode> Parser::parse_assign() {
  const token &name = eat();
  expect(TK_OPERATOR, ":=", "':=' after '" + name.text + "'");
  return std::make_unique<AssignNode>(name.line, name.col, name.text,
                                      parse_expr());
}

std::unique_ptr<StmtNode> Parser::parse_statement() {
  const token &tok = *peek();
  if (is(tok, TK_KEYWORD, "i32"))
    return parse_decl();
  if (tok.kind == TK_IDENT)
    return parse_assign();
  error(tok.line, tok.col, "cannot start a statement with " + describe(tok));
}

std::unique_ptr<ExitNode> Parser::parse_exit() {
  const token &kw = eat();
  return std::make_unique<ExitNode>(kw.line, kw.col, parse_factor());
}

std::unique_ptr<ProgramNode> Parser::parse_program() {
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit_node;
  uint64_t last_line = 1;

  for (const std::vector<token> &line : lines) {
    toks.clear();
    for (const token &tok : line)
      if (tok.kind != TK_ENDLINE)
        toks.push_back(tok);
    pos = 0;
    if (toks.empty())
      continue;

    const token &first = toks.front();
    last_line = first.line;
    if (exit_node)
      error(first.line, first.col, "exit must be the last statement");
    if (is(first, TK_KEYWORD, "exit"))
      exit_node = parse_exit();
    else
      statements.push_back(parse_statement());
    expect_end();
  }

  if (!exit_node)
    error(last_line, 1, "the program has no exit");
  return std::make_unique<ProgramNode>(1, 1, std::move(statements),
                                       std::move(exit_node));
}
