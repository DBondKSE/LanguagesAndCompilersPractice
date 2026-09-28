#pragma once

#include "ast.h"

#include <map>
#include <string>

class SemanticChecker : public Visitor {
  std::map<std::string, const DeclNode *> symbols;

  const std::string &check(ExprNode &node);
  const DeclNode &lookup(const std::string &name, const Node &at) const;
  void check_assignable(const ExprNode &expr, const std::string &want,
                        const Node &at, const std::string &what) const;

public:
  void visit_program(ProgramNode &node) override;
  void visit_decl(DeclNode &node) override;
  void visit_assign(AssignNode &node) override;
  void visit_exit(ExitNode &node) override;
  void visit_binop(BinOpNode &node) override;
  void visit_var(VarNode &node) override;
  void visit_const(ConstNode &node) override;
  void visit_bool(BoolNode &node) override;
};
