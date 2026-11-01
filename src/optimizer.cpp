#include "optimizer.h"

#include <map>
#include <set>

class EffectFinder {
  struct FnInfo {
    bool effect = false;
    std::set<const FnNode *> callees;
  };
  std::map<const FnNode *, FnInfo> fns;
  FnInfo *current = nullptr;

  void enter(const FnNode &fn) {
    FnInfo *outer = current;
    current = &fns[&fn];
    if (const MemberFnNode *m = dynamic_cast<const MemberFnNode *>(&fn))
      current->effect = m->mutating;
    scan(*fn.body);
    current = outer;
  }

  void scan(const Node &node) {
    if (const FnNode *fn = dynamic_cast<const FnNode *>(&node)) {
      enter(*fn);
      return;
    }
    if (const LambdaNode *lambda = dynamic_cast<const LambdaNode *>(&node)) {
      enter(*lambda->closure->methods[0]);
      return;
    }
    if (current) {
      if (dynamic_cast<const PrintNode *>(&node) ||
          dynamic_cast<const ReadNode *>(&node) ||
          dynamic_cast<const WhileNode *>(&node))
        current->effect = true;
      if (const CallNode *call = dynamic_cast<const CallNode *>(&node))
        current->callees.insert(call->fn);
      if (const MemberCallNode *call =
              dynamic_cast<const MemberCallNode *>(&node)) {
        current->callees.insert(call->fn);
        scan(*call->object);
      }
    }
    for (const Node *child : node.children())
      scan(*child);
  }

  bool reaches(const FnNode *from, const FnNode *target,
               std::set<const FnNode *> &seen) const {
    auto it = fns.find(from);
    if (it == fns.end())
      return false;
    for (const FnNode *callee : it->second.callees) {
      if (callee == target)
        return true;
      if (seen.insert(callee).second && reaches(callee, target, seen))
        return true;
    }
    return false;
  }

public:
  std::set<const FnNode *> find(const ProgramNode &program) {
    scan(program);
    std::set<const FnNode *> effects;
    for (const auto &[fn, info] : fns) {
      std::set<const FnNode *> seen;
      if (info.effect || reaches(fn, fn, seen))
        effects.insert(fn);
    }
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto &[fn, info] : fns)
        for (const FnNode *callee : info.callees)
          if (!effects.count(fn) && effects.count(callee)) {
            effects.insert(fn);
            changed = true;
          }
    }
    return effects;
  }
};

class UseCounter : public Visitor {
  const std::set<const FnNode *> &effects;

  bool has_effect(const Node &node) const {
    if (dynamic_cast<const ReadNode *>(&node))
      return true;
    if (const CallNode *call = dynamic_cast<const CallNode *>(&node))
      if (effects.count(call->fn))
        return true;
    if (const MemberCallNode *call =
            dynamic_cast<const MemberCallNode *>(&node))
      if (effects.count(call->fn) || has_effect(*call->object))
        return true;
    for (const Node *child : node.children())
      if (has_effect(*child))
        return true;
    return false;
  }

  void read(const DeclNode *decl) { reads[decl]++; }

  void visit_args(std::vector<std::unique_ptr<ExprNode>> &args) {
    for (std::unique_ptr<ExprNode> &arg : args)
      arg->accept(*this);
  }

public:
  std::map<const DeclNode *, int> reads;
  std::map<const FnNode *, int> calls;

  explicit UseCounter(const std::set<const FnNode *> &effects)
      : effects(effects) {}

  void visit_program(ProgramNode &node) override {
    for (std::unique_ptr<StructNode> &s : node.structs)
      s->accept(*this);
    for (std::unique_ptr<FnNode> &fn : node.functions)
      fn->accept(*this);
    for (std::unique_ptr<StmtNode> &stmt : node.statements)
      stmt->accept(*this);
    node.exit->accept(*this);
  }

  void visit_struct(StructNode &node) override {
    for (std::unique_ptr<MemberFnNode> &m : node.methods)
      m->accept(*this);
  }

  void visit_fn(FnNode &node) override {
    calls.emplace(&node, 0);
    node.body->accept(*this);
  }

  void visit_member_fn(MemberFnNode &node) override {
    node.body->accept(*this);
  }

  void visit_decl(DeclNode &node) override {
    reads.emplace(&node, 0);
    if (has_effect(*node.init))
      read(&node);
    node.init->accept(*this);
  }

  void visit_assign(AssignNode &node) override {
    if (has_effect(*node.value))
      read(node.decl);
    node.value->accept(*this);
  }

  void visit_exit(ExitNode &node) override { node.value->accept(*this); }

  void visit_block(BlockNode &node) override {
    for (std::unique_ptr<StmtNode> &stmt : node.statements)
      stmt->accept(*this);
    if (node.exit)
      node.exit->accept(*this);
  }

  void visit_if(IfNode &node) override {
    node.cond->accept(*this);
    node.then_block->accept(*this);
    if (node.else_block)
      node.else_block->accept(*this);
  }

  void visit_while(WhileNode &node) override {
    node.cond->accept(*this);
    node.body->accept(*this);
  }

  void visit_binop(BinOpNode &node) override {
    node.left->accept(*this);
    node.right->accept(*this);
  }

  void visit_not(NotNode &node) override { node.operand->accept(*this); }

  void visit_var(VarNode &node) override { read(node.decl); }

  void visit_call(CallNode &node) override {
    calls[node.fn]++;
    visit_args(node.args);
  }

  void visit_member_call(MemberCallNode &node) override {
    node.object->accept(*this);
    visit_args(node.args);
  }

  void visit_field_of(FieldOfNode &node) override {
    node.object->accept(*this);
  }

  void visit_init(InitNode &node) override { visit_args(node.values); }

  void visit_read(ReadNode &) override {}

  void visit_print(PrintNode &node) override {
    if (node.value)
      node.value->accept(*this);
  }

  void visit_const(ConstNode &) override {}

  void visit_bool(BoolNode &) override {}

  void visit_lambda(LambdaNode &node) override {
    node.closure->accept(*this);
    node.var->init->accept(*this);
  }
};

class Pruner {
  const std::set<const DeclNode *> &dead;
  const std::set<const FnNode *> &dead_fns;

  bool removed(const StmtNode &stmt) const {
    if (const AssignNode *assign = dynamic_cast<const AssignNode *>(&stmt))
      return dead.count(assign->decl);
    const DeclNode *decl = dynamic_cast<const DeclNode *>(&stmt);
    return decl && dead.count(decl);
  }

  void prune(std::vector<std::unique_ptr<StmtNode>> &statements) {
    std::vector<std::unique_ptr<StmtNode>> kept;
    for (std::unique_ptr<StmtNode> &stmt : statements) {
      if (removed(*stmt))
        continue;
      prune_inside(*stmt);
      kept.push_back(std::move(stmt));
    }
    statements = std::move(kept);
  }

  void prune_inside(StmtNode &stmt) {
    if (IfNode *node = dynamic_cast<IfNode *>(&stmt)) {
      prune(node->then_block->statements);
      if (node->else_block)
        prune(node->else_block->statements);
    } else if (WhileNode *node = dynamic_cast<WhileNode *>(&stmt)) {
      prune(node->body->statements);
    } else if (LambdaNode *node = dynamic_cast<LambdaNode *>(&stmt)) {
      prune(node->closure->methods[0]->body->statements);
    }
  }

public:
  Pruner(const std::set<const DeclNode *> &dead,
         const std::set<const FnNode *> &dead_fns)
      : dead(dead), dead_fns(dead_fns) {}

  void prune(ProgramNode &program) {
    for (std::unique_ptr<StructNode> &s : program.structs)
      for (std::unique_ptr<MemberFnNode> &m : s->methods)
        prune(m->body->statements);
    std::vector<std::unique_ptr<FnNode>> kept;
    for (std::unique_ptr<FnNode> &fn : program.functions) {
      if (dead_fns.count(fn.get()))
        continue;
      prune(fn->body->statements);
      kept.push_back(std::move(fn));
    }
    program.functions = std::move(kept);
    prune(program.statements);
  }
};

template <class Key>
static std::set<const Key *> unused(const std::map<const Key *, int> &counts) {
  std::set<const Key *> dead;
  for (const auto &[key, count] : counts)
    if (count == 0)
      dead.insert(key);
  return dead;
}

void remove_unused(ProgramNode &program) {
  std::set<const FnNode *> effects = EffectFinder().find(program);
  while (true) {
    UseCounter counter(effects);
    program.accept(counter);
    std::set<const DeclNode *> dead = unused(counter.reads);
    std::set<const FnNode *> dead_fns = unused(counter.calls);
    if (dead.empty() && dead_fns.empty())
      return;
    Pruner(dead, dead_fns).prune(program);
  }
}
