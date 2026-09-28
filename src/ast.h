#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class ProgramNode;
class DeclNode;
class AssignNode;
class ExitNode;
class BinOpNode;
class VarNode;
class ConstNode;
class BoolNode;

class Visitor {
public:
  virtual ~Visitor() = default;
  virtual void visit_program(ProgramNode &node) = 0;
  virtual void visit_decl(DeclNode &node) = 0;
  virtual void visit_assign(AssignNode &node) = 0;
  virtual void visit_exit(ExitNode &node) = 0;
  virtual void visit_binop(BinOpNode &node) = 0;
  virtual void visit_var(VarNode &node) = 0;
  virtual void visit_const(ConstNode &node) = 0;
  virtual void visit_bool(BoolNode &node) = 0;
};

class Node {
public:
  uint64_t line, col;
  Node(uint64_t line, uint64_t col) : line(line), col(col) {}
  virtual ~Node() = default;
  virtual void accept(Visitor &visitor) = 0;
  virtual std::string label() const = 0;
  virtual std::vector<const Node *> children() const { return {}; }

  void dump(int depth = 0) const {
    std::cout << std::string(depth * 2, ' ') << label() << "\n";
    for (const Node *child : children())
      child->dump(depth + 1);
  }
};

class ExprNode : public Node {
public:
  std::string type;
  using Node::Node;
};

class BinOpNode : public ExprNode {
public:
  std::string op;
  std::unique_ptr<ExprNode> left, right;
  BinOpNode(uint64_t line, uint64_t col, std::string op,
            std::unique_ptr<ExprNode> left, std::unique_ptr<ExprNode> right)
      : ExprNode(line, col), op(std::move(op)), left(std::move(left)),
        right(std::move(right)) {}
  void accept(Visitor &visitor) override { visitor.visit_binop(*this); }
  std::string label() const override { return "BinOp " + op; }
  std::vector<const Node *> children() const override {
    return {left.get(), right.get()};
  }
};

class VarNode : public ExprNode {
public:
  std::string name;
  const DeclNode *decl = nullptr;
  VarNode(uint64_t line, uint64_t col, std::string name)
      : ExprNode(line, col), name(std::move(name)) {}
  void accept(Visitor &visitor) override { visitor.visit_var(*this); }
  std::string label() const override { return "Var " + name; }
};

class ConstNode : public ExprNode {
public:
  std::string text;
  int64_t value = 0;
  ConstNode(uint64_t line, uint64_t col, std::string text)
      : ExprNode(line, col), text(std::move(text)) {}
  void accept(Visitor &visitor) override { visitor.visit_const(*this); }
  std::string label() const override { return "Const " + text; }
};

class BoolNode : public ExprNode {
public:
  bool value;
  BoolNode(uint64_t line, uint64_t col, bool value)
      : ExprNode(line, col), value(value) {}
  void accept(Visitor &visitor) override { visitor.visit_bool(*this); }
  std::string label() const override {
    return std::string("Bool ") + (value ? "true" : "false");
  }
};

class StmtNode : public Node {
public:
  using Node::Node;
};

class DeclNode : public StmtNode {
public:
  std::string type_name;
  std::string name;
  bool mut;
  std::unique_ptr<ExprNode> init;
  DeclNode(uint64_t line, uint64_t col, std::string type_name,
           std::string name, bool mut, std::unique_ptr<ExprNode> init)
      : StmtNode(line, col), type_name(std::move(type_name)),
        name(std::move(name)), mut(mut), init(std::move(init)) {}
  void accept(Visitor &visitor) override { visitor.visit_decl(*this); }
  std::string label() const override {
    return "Decl " + name + " " + type_name + (mut ? " mut" : " const");
  }
  std::vector<const Node *> children() const override { return {init.get()}; }
};

class AssignNode : public StmtNode {
public:
  std::string name;
  std::unique_ptr<ExprNode> value;
  const DeclNode *decl = nullptr;
  AssignNode(uint64_t line, uint64_t col, std::string name,
             std::unique_ptr<ExprNode> value)
      : StmtNode(line, col), name(std::move(name)), value(std::move(value)) {}
  void accept(Visitor &visitor) override { visitor.visit_assign(*this); }
  std::string label() const override { return "Assign " + name; }
  std::vector<const Node *> children() const override {
    return {value.get()};
  }
};

class ExitNode : public Node {
public:
  std::unique_ptr<ExprNode> value;
  ExitNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> value)
      : Node(line, col), value(std::move(value)) {}
  void accept(Visitor &visitor) override { visitor.visit_exit(*this); }
  std::string label() const override { return "Exit"; }
  std::vector<const Node *> children() const override {
    return {value.get()};
  }
};

class ProgramNode : public Node {
public:
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit;
  ProgramNode(uint64_t line, uint64_t col,
              std::vector<std::unique_ptr<StmtNode>> statements,
              std::unique_ptr<ExitNode> exit)
      : Node(line, col), statements(std::move(statements)),
        exit(std::move(exit)) {}
  void accept(Visitor &visitor) override { visitor.visit_program(*this); }
  std::string label() const override { return "Program"; }
  std::vector<const Node *> children() const override {
    std::vector<const Node *> c;
    for (const std::unique_ptr<StmtNode> &s : statements)
      c.push_back(s.get());
    c.push_back(exit.get());
    return c;
  }
};
