#pragma once

#include "ast.h"

#include <map>
#include <string>
#include <vector>

class SemanticChecker : public Visitor {
  std::map<std::string, const StructNode *> structs;
  std::map<std::string, const FnNode *> functions;
  const FnNode *current_fn = nullptr;
  MemberFnNode *current_member = nullptr;
  std::vector<std::map<std::string, const DeclNode *>> scopes;
  struct PendingCall {
    const MemberCallNode *call;
    std::string target, blocker;
  };
  std::vector<PendingCall> pending;
  bool mutation_known = false;

  const std::string &check(ExprNode &node);
  const DeclNode *find(const std::string &name) const;
  const DeclNode &lookup(const std::string &name, const Node &at) const;
  bool is_self(const DeclNode &decl) const;
  bool is_own_field(const std::string &name) const;
  const DeclNode &root(const std::string &name, std::vector<Link> &links,
                       const Node &at) const;
  std::string display(const DeclNode &decl,
                      const std::vector<Link> &links) const;
  std::string const_link(const VarNode &object) const;
  void check_mutation(const MemberCallNode &call, const std::string &target,
                      const std::string &blocker) const;
  void mark_mutating(ProgramNode &program);
  void declare_params(FnNode &node);
  void check_body(FnNode &node);
  void check_args(std::vector<std::unique_ptr<ExprNode>> &args,
                  const FnNode &fn, const std::string &name, const Node &at);
  const StructNode *resolve_type(const std::string &type, uint64_t line,
                                 uint64_t col) const;
  const FieldNode *walk_links(std::string path, const std::string &type_name,
                              const StructNode *owner,
                              std::vector<Link> &links, uint64_t line,
                              const DeclNode *written) const;
  const FieldNode *resolve_links(const DeclNode &decl,
                                 std::vector<Link> &links, uint64_t line,
                                 bool write) const;
  void check_value(ExprNode &value, const std::string &type,
                   const StructNode *struct_type, const Node &at,
                   const std::string &target);
  void declare_fn(FnNode &node);
  void check_assignable(const ExprNode &expr, const std::string &want,
                        const Node &at, const std::string &what) const;

public:
  void visit_program(ProgramNode &node) override;
  void visit_struct(StructNode &node) override;
  void visit_fn(FnNode &node) override;
  void visit_member_fn(MemberFnNode &node) override;
  void visit_decl(DeclNode &node) override;
  void visit_assign(AssignNode &node) override;
  void visit_exit(ExitNode &node) override;
  void visit_block(BlockNode &node) override;
  void visit_if(IfNode &node) override;
  void visit_while(WhileNode &node) override;
  void visit_binop(BinOpNode &node) override;
  void visit_not(NotNode &node) override;
  void visit_var(VarNode &node) override;
  void visit_call(CallNode &node) override;
  void visit_member_call(MemberCallNode &node) override;
  void visit_field_of(FieldOfNode &node) override;
  void visit_init(InitNode &node) override;
  void visit_read(ReadNode &node) override;
  void visit_print(PrintNode &node) override;
  void visit_const(ConstNode &node) override;
  void visit_bool(BoolNode &node) override;
};
