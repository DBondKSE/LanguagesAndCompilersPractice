#include "semantic.h"
#include "error.h"

#include <set>

static bool is_integer(const std::string &type) {
  return type == "i32" || type == "i64";
}

static bool is_builtin(const std::string &type) {
  return is_integer(type) || type == "bool";
}

static bool is_lambda(const std::string &type) {
  return type.rfind("lambda.", 0) == 0;
}

static const FieldNode *field_named(const StructNode &type,
                                    const std::string &name) {
  for (const std::unique_ptr<FieldNode> &field : type.fields)
    if (field->name == name)
      return field.get();
  return nullptr;
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

const std::string &SemanticChecker::check(std::unique_ptr<ExprNode> &slot) {
  check(*slot);
  if (replacement)
    slot = std::move(replacement);
  return slot->type;
}

const DeclNode *SemanticChecker::find(const std::string &name) const {
  for (size_t i = scopes.size(); i > barrier; i--) {
    auto it = scopes[i - 1].find(name);
    if (it != scopes[i - 1].end())
      return it->second;
  }
  return nullptr;
}

bool SemanticChecker::visible_outside(const std::string &name) const {
  for (size_t i = 0; i < barrier; i++)
    if (scopes[i].count(name))
      return true;
  return enclosing && field_named(*enclosing->owner, name);
}

void SemanticChecker::not_captured(const std::string &name,
                                   const Node &at) const {
  error(at.line, at.col,
        "'" + name + "' is not captured by lambda '" + current_lambda->name +
            "': add it to the capture list, [" + name + "]");
}

void SemanticChecker::reject_lambda(const ExprNode &value,
                                    const std::string &action,
                                    const std::string &holder) const {
  if (!is_lambda(value.type))
    return;
  const VarNode &var = dynamic_cast<const VarNode &>(value);
  std::string name = var.own_field ? var.links.front().name : var.name;
  error(value.line, value.col,
        "'" + name + "' is a lambda and cannot " + action +
            ": its type has no name" +
            (holder.empty() ? "" : ", so no " + holder + " can have it"));
}

const DeclNode &SemanticChecker::lookup(const std::string &name,
                                        const Node &at) const {
  if (const DeclNode *decl = find(name))
    return *decl;
  if (functions.count(name))
    error(at.line, at.col, "'" + name + "' is a function, not a variable");
  error(at.line, at.col,
        "variable '" + name + "' is used before its declaration");
}

static const MemberFnNode *method(const StructNode &type,
                                  const std::string &name) {
  for (const std::unique_ptr<MemberFnNode> &m : type.methods)
    if (m->name == name)
      return m.get();
  return nullptr;
}

bool SemanticChecker::is_self(const DeclNode &decl) const {
  return current_member && &decl == current_member->self.get();
}

bool SemanticChecker::is_own_field(const std::string &name) const {
  return current_member && field_named(*current_member->owner, name);
}

const DeclNode &SemanticChecker::root(const std::string &name,
                                      std::vector<Link> &links,
                                      const Node &at) const {
  if (const DeclNode *decl = find(name))
    return *decl;
  if (is_own_field(name)) {
    links.insert(links.begin(), Link{name, at.col});
    return *current_member->self;
  }
  if (current_lambda && visible_outside(name))
    not_captured(name, at);
  if (current_member && !current_lambda)
    error(at.line, at.col,
          "'" + name +
              "' is not a parameter, a local variable or a field of struct '" +
              current_member->owner->name + "'");
  return lookup(name, at);
}

std::string SemanticChecker::display(const DeclNode &decl,
                                     const std::vector<Link> &links) const {
  if (!is_self(decl))
    return chain(decl.name, links);
  return chain(links.front().name,
               std::vector<Link>(links.begin() + 1, links.end()));
}

std::string SemanticChecker::const_link(const VarNode &object) const {
  const DeclNode &decl = *object.decl;
  if (dynamic_cast<const ParamNode *>(&decl))
    return "'" + decl.name + "' is a parameter";
  if (!decl.mut)
    return "'" + decl.name + "' is not mut";
  const StructNode *owner = decl.struct_type;
  for (const Link &link : object.links) {
    const FieldNode &field = *owner->fields[link.index];
    if (!field.mut && current_lambda && is_self(decl) &&
        &link == &object.links.front())
      return "the capture '" + link.name + "' is const";
    if (!field.mut)
      return "field '" + link.name + "' is not mut";
    owner = field.struct_type;
  }
  return "";
}

void SemanticChecker::check_mutation(const MemberCallNode &call,
                                     const std::string &target,
                                     const std::string &blocker) const {
  if (!call.fn->mutating)
    return;
  if (!call.callee.empty())
    error(call.line, call.col,
          "cannot call '" + target + "': its 'call' changes it, and " +
              blocker);
  error(call.line, call.col,
        "cannot call '" + call.name + "' on '" + target +
            "': it changes the object and " + blocker);
}

void SemanticChecker::mark_mutating(ProgramNode &program) {
  std::vector<MemberFnNode *> members(lambdas);
  for (std::unique_ptr<StructNode> &s : program.structs)
    for (std::unique_ptr<MemberFnNode> &m : s->methods)
      members.push_back(m.get());
  bool changed = true;
  while (changed) {
    changed = false;
    for (MemberFnNode *m : members)
      for (const MemberFnNode *callee : m->self_calls)
        if (!m->mutating && callee->mutating) {
          m->mutating = true;
          changed = true;
        }
  }
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

static std::string show(const ExprNode &expr) {
  if (const VarNode *v = dynamic_cast<const VarNode *>(&expr)) {
    if (v->own_field)
      return chain(v->name, {v->links.begin() + 1, v->links.end()});
    return chain(v->name, v->links);
  }
  if (const CallNode *c = dynamic_cast<const CallNode *>(&expr))
    return c->name + (c->args.empty() ? "()" : "(...)");
  if (const MemberCallNode *m = dynamic_cast<const MemberCallNode *>(&expr))
    return (m->callee.empty() ? show(*m->object) + "." + m->name : m->callee) +
           (m->args.empty() ? "()" : "(...)");
  const FieldOfNode &f = dynamic_cast<const FieldOfNode &>(expr);
  return chain(show(*f.object), f.links);
}

const FieldNode *SemanticChecker::resolve_links(const DeclNode &decl,
                                                std::vector<Link> &links,
                                                uint64_t line,
                                                bool write) const {
  return walk_links(is_self(decl) ? "" : decl.name, decl.type_name,
                    decl.struct_type, links, line, write ? &decl : nullptr);
}

const FieldNode *SemanticChecker::walk_links(std::string path,
                                             const std::string &type_name,
                                             const StructNode *owner,
                                             std::vector<Link> &links,
                                             uint64_t line,
                                             const DeclNode *written) const {
  const std::string *type = &type_name;
  const FieldNode *field = nullptr;
  for (Link &link : links) {
    if (!owner)
      error(line, link.col,
            "'" + path + "' of type " + *type + " has no fields");
    if (is_lambda(owner->name) &&
        !(current_lambda && owner == current_member->owner))
      error(line, link.col,
            "'" + path +
                "' is a lambda: its captures are not visible outside its body");
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
    if (written && !field->mut)
      error(line, link.col,
            "cannot assign to '" + display(*written, links) + "': field '" +
                link.name + "' is not mut");
    path += (path.empty() ? "" : ".") + link.name;
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
  for (std::unique_ptr<StructNode> &s : node.structs)
    for (std::unique_ptr<MemberFnNode> &m : s->methods)
      declare_params(*m);
  for (std::unique_ptr<FnNode> &fn : node.functions)
    declare_fn(*fn);
  for (std::unique_ptr<StructNode> &s : node.structs)
    for (std::unique_ptr<MemberFnNode> &m : s->methods)
      m->accept(*this);
  for (std::unique_ptr<FnNode> &fn : node.functions)
    fn->accept(*this);
  mark_mutating(node);
  for (const PendingCall &p : pending)
    check_mutation(*p.call, p.target, p.blocker);
  mutation_known = true;
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
  std::map<std::string, const MemberFnNode *> methods;
  for (std::unique_ptr<MemberFnNode> &method : node.methods) {
    if (seen.count(method->name))
      error(method->line, method->col,
            "member function '" + method->name +
                "' has the same name as a field of struct '" + node.name +
                "'");
    if (!methods.emplace(method->name, method.get()).second)
      error(method->line, method->col,
            "member function '" + method->name +
                "' is already declared in struct '" + node.name + "'");
  }
  structs[node.name] = &node;
}

void SemanticChecker::declare_fn(FnNode &node) {
  if (functions.count(node.name))
    error(node.line, node.col,
          "function '" + node.name + "' is already declared");
  declare_params(node);
  functions[node.name] = &node;
}

void SemanticChecker::declare_params(FnNode &node) {
  std::map<std::string, const ParamNode *> seen;
  for (std::unique_ptr<ParamNode> &param : node.params) {
    param->struct_type =
        resolve_type(param->type_name, param->line, param->type_col);
    if (!seen.emplace(param->name, param.get()).second)
      error(param->line, param->col,
            "parameter '" + param->name + "' is already declared");
  }
  resolve_type(node.result_type, node.line, node.result_col);
}

void SemanticChecker::visit_fn(FnNode &node) { check_body(node); }

void SemanticChecker::visit_member_fn(MemberFnNode &node) {
  current_member = &node;
  check_body(node);
  current_member = nullptr;
}

void SemanticChecker::check_body(FnNode &node) {
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
    if (!as_init(*init.values[0]))
      check(init.values[0]);
    reject_lambda(*init.values[0], "initialise '" + node.name + "'",
                  "variable");
    check_value(*init.values[0], node.type_name, nullptr, node,
                "'" + node.name + "'");
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
  for (std::unique_ptr<ExprNode> &value : node.values) {
    if (as_init(*value))
      continue;
    check(value);
    if (!is_lambda(node.type))
      reject_lambda(*value, "initialise a field of " + node.type, "field");
  }
  node.copy = node.values.size() == 1 && node.values[0]->type == node.type;
  if (node.copy)
    return;
  check_count(node, fields.size());
  for (size_t i = 0; i < fields.size(); i++)
    check_value(*node.values[i], fields[i]->type_name, fields[i]->struct_type,
                *node.values[i], "field '" + fields[i]->name + "'");
}

void SemanticChecker::visit_assign(AssignNode &node) {
  std::string target = chain(node.name, node.links);
  std::string object = node.links.empty() ? "it" : "'" + node.name + "'";
  const DeclNode &decl = root(node.name, node.links, node);
  std::string lambda_target = "cannot assign to '" + target +
                              "': it is a lambda, and a lambda cannot be "
                              "assigned";
  if (node.links.empty() && is_lambda(decl.type_name))
    error(node.line, node.col, lambda_target);
  if (dynamic_cast<const ParamNode *>(&decl))
    error(node.line, node.col,
          "cannot assign to '" + target + "': " + object + " is a parameter");
  if (!decl.mut)
    error(node.line, node.col,
          "cannot assign to '" + target + "': " + object + " is not mut");
  if (current_lambda && is_self(decl)) {
    const std::string &capture = node.links.front().name;
    if (!field_named(*current_member->owner, capture)->mut)
      error(node.line, node.col,
            "cannot assign to '" + target + "': the capture '" + capture +
                "' is const, write [mut " + capture + "]");
  }
  const FieldNode *field = resolve_links(decl, node.links, node.line, true);
  if (is_self(decl))
    current_member->mutating = true;
  node.decl = &decl;
  node.own_field = is_self(decl);
  node.type = field ? field->type_name : decl.type_name;
  if (is_lambda(node.type))
    error(node.line, node.col, lambda_target);
  const StructNode *replaced = field ? field->struct_type : decl.struct_type;
  if (replaced) {
    std::string inner = const_field(*replaced);
    if (!inner.empty())
      error(node.line, node.col,
            "cannot assign to '" + target + "': its field '" + inner +
                "' is not mut");
  }
  check(node.value);
  reject_lambda(*node.value, "be assigned", "");
  check_assignable(*node.value, node.type, node, "assign to '" + target + "'");
}

void SemanticChecker::visit_exit(ExitNode &node) {
  const std::string &type = check(node.value);
  reject_lambda(*node.value, "be the value of exit", "");
  if (current_lambda)
    check_assignable(*node.value, current_fn->result_type, *node.value,
                     "exit lambda '" + current_lambda->name + "'");
  else if (current_fn)
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
  const std::string &type = check(node.cond);
  if (type != "bool")
    error(node.line, node.col,
          "the condition of 'if' must be bool, got " + type);
  node.then_block->accept(*this);
  if (node.else_block)
    node.else_block->accept(*this);
}

void SemanticChecker::visit_while(WhileNode &node) {
  const std::string &type = check(node.cond);
  if (type != "bool")
    error(node.line, node.col,
          "the condition of 'while' must be bool, got " + type);
  node.body->accept(*this);
}

void SemanticChecker::visit_not(NotNode &node) {
  const std::string &type = check(node.operand);
  if (type != "bool")
    error(node.line, node.col, "cannot apply '!' to " + type);
  node.type = "bool";
}

void SemanticChecker::visit_binop(BinOpNode &node) {
  const std::string &lt = check(node.left);
  const std::string &rt = check(node.right);
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
  node.decl = &root(node.name, node.links, node);
  node.own_field = is_self(*node.decl);
  const FieldNode *field =
      resolve_links(*node.decl, node.links, node.line, false);
  node.type = field ? field->type_name : node.decl->type_name;
}

void SemanticChecker::check_args(std::vector<std::unique_ptr<ExprNode>> &args,
                                 const FnNode &fn, const std::string &name,
                                 const Node &at) {
  const std::vector<std::unique_ptr<ParamNode>> &params = fn.params;
  if (args.size() != params.size())
    error(at.line, at.col,
          "wrong number of arguments for '" + name + "': expected " +
              std::to_string(params.size()) + ", got " +
              std::to_string(args.size()));
  for (size_t i = 0; i < params.size(); i++) {
    check(args[i]);
    reject_lambda(*args[i], "be passed to '" + name + "'", "parameter");
    check_assignable(*args[i], params[i]->type_name, *args[i],
                     "initialise parameter '" + params[i]->name + "'");
  }
}

void SemanticChecker::visit_call(CallNode &node) {
  const MemberFnNode *own = current_member && !current_lambda
                                ? method(*current_member->owner, node.name)
                                : nullptr;
  bool variable = find(node.name) || is_own_field(node.name);
  if (variable && (own || functions.count(node.name)))
    error(node.line, node.col,
          "'" + node.name +
              "' is both a variable and a function: rename one of them");
  if (variable) {
    std::unique_ptr<MemberCallNode> call = std::make_unique<MemberCallNode>(
        node.line, node.col,
        std::make_unique<VarNode>(node.line, node.col, node.name,
                                  std::vector<Link>{}),
        node.col, "call", std::move(node.args));
    call->callee = node.name;
    call->accept(*this);
    replacement = std::move(call);
    return;
  }
  if (own) {
    current_member->self_calls.push_back(own);
    node.fn = own;
  } else {
    auto it = functions.find(node.name);
    if (it == functions.end() && current_lambda &&
        node.name == current_lambda->name)
      error(node.line, node.col,
            "lambda '" + node.name +
                "' cannot call itself: its name is declared after its body");
    if (it == functions.end() && current_lambda && visible_outside(node.name))
      not_captured(node.name, node);
    if (it == functions.end())
      error(node.line, node.col,
            "function '" + node.name + "' is not declared");
    node.fn = it->second;
  }
  check_args(node.args, *node.fn, node.name, node);
  node.type = node.fn->result_type;
}

void SemanticChecker::visit_member_call(MemberCallNode &node) {
  VarNode *object = dynamic_cast<VarNode *>(node.object.get());
  std::string target = show(*node.object);
  if (object && object->links.empty() && !find(object->name) &&
      !is_own_field(object->name) && structs.count(object->name))
    error(object->line, object->col,
          "cannot call '" + node.name + "' on type '" + object->name +
              "': a member function is called on an object");
  std::string type = check(node.object);
  auto owner = structs.find(type);
  if (node.callee.empty() && owner != structs.end()) {
    const std::vector<std::unique_ptr<FieldNode>> &fields =
        owner->second->fields;
    for (unsigned i = 0; i < fields.size(); i++) {
      if (fields[i]->name != node.name)
        continue;
      if (is_lambda(type))
        error(node.line, node.col,
              "'" + target +
                  "' is a lambda: its captures are not visible outside its "
                  "body");
      Link link{node.name, node.col, i};
      if (object)
        object->links.push_back(link);
      else
        node.object = std::make_unique<FieldOfNode>(
            node.line, node.dot_col, std::move(node.object),
            std::vector<Link>{link});
      node.object->type = type = fields[i]->type_name;
      node.callee = target = target + "." + node.name;
      node.name = "call";
      owner = structs.find(type);
      break;
    }
  }
  if (owner == structs.end()) {
    if (!node.callee.empty())
      error(node.line, node.col,
            "'" + target + "' is not callable: it is " + type);
    error(node.line, object ? node.col : node.dot_col,
          "cannot call '" + node.name + "' on '" + target + "' of type " +
              type);
  }
  node.fn = method(*owner->second, node.name);
  if (!node.fn && !node.callee.empty())
    error(node.line, node.col,
          "'" + target + "' is not callable: " + type +
              " has no member function 'call'");
  if (!node.fn)
    error(node.line, node.col,
          "struct " + type + " has no member function '" + node.name + "'");
  check_args(node.args, *node.fn, node.callee.empty() ? node.name : target,
             node);
  node.type = node.fn->result_type;
  if (!object)
    return;
  if (is_self(*object->decl))
    current_member->self_calls.push_back(node.fn);
  std::string blocker = const_link(*object);
  if (blocker.empty())
    return;
  if (mutation_known)
    check_mutation(node, target, blocker);
  else
    pending.push_back({&node, target, blocker});
}

void SemanticChecker::visit_field_of(FieldOfNode &node) {
  std::string target = show(*node.object);
  const std::string &type = check(node.object);
  auto owner = structs.find(type);
  if (owner == structs.end())
    error(node.line, node.col,
          "'" + target + "' of type " + type + " has no fields");
  node.type = walk_links(target, type, owner->second, node.links, node.line,
                         nullptr)
                  ->type_name;
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

void SemanticChecker::visit_read(ReadNode &node) { node.type = "i32"; }

void SemanticChecker::visit_print(PrintNode &node) {
  if (!node.value)
    return;
  const std::string &type = check(node.value);
  reject_lambda(*node.value, "be printed", "");
  if (!is_builtin(type))
    node.struct_type = structs.at(type);
}

void SemanticChecker::visit_lambda(LambdaNode &node) {
  std::set<std::string> seen;
  std::vector<std::unique_ptr<FieldNode>> fields;
  std::vector<std::unique_ptr<ExprNode>> values;
  for (const Capture &capture : node.captures) {
    if (!seen.insert(capture.name).second)
      error(capture.line, capture.col,
            "'" + capture.name + "' is captured twice");
    const DeclNode *decl = find(capture.name);
    const FieldNode *field =
        decl || !current_member
            ? nullptr
            : field_named(*current_member->owner, capture.name);
    if (!decl && !field)
      error(capture.line, capture.col,
            "cannot capture '" + capture.name + "': no variable '" +
                capture.name + "' is visible here");
    fields.push_back(std::make_unique<FieldNode>(
        capture.line, capture.col, decl ? decl->type_name : field->type_name,
        capture.col, capture.name, capture.mut));
    fields.back()->struct_type = decl ? decl->struct_type : field->struct_type;
    values.push_back(std::make_unique<VarNode>(
        capture.line, capture.col, capture.name, std::vector<Link>{}));
  }
  for (const std::unique_ptr<ParamNode> &param : node.call->params)
    if (seen.count(param->name))
      error(param->line, param->col,
            "parameter '" + param->name +
                "' has the same name as a capture of lambda '" + node.name +
                "'");

  std::string base = "lambda." + node.name, type = base;
  for (int i = 2; structs.count(type); i++)
    type = base + "." + std::to_string(i);
  node.call->name = "call";
  MemberFnNode &call = *node.call;
  std::vector<std::unique_ptr<MemberFnNode>> methods;
  methods.push_back(std::move(node.call));
  node.closure = std::make_unique<StructNode>(
      node.line, node.col, type, std::move(fields), std::move(methods));
  structs[type] = node.closure.get();
  declare_params(call);
  lambdas.push_back(&call);

  size_t outer_barrier = barrier;
  const FnNode *outer_fn = current_fn;
  MemberFnNode *outer_member = current_member;
  const MemberFnNode *outer_enclosing = enclosing;
  const LambdaNode *outer_lambda = current_lambda;
  barrier = scopes.size();
  enclosing = current_member;
  current_lambda = &node;
  call.accept(*this);
  barrier = outer_barrier;
  current_fn = outer_fn;
  current_member = outer_member;
  enclosing = outer_enclosing;
  current_lambda = outer_lambda;
  for (const MemberFnNode *callee : call.self_calls)
    if (callee->mutating)
      call.mutating = true;

  bool mut = false;
  for (const Capture &capture : node.captures)
    mut = mut || capture.mut;
  node.var = std::make_unique<DeclNode>(
      node.line, node.col, type, node.col, node.name, mut,
      std::make_unique<InitNode>(node.line, node.col, std::move(values)));
  node.var->accept(*this);
}
