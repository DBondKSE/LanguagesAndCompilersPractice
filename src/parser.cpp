#include "parser.h"
#include "error.h"

#include <cstdint>

Parser::Parser(const std::vector<std::vector<token>> &source) {
  for (const std::vector<token> &line : source) {
    std::vector<token> kept;
    for (const token &tok : line)
      if (tok.kind != TK_ENDLINE)
        kept.push_back(tok);
    if (!kept.empty())
      lines.push_back(kept);
  }
}

const std::vector<token> *Parser::peek_line() const {
  return next < lines.size() ? &lines[next] : nullptr;
}

void Parser::next_line() {
  toks = lines[next++];
  pos = 0;
}

const token *Parser::peek(size_t ahead) const {
  return pos + ahead < toks.size() ? &toks[pos + ahead] : nullptr;
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

bool Parser::match(tok_kind kind, const std::string &text) {
  const token *tok = peek();
  if (!tok || !is(*tok, kind, text))
    return false;
  eat();
  return true;
}

void Parser::expect_end(const std::string &after) const {
  const token *tok = peek();
  if (tok)
    error(tok->line, tok->col,
          "unexpected " + describe(*tok) + " after " + after);
}

std::vector<Link> Parser::parse_links() {
  std::vector<Link> links;
  while (match(TK_OPERATOR, ".")) {
    const token &field = expect(TK_IDENT, "", "a field name");
    links.push_back({field.text, field.col});
  }
  return links;
}

std::unique_ptr<ExprNode> Parser::parse_factor() {
  const token *tok = peek();
  if (tok && is(*tok, TK_OPERATOR, "!")) {
    eat();
    return std::make_unique<NotNode>(tok->line, tok->col, parse_factor());
  }
  if (tok && tok->kind == TK_NUMBER) {
    eat();
    return std::make_unique<ConstNode>(tok->line, tok->col, tok->text);
  }
  if (tok && (is(*tok, TK_KEYWORD, "true") || is(*tok, TK_KEYWORD, "false"))) {
    eat();
    return std::make_unique<BoolNode>(tok->line, tok->col, tok->text == "true");
  }
  if (tok && tok->kind == TK_IDENT) {
    eat();
    if (!match(TK_SEPARATOR, "("))
      return std::make_unique<VarNode>(tok->line, tok->col, tok->text,
                                       parse_links());
    std::vector<std::unique_ptr<ExprNode>> args;
    if (!match(TK_SEPARATOR, ")")) {
      do
        args.push_back(parse_expr());
      while (match(TK_SEPARATOR, ","));
      expect(TK_SEPARATOR, ")", "')'");
    }
    return std::make_unique<CallNode>(tok->line, tok->col, tok->text,
                                      std::move(args));
  }
  fail("expected a constant or a variable");
}

std::unique_ptr<ExprNode> Parser::parse_term() {
  std::unique_ptr<ExprNode> node = parse_factor();
  const token *tok;
  while ((tok = peek()) && is(*tok, TK_OPERATOR, "*")) {
    eat();
    std::unique_ptr<ExprNode> right = parse_factor();
    node = std::make_unique<BinOpNode>(tok->line, tok->col, "*",
                                       std::move(node), std::move(right));
  }
  return node;
}

std::unique_ptr<ExprNode> Parser::parse_arith() {
  std::unique_ptr<ExprNode> node = parse_term();
  const token *tok;
  while ((tok = peek()) &&
         (is(*tok, TK_OPERATOR, "+") || is(*tok, TK_OPERATOR, "-"))) {
    eat();
    std::unique_ptr<ExprNode> right = parse_term();
    node = std::make_unique<BinOpNode>(tok->line, tok->col, tok->text,
                                       std::move(node), std::move(right));
  }
  return node;
}

static bool is_comparison(const token *tok) {
  return tok && (is(*tok, TK_OPERATOR, "==") || is(*tok, TK_OPERATOR, "!="));
}

std::unique_ptr<ExprNode> Parser::parse_expr() {
  std::unique_ptr<ExprNode> node = parse_arith();
  const token *tok = peek();
  if (!is_comparison(tok))
    return node;
  eat();
  std::unique_ptr<ExprNode> right = parse_arith();
  node = std::make_unique<BinOpNode>(tok->line, tok->col, tok->text,
                                     std::move(node), std::move(right));
  tok = peek();
  if (is_comparison(tok))
    error(tok->line, tok->col,
          "only one comparison is allowed in an expression, got " +
              describe(*tok));
  return node;
}

static bool is_type(const token &tok) {
  return is(tok, TK_KEYWORD, "i32") || is(tok, TK_KEYWORD, "i64") ||
         is(tok, TK_KEYWORD, "bool");
}

const token &Parser::expect_type(const std::string &what) {
  const token *tok = peek();
  if (!tok || (!is_type(*tok) && tok->kind != TK_IDENT))
    fail("expected " + what);
  return eat();
}

token Parser::open_brace(const std::string &after) {
  std::string want = "expected '{' on its own line after '" + after + "'";
  const std::vector<token> *line = peek_line();
  if (!line) {
    const token &last = toks.back();
    error(last.line, last.col + last.text.size(), want + ", found end of file");
  }
  if (!is(line->front(), TK_BLOCK, "{"))
    error(line->front().line, line->front().col,
          want + ", got " + describe(line->front()));
  next_line();
  token brace = eat();
  expect_end("'{'");
  return brace;
}

std::unique_ptr<FieldNode> Parser::parse_field() {
  const token &type = expect_type("a field type");
  bool mut = match(TK_KEYWORD, "mut");
  const token &name = expect(TK_IDENT, "", "a field name");
  expect_end("field '" + name.text + "'");
  return std::make_unique<FieldNode>(name.line, name.col, type.text, type.col,
                                     name.text, mut);
}

std::unique_ptr<StructNode> Parser::parse_struct() {
  eat();
  token name = expect(TK_IDENT, "", "a struct name");
  expect_end("'" + name.text + "'");
  token brace = open_brace("struct " + name.text);

  std::vector<std::unique_ptr<FieldNode>> fields;
  while (true) {
    const std::vector<token> *line = peek_line();
    if (!line)
      error(brace.line, brace.col, "'{' is never closed");
    if (is(line->front(), TK_BLOCK, "}"))
      break;
    next_line();
    fields.push_back(parse_field());
  }
  next_line();
  eat();
  expect_end("'}'");

  if (fields.empty())
    error(brace.line, brace.col, "struct '" + name.text + "' has no fields");
  return std::make_unique<StructNode>(name.line, name.col, name.text,
                                      std::move(fields));
}

std::unique_ptr<FnNode> Parser::parse_fn() {
  eat();
  token name = expect(TK_IDENT, "", "a function name");
  expect(TK_OPERATOR, ":=", "':=' after '" + name.text + "'");
  expect(TK_SEPARATOR, "(", "'(' after ':='");
  std::vector<std::unique_ptr<ParamNode>> params;
  if (!match(TK_SEPARATOR, ")")) {
    do {
      const token &type = expect_type("a parameter type");
      const token &param = expect(TK_IDENT, "", "a parameter name");
      params.push_back(std::make_unique<ParamNode>(
          param.line, param.col, type.text, type.col, param.text));
    } while (match(TK_SEPARATOR, ","));
    expect(TK_SEPARATOR, ")", "')'");
  }
  expect(TK_SEPARATOR, "->", "'->' after ')'");
  token result = expect_type("a result type");
  expect_end("the result type");

  std::unique_ptr<BlockNode> body = parse_block("fn " + name.text);
  if (!body->exit)
    error(toks.front().line, toks.front().col,
          "function '" + name.text + "' must end with 'exit'");
  return std::make_unique<FnNode>(name.line, name.col, name.text,
                                  std::move(params), result.text, result.col,
                                  std::move(body));
}

std::unique_ptr<InitNode> Parser::parse_init(const token &brace) {
  std::vector<std::unique_ptr<ExprNode>> values;
  do {
    const token *tok = peek();
    if (tok && is(*tok, TK_BLOCK, "{"))
      values.push_back(parse_init(eat()));
    else
      values.push_back(parse_expr());
  } while (match(TK_SEPARATOR, ","));
  expect(TK_BLOCK, "}", "'}'");
  return std::make_unique<InitNode>(brace.line, brace.col, std::move(values));
}

std::unique_ptr<StmtNode> Parser::parse_decl() {
  const token &type = eat();
  bool mut = match(TK_KEYWORD, "mut");
  const token &name = expect(TK_IDENT, "", "a variable name");
  if (!peek())
    error(name.line, name.col,
          "variable '" + name.text + "' needs an initialiser in {}");
  const token &brace =
      expect(TK_BLOCK, "{", "'{' after '" + name.text + "'");
  return std::make_unique<DeclNode>(name.line, name.col, type.text, type.col,
                                    name.text, mut, parse_init(brace));
}

std::unique_ptr<StmtNode> Parser::parse_assign() {
  const token &name = eat();
  std::vector<Link> links = parse_links();
  expect(TK_OPERATOR, ":=", "':=' after '" + chain(name.text, links) + "'");
  return std::make_unique<AssignNode>(name.line, name.col, name.text,
                                      std::move(links), parse_expr());
}

std::unique_ptr<BlockNode> Parser::parse_block(const std::string &after) {
  token brace = open_brace(after);

  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit_node;
  while (true) {
    const std::vector<token> *line = peek_line();
    if (!line)
      error(brace.line, brace.col, "'{' is never closed");
    const token &first = line->front();
    if (is(first, TK_BLOCK, "}"))
      break;
    if (exit_node)
      error(first.line, first.col, "statement after 'exit' in the same block");
    next_line();
    if (is(first, TK_KEYWORD, "exit")) {
      exit_node = parse_exit();
      expect_end();
    } else {
      statements.push_back(parse_statement());
    }
  }
  next_line();
  eat();
  expect_end("'}'");

  if (statements.empty() && !exit_node)
    error(brace.line, brace.col, "empty block");
  return std::make_unique<BlockNode>(brace.line, brace.col,
                                     std::move(statements),
                                     std::move(exit_node));
}

std::unique_ptr<StmtNode> Parser::parse_if() {
  token kw = eat();
  std::unique_ptr<ExprNode> cond = parse_expr();
  expect_end();
  std::unique_ptr<BlockNode> then_block = parse_block("if");

  std::unique_ptr<BlockNode> else_block;
  const std::vector<token> *line = peek_line();
  if (line && is(line->front(), TK_KEYWORD, "else")) {
    next_line();
    eat();
    expect_end("'else'");
    else_block = parse_block("else");
  }
  return std::make_unique<IfNode>(kw.line, kw.col, std::move(cond),
                                  std::move(then_block), std::move(else_block));
}

std::unique_ptr<StmtNode> Parser::parse_while() {
  token kw = eat();
  std::unique_ptr<ExprNode> cond = parse_expr();
  expect_end();
  std::unique_ptr<BlockNode> body = parse_block("while");
  return std::make_unique<WhileNode>(kw.line, kw.col, std::move(cond),
                                     std::move(body));
}

std::unique_ptr<StmtNode> Parser::parse_statement() {
  const token &tok = *peek();
  const token *second = peek(1);
  bool struct_decl =
      tok.kind == TK_IDENT && second &&
      (second->kind == TK_IDENT || is(*second, TK_KEYWORD, "mut"));
  if (is_type(tok) || struct_decl) {
    std::unique_ptr<StmtNode> decl = parse_decl();
    expect_end();
    return decl;
  }
  if (tok.kind == TK_IDENT && second && is(*second, TK_SEPARATOR, "("))
    error(tok.line, tok.col, "a call alone is not a statement");
  if (tok.kind == TK_IDENT) {
    std::unique_ptr<StmtNode> assign = parse_assign();
    expect_end();
    return assign;
  }
  if (is(tok, TK_KEYWORD, "if"))
    return parse_if();
  if (is(tok, TK_KEYWORD, "while"))
    return parse_while();
  if (is(tok, TK_KEYWORD, "else"))
    error(tok.line, tok.col, "'else' without an 'if'");
  if (is(tok, TK_BLOCK, "}"))
    error(tok.line, tok.col, "'}' without a '{'");
  if (is(tok, TK_KEYWORD, "struct"))
    error(tok.line, tok.col, "a struct can be declared only at the top level");
  if (is(tok, TK_KEYWORD, "fn"))
    error(tok.line, tok.col,
          "a function can be declared only at the top level");
  error(tok.line, tok.col, "cannot start a statement with " + describe(tok));
}

std::unique_ptr<ExitNode> Parser::parse_exit() {
  const token &kw = eat();
  return std::make_unique<ExitNode>(kw.line, kw.col, parse_factor());
}

std::unique_ptr<ProgramNode> Parser::parse_program() {
  std::vector<std::unique_ptr<StructNode>> structs;
  std::vector<std::unique_ptr<FnNode>> functions;
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit_node;

  while (const std::vector<token> *line = peek_line()) {
    const token &first = line->front();
    if (exit_node)
      error(first.line, first.col, "exit must be the last statement");
    next_line();
    if (is(first, TK_KEYWORD, "struct")) {
      if (!statements.empty())
        error(first.line, first.col,
              "a struct must be declared before the first statement");
      if (!functions.empty())
        error(first.line, first.col,
              "a struct must be declared before the first function");
      structs.push_back(parse_struct());
    } else if (is(first, TK_KEYWORD, "fn")) {
      if (!statements.empty())
        error(first.line, first.col,
              "a function must be declared before the first statement");
      functions.push_back(parse_fn());
    } else if (is(first, TK_KEYWORD, "exit")) {
      exit_node = parse_exit();
      expect_end();
    } else {
      statements.push_back(parse_statement());
    }
  }

  if (!exit_node)
    error(lines.empty() ? 1 : lines.back().front().line, 1,
          "the program has no exit");
  return std::make_unique<ProgramNode>(1, 1, std::move(structs),
                                       std::move(functions),
                                       std::move(statements),
                                       std::move(exit_node));
}
