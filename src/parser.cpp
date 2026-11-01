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

static std::unique_ptr<ExprNode> chain_end(const token &name,
                                           std::unique_ptr<ExprNode> node,
                                           std::vector<Link> links,
                                           uint64_t dot_col) {
  if (!node)
    return std::make_unique<VarNode>(name.line, name.col, name.text,
                                     std::move(links));
  if (links.empty())
    return node;
  return std::make_unique<FieldOfNode>(name.line, dot_col, std::move(node),
                                       std::move(links));
}

std::unique_ptr<ExprNode> Parser::parse_chain(const token &name) {
  std::unique_ptr<ExprNode> node;
  if (match(TK_SEPARATOR, "("))
    node = std::make_unique<CallNode>(name.line, name.col, name.text,
                                      parse_args());
  std::vector<Link> links;
  uint64_t dot_col = 0;
  const token *dot;
  while ((dot = peek()) && is(*dot, TK_OPERATOR, ".")) {
    eat();
    if (links.empty())
      dot_col = dot->col;
    const token &field = expect(TK_IDENT, "", "a field name");
    if (!match(TK_SEPARATOR, "(")) {
      links.push_back({field.text, field.col});
      continue;
    }
    std::unique_ptr<ExprNode> object =
        chain_end(name, std::move(node), std::move(links), dot_col);
    links.clear();
    node = std::make_unique<MemberCallNode>(name.line, field.col,
                                            std::move(object), dot->col,
                                            field.text, parse_args());
  }
  return chain_end(name, std::move(node), std::move(links), dot_col);
}

std::vector<std::unique_ptr<ExprNode>> Parser::parse_args() {
  std::vector<std::unique_ptr<ExprNode>> args;
  if (match(TK_SEPARATOR, ")"))
    return args;
  do
    args.push_back(parse_expr());
  while (match(TK_SEPARATOR, ","));
  expect(TK_SEPARATOR, ")", "')'");
  return args;
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
    return parse_chain(*tok);
  }
  if (tok && is(*tok, TK_KEYWORD, "read")) {
    eat();
    expect(TK_SEPARATOR, "(", "'(' after 'read'");
    const token *arg = peek();
    if (arg && !is(*arg, TK_SEPARATOR, ")"))
      error(arg->line, arg->col, "'read' takes no arguments");
    expect(TK_SEPARATOR, ")", "')'");
    return std::make_unique<ReadNode>(tok->line, tok->col);
  }
  if (tok && is(*tok, TK_KEYWORD, "print"))
    error(tok->line, tok->col, "'print' is a statement and has no value");
  if (tok && tok->kind == TK_STRING)
    error(tok->line, tok->col,
          "a string literal can only be the argument of 'print'");
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

template <class Fn> std::unique_ptr<Fn> Parser::parse_fn() {
  eat();
  token name = expect(TK_IDENT, "", "a function name");
  expect(TK_OPERATOR, ":=", "':=' after '" + name.text + "'");
  return parse_signature<Fn>(name, "':='");
}

template <class Fn>
std::unique_ptr<Fn> Parser::parse_signature(const token &name,
                                            const std::string &after) {
  expect(TK_SEPARATOR, "(", "'(' after " + after);
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
  return std::make_unique<Fn>(name.line, name.col, name.text,
                              std::move(params), result.text, result.col,
                              std::move(body));
}

bool Parser::at_lambda() const {
  const token *name = peek(1), *assign = peek(2), *bracket = peek(3);
  return name && name->kind == TK_IDENT && assign &&
         is(*assign, TK_OPERATOR, ":=") && bracket &&
         is(*bracket, TK_SEPARATOR, "[");
}

std::unique_ptr<StmtNode> Parser::parse_lambda() {
  eat();
  token name = eat();
  eat();
  eat();
  std::vector<Capture> captures;
  if (!match(TK_SEPARATOR, "]")) {
    do {
      bool mut = match(TK_KEYWORD, "mut");
      const token &capture = expect(TK_IDENT, "", "a captured variable name");
      captures.push_back({capture.text, capture.line, capture.col, mut});
    } while (match(TK_SEPARATOR, ","));
    expect(TK_SEPARATOR, "]", "']'");
  }
  std::unique_ptr<MemberFnNode> call =
      parse_signature<MemberFnNode>(name, "']'");
  return std::make_unique<LambdaNode>(name.line, name.col, name.text,
                                      std::move(captures), std::move(call));
}

std::unique_ptr<StructNode> Parser::parse_struct() {
  eat();
  token name = expect(TK_IDENT, "", "a struct name");
  expect_end("'" + name.text + "'");
  token brace = open_brace("struct " + name.text);

  std::vector<std::unique_ptr<FieldNode>> fields;
  std::vector<std::unique_ptr<MemberFnNode>> methods;
  while (true) {
    const std::vector<token> *line = peek_line();
    if (!line)
      error(brace.line, brace.col, "'{' is never closed");
    const token &first = line->front();
    if (is(first, TK_BLOCK, "}"))
      break;
    next_line();
    if (is(first, TK_KEYWORD, "fn")) {
      if (at_lambda())
        error(peek(3)->line, peek(3)->col,
              "a member function cannot have a capture list");
      if (fields.empty())
        error(first.line, first.col,
              "a member function cannot come before the first field of "
              "struct '" +
                  name.text + "'");
      methods.push_back(parse_fn<MemberFnNode>());
    } else {
      if (!methods.empty())
        error(first.line, first.col,
              "a field cannot follow a member function in struct '" +
                  name.text + "'");
      fields.push_back(parse_field());
    }
  }
  next_line();
  eat();
  expect_end("'}'");

  if (fields.empty())
    error(brace.line, brace.col, "struct '" + name.text + "' has no fields");
  return std::make_unique<StructNode>(name.line, name.col, name.text,
                                      std::move(fields), std::move(methods));
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
  std::unique_ptr<ExprNode> target = parse_chain(name);
  VarNode *var = dynamic_cast<VarNode *>(target.get());
  if (!var) {
    const token *tok = peek();
    if (tok && is(*tok, TK_OPERATOR, ":="))
      error(name.line, name.col, "cannot assign to the result of a call");
    error(name.line, name.col, "a call alone is not a statement");
  }
  expect(TK_OPERATOR, ":=",
         "':=' after '" + chain(var->name, var->links) + "'");
  return std::make_unique<AssignNode>(name.line, name.col, name.text,
                                      std::move(var->links), parse_expr());
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

std::unique_ptr<StmtNode> Parser::parse_print() {
  token kw = eat();
  expect(TK_SEPARATOR, "(", "'(' after 'print'");
  const token *tok = peek();
  if (tok && is(*tok, TK_SEPARATOR, ")"))
    error(kw.line, kw.col, "'print' takes one argument, got none");
  std::unique_ptr<ExprNode> value;
  std::string text;
  if (tok && tok->kind == TK_STRING) {
    const std::string &literal = eat().text;
    text = literal.substr(1, literal.size() - 2);
  } else {
    value = parse_expr();
  }
  tok = peek();
  if (tok && is(*tok, TK_SEPARATOR, ","))
    error(tok->line, tok->col, "'print' takes only one argument");
  expect(TK_SEPARATOR, ")", "')'");
  return std::make_unique<PrintNode>(kw.line, kw.col, std::move(value), text);
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
  if (tok.kind == TK_IDENT) {
    std::unique_ptr<StmtNode> assign = parse_assign();
    expect_end();
    return assign;
  }
  if (is(tok, TK_KEYWORD, "print")) {
    std::unique_ptr<StmtNode> print = parse_print();
    expect_end();
    return print;
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
  if (is(tok, TK_KEYWORD, "fn") && at_lambda())
    return parse_lambda();
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
    } else if (is(first, TK_KEYWORD, "fn") && at_lambda()) {
      statements.push_back(parse_lambda());
    } else if (is(first, TK_KEYWORD, "fn")) {
      if (!statements.empty())
        error(first.line, first.col,
              "a function must be declared before the first statement");
      functions.push_back(parse_fn<FnNode>());
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
