#include "semantic.h"
#include "error.h"

static bool is_integer(const std::string &type) {
  return type == "i32" || type == "i64";
}

static bool is_builtin(const std::string &type) {
  return is_integer(type) || type == "bool";
}

static InitNode *as_init(ExprNode &node) {
  return dynamic_cast<InitNode *>(&node);
}

static void check_count(const InitNode &init, size_t want) {
  if (init.values.size() != want)
    error(init.line, init.col,
          "wrong number of values in {} for " + init.type + ": expected " +
              std::to_string(want) + ", got " +
              std::to_string(init.values.size()));
}

static std::string const_field(const StructNode &type) {
  for (const std::unique_ptr<FieldNode> &field : type.fields) {
    if (!field->mut)
      return field->name;
    if (field->struct_type) {
      std::string inner = const_field(*field->struct_type);
      if (!inner.empty())
        return field->name + "." + inner;
    }
  }
  return "";
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
  if (functions.count(name))
    error(at.line, at.col, "'" + name + "' is a function, not a variable");
  error(at.line, at.col,
        "variable '" + name + "' is used before its declaration");
}

const StructNode *SemanticChecker::resolve_type(const std::string &type,
                                                uint64_t line,
                                                uint64_t col) const {
  if (is_builtin(type))
    return nullptr;
  auto it = structs.find(type);
  if (it == structs.end())
    error(line, col, "unknown type '" + type + "'");
  return it->second;
}

const FieldNode *SemanticChecker::resolve_links(const DeclNode &decl,
                                                std::vector<Link> &links,
                                                uint64_t line,
                                                bool write) const {
  std::string path = decl.name;
  const std::string *type = &decl.type_name;
  const StructNode *owner = decl.struct_type;
  const FieldNode *field = nullptr;
  for (Link &link : links) {
    if (!owner)
      error(line, link.col,
            "'" + path + "' of type " + *type + " has no fields");
    field = nullptr;
    for (unsigned i = 0; i < owner->fields.size() && !field; i++) {
      if (owner->fields[i]->name == link.name) {
        field = owner->fields[i].get();
        link.index = i;
      }
    }
    if (!field)
      error(line, link.col,
            "struct " + *type + " has no field '" + link.name + "'");
    if (write && !field->mut)
      error(line, link.col,
            "cannot assign to '" + chain(decl.name, links) + "': field '" +
                link.name + "' is not mut");
    path += "." + link.name;
    type = &field->type_name;
    owner = field->struct_type;
  }
  return field;
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
  for (std::unique_ptr<StructNode> &s : node.structs)
    s->accept(*this);
  for (std::unique_ptr<FnNode> &fn : node.functions)
    declare_fn(*fn);
  for (std::unique_ptr<FnNode> &fn : node.functions)
    fn->accept(*this);
  scopes.push_back({});
  for (std::unique_ptr<StmtNode> &stmt : node.statements)
    stmt->accept(*this);
  node.exit->accept(*this);
  scopes.pop_back();
}

void SemanticChecker::visit_struct(StructNode &node) {
  if (structs.count(node.name))
    error(node.line, node.col,
          "struct '" + node.name + "' is already declared");
  std::map<std::string, const FieldNode *> seen;
  for (std::unique_ptr<FieldNode> &field : node.fields) {
    if (field->type_name == node.name)
      error(field->line, field->type_col,
            "struct '" + node.name + "' cannot contain itself");
    field->struct_type =
        resolve_type(field->type_name, field->line, field->type_col);
    if (!seen.emplace(field->name, field.get()).second)
      error(field->line, field->col,
            "field '" + field->name + "' is already declared in struct '" +
                node.name + "'");
  }
  structs[node.name] = &node;
}

void SemanticChecker::declare_fn(FnNode &node) {
  if (functions.count(node.name))
    error(node.line, node.col,
          "function '" + node.name + "' is already declared");
  std::map<std::string, const ParamNode *> seen;
  for (std::unique_ptr<ParamNode> &param : node.params) {
    param->struct_type =
        resolve_type(param->type_name, param->line, param->type_col);
    if (!seen.emplace(param->name, param.get()).second)
      error(param->line, param->col,
            "parameter '" + param->name + "' is already declared");
  }
  resolve_type(node.result_type, node.line, node.result_col);
  functions[node.name] = &node;
}

void SemanticChecker::visit_fn(FnNode &node) {
  current_fn = &node;
  scopes.push_back({});
  for (std::unique_ptr<ParamNode> &param : node.params)
    scopes.back()[param->name] = param.get();
  for (std::unique_ptr<StmtNode> &stmt : node.body->statements)
    stmt->accept(*this);
  node.body->exit->accept(*this);
  scopes.pop_back();
  current_fn = nullptr;
}

void SemanticChecker::visit_decl(DeclNode &node) {
  node.struct_type = resolve_type(node.type_name, node.line, node.type_col);
  if (scopes.back().count(node.name))
    error(node.line, node.col,
          "variable '" + node.name + "' is already declared" +
              (scopes.size() > 1 ? " in this block" : ""));
  InitNode &init = *node.init;
  init.type = node.type_name;
  init.struct_type = node.struct_type;
  if (node.struct_type) {
    check(init);
  } else {
    check_count(init, 1);
    ExprNode &value = *init.values[0];
    if (!as_init(value))
      check(value);
    check_value(value, node.type_name, nullptr, node, "'" + node.name + "'");
  }
  scopes.back()[node.name] = &node;
}

void SemanticChecker::check_value(ExprNode &value, const std::string &type,
                                  const StructNode *struct_type,
                                  const Node &at, const std::string &target) {
  InitNode *init = as_init(value);
  if (!init) {
    check_assignable(value, type, at, "initialise " + target);
    return;
  }
  if (!struct_type)
    error(init->line, init->col,
          "cannot initialise " + target + " of type " + type + " with {}");
  init->type = type;
  init->struct_type = struct_type;
  check(*init);
}

void SemanticChecker::visit_init(InitNode &node) {
  const std::vector<std::unique_ptr<FieldNode>> &fields =
      node.struct_type->fields;
  for (std::unique_ptr<ExprNode> &value : node.values)
    if (!as_init(*value))
      check(*value);
  node.copy = node.values.size() == 1 && node.values[0]->type == node.type;
  if (node.copy)
    return;
  check_count(node, fields.size());
  for (size_t i = 0; i < fields.size(); i++)
    check_value(*node.values[i], fields[i]->type_name, fields[i]->struct_type,
                *node.values[i], "field '" + fields[i]->name + "'");
}

void SemanticChecker::visit_assign(AssignNode &node) {
  const DeclNode &decl = lookup(node.name, node);
  std::string target = chain(node.name, node.links);
  std::string object = node.links.empty() ? "it" : "'" + node.name + "'";
  if (dynamic_cast<const ParamNode *>(&decl))
    error(node.line, node.col,
          "cannot assign to '" + target + "': " + object + " is a parameter");
  if (!decl.mut)
    error(node.line, node.col,
          "cannot assign to '" + target + "': " + object + " is not mut");
  const FieldNode *field = resolve_links(decl, node.links, node.line, true);
  node.decl = &decl;
  node.type = field ? field->type_name : decl.type_name;
  const StructNode *replaced = field ? field->struct_type : decl.struct_type;
  if (replaced) {
    std::string inner = const_field(*replaced);
    if (!inner.empty())
      error(node.line, node.col,
            "cannot assign to '" + target + "': its field '" + inner +
                "' is not mut");
  }
  check(*node.value);
  check_assignable(*node.value, node.type, node, "assign to '" + target + "'");
}

void SemanticChecker::visit_exit(ExitNode &node) {
  const std::string &type = check(*node.value);
  if (current_fn)
    check_assignable(*node.value, current_fn->result_type, *node.value,
                     "exit function '" + current_fn->name + "'");
  else if (!is_builtin(type))
    error(node.value->line, node.value->col,
          "cannot exit with a value of type " + type);
}

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
    if (is_integer(lt) != is_integer(rt) || !is_builtin(lt) || !is_builtin(rt))
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
  const FieldNode *field =
      resolve_links(*node.decl, node.links, node.line, false);
  node.type = field ? field->type_name : node.decl->type_name;
}

void SemanticChecker::visit_call(CallNode &node) {
  auto it = functions.find(node.name);
  if (it == functions.end())
    error(node.line, node.col,
          "function '" + node.name + "' is not declared");
  node.fn = it->second;
  const std::vector<std::unique_ptr<ParamNode>> &params = node.fn->params;
  if (node.args.size() != params.size())
    error(node.line, node.col,
          "wrong number of arguments for '" + node.name + "': expected " +
              std::to_string(params.size()) + ", got " +
              std::to_string(node.args.size()));
  for (size_t i = 0; i < params.size(); i++) {
    check(*node.args[i]);
    check_assignable(*node.args[i], params[i]->type_name, *node.args[i],
                     "initialise parameter '" + params[i]->name + "'");
  }
  node.type = node.fn->result_type;
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
