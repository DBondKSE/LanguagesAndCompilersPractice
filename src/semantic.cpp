#include "semantic.h"
#include "error.h"

static bool is_integer(const std::string &type) {
  return type == "i32" || type == "i64";
}

const std::string &SemanticChecker::check(ExprNode &node) {
  node.accept(*this);
  return node.type;
}

const DeclNode &SemanticChecker::lookup(const std::string &name,
                                        const Node &at) const {
  for (auto frame = scopes.rbegin(); frame != scopes.rend(); ++frame) {
    auto it = frame->find(name);
    if (it != frame->end())
      return *it->second;
  }
  error(at.line, at.col,
        "variable '" + name + "' is used before its declaration");
}

void SemanticChecker::check_assignable(const ExprNode &expr,
                                       const std::string &want,
                                       const Node &at,
                                       const std::string &what) const {
  const std::string &have = expr.type;
  if (have == want || (have == "i32" && want == "i64"))
    return;
  const ConstNode *c = dynamic_cast<const ConstNode *>(&expr);
  if (c && is_integer(want))
    error(c->line, c->col, "constant " + c->text + " does not fit in " + want);
  error(at.line, at.col,
        "cannot " + what + " of type " + want + " with a value of type " +
            have);
}

void SemanticChecker::visit_program(ProgramNode &node) {
  scopes.push_back({});
  for (std::unique_ptr<StmtNode> &stmt : node.statements)
    stmt->accept(*this);
  node.exit->accept(*this);
  scopes.pop_back();
}

void SemanticChecker::visit_decl(DeclNode &node) {
  if (scopes.back().count(node.name))
    error(node.line, node.col,
          "variable '" + node.name + "' is already declared" +
              (scopes.size() > 1 ? " in this block" : ""));
  check(*node.init);
  check_assignable(*node.init, node.type_name, node,
                   "initialise '" + node.name + "'");
  scopes.back()[node.name] = &node;
}

void SemanticChecker::visit_assign(AssignNode &node) {
  const DeclNode &decl = lookup(node.name, node);
  if (!decl.mut)
    error(node.line, node.col,
          "cannot assign to '" + node.name + "': it is not mut");
  node.decl = &decl;
  check(*node.value);
  check_assignable(*node.value, decl.type_name, node,
                   "assign to '" + node.name + "'");
}

void SemanticChecker::visit_exit(ExitNode &node) { check(*node.value); }

void SemanticChecker::visit_block(BlockNode &node) {
  scopes.push_back({});
  for (std::unique_ptr<StmtNode> &stmt : node.statements)
    stmt->accept(*this);
  if (node.exit)
    node.exit->accept(*this);
  scopes.pop_back();
}

void SemanticChecker::visit_if(IfNode &node) {
  const std::string &type = check(*node.cond);
  if (type != "bool")
    error(node.line, node.col,
          "the condition of 'if' must be bool, got " + type);
  node.then_block->accept(*this);
  if (node.else_block)
    node.else_block->accept(*this);
}

void SemanticChecker::visit_while(WhileNode &node) {
  const std::string &type = check(*node.cond);
  if (type != "bool")
    error(node.line, node.col,
          "the condition of 'while' must be bool, got " + type);
  node.body->accept(*this);
}

void SemanticChecker::visit_not(NotNode &node) {
  const std::string &type = check(*node.operand);
  if (type != "bool")
    error(node.line, node.col, "cannot apply '!' to " + type);
  node.type = "bool";
}

void SemanticChecker::visit_binop(BinOpNode &node) {
  const std::string &lt = check(*node.left);
  const std::string &rt = check(*node.right);
  if (node.op == "==" || node.op == "!=") {
    if (is_integer(lt) != is_integer(rt))
      error(node.line, node.col, "cannot compare " + lt + " with " + rt);
    node.type = "bool";
    return;
  }
  if (!is_integer(lt) || !is_integer(rt))
    error(node.line, node.col,
          "cannot apply '" + node.op + "' to " + (is_integer(lt) ? rt : lt));
  node.type = lt == "i64" || rt == "i64" ? "i64" : "i32";
}

void SemanticChecker::visit_var(VarNode &node) {
  node.decl = &lookup(node.name, node);
  node.type = node.decl->type_name;
}

void SemanticChecker::visit_const(ConstNode &node) {
  uint64_t v = 0;
  for (char c : node.text) {
    uint64_t digit = c - '0';
    if (v > (INT64_MAX - digit) / 10)
      error(node.line, node.col,
            "constant " + node.text + " does not fit in i64");
    v = v * 10 + digit;
  }
  node.value = (int64_t)v;
  node.type = v > INT32_MAX ? "i64" : "i32";
}

void SemanticChecker::visit_bool(BoolNode &node) { node.type = "bool"; }
