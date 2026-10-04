#pragma once

#include "ast.h"

#include <map>
#include <string>
#include <vector>

class SemanticChecker : public Visitor {
  std::vector<std::map<std::string, const DeclNode *>> scopes;

  const std::string &check(ExprNode &node);
  const DeclNode &lookup(const std::string &name, const Node &at) const;
  void check_assignable(const ExprNode &expr, const std::string &want,
                        const Node &at, const std::string &what) const;

public:
  void visit_program(ProgramNode &node) override;
  void visit_decl(DeclNode &node) override;
  void visit_assign(AssignNode &node) override;
  void visit_exit(ExitNode &node) override;
  void visit_block(BlockNode &node) override;
  void visit_if(IfNode &node) override;
  void visit_while(WhileNode &node) override;
  void visit_binop(BinOpNode &node) override;
  void visit_not(NotNode &node) override;
  void visit_var(VarNode &node) override;
  void visit_const(ConstNode &node) override;
  void visit_bool(BoolNode &node) override;
};
