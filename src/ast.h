#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class ProgramNode;
class StructNode;
class FnNode;
class DeclNode;
class AssignNode;
class ExitNode;
class BlockNode;
class IfNode;
class WhileNode;
class BinOpNode;
class NotNode;
class VarNode;
class CallNode;
class InitNode;
class ConstNode;
class BoolNode;

class Visitor {
public:
  virtual ~Visitor() = default;
  virtual void visit_program(ProgramNode &node) = 0;
  virtual void visit_struct(StructNode &node) = 0;
  virtual void visit_fn(FnNode &node) = 0;
  virtual void visit_decl(DeclNode &node) = 0;
  virtual void visit_assign(AssignNode &node) = 0;
  virtual void visit_exit(ExitNode &node) = 0;
  virtual void visit_block(BlockNode &node) = 0;
  virtual void visit_if(IfNode &node) = 0;
  virtual void visit_while(WhileNode &node) = 0;
  virtual void visit_binop(BinOpNode &node) = 0;
  virtual void visit_not(NotNode &node) = 0;
  virtual void visit_var(VarNode &node) = 0;
  virtual void visit_call(CallNode &node) = 0;
  virtual void visit_init(InitNode &node) = 0;
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

class NotNode : public ExprNode {
public:
  std::unique_ptr<ExprNode> operand;
  NotNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> operand)
      : ExprNode(line, col), operand(std::move(operand)) {}
  void accept(Visitor &visitor) override { visitor.visit_not(*this); }
  std::string label() const override { return "Not"; }
  std::vector<const Node *> children() const override {
    return {operand.get()};
  }
};

struct Link {
  std::string name;
  uint64_t col;
  unsigned index = 0;
};

inline std::string chain(std::string name, const std::vector<Link> &links) {
  for (const Link &link : links)
    name += "." + link.name;
  return name;
}

class VarNode : public ExprNode {
public:
  std::string name;
  std::vector<Link> links;
  const DeclNode *decl = nullptr;
  VarNode(uint64_t line, uint64_t col, std::string name,
          std::vector<Link> links)
      : ExprNode(line, col), name(std::move(name)), links(std::move(links)) {}
  void accept(Visitor &visitor) override { visitor.visit_var(*this); }
  std::string label() const override { return "Var " + chain(name, links); }
};

class CallNode : public ExprNode {
public:
  std::string name;
  std::vector<std::unique_ptr<ExprNode>> args;
  const FnNode *fn = nullptr;
  CallNode(uint64_t line, uint64_t col, std::string name,
           std::vector<std::unique_ptr<ExprNode>> args)
      : ExprNode(line, col), name(std::move(name)), args(std::move(args)) {}
  void accept(Visitor &visitor) override { visitor.visit_call(*this); }
  std::string label() const override { return "Call " + name; }
  std::vector<const Node *> children() const override {
    std::vector<const Node *> c;
    for (const std::unique_ptr<ExprNode> &a : args)
      c.push_back(a.get());
    return c;
  }
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

class InitNode : public ExprNode {
public:
  std::vector<std::unique_ptr<ExprNode>> values;
  const StructNode *struct_type = nullptr;
  bool copy = false;
  InitNode(uint64_t line, uint64_t col,
           std::vector<std::unique_ptr<ExprNode>> values)
      : ExprNode(line, col), values(std::move(values)) {}
  void accept(Visitor &visitor) override { visitor.visit_init(*this); }
  std::string label() const override { return "Init"; }
  std::vector<const Node *> children() const override {
    std::vector<const Node *> c;
    for (const std::unique_ptr<ExprNode> &v : values)
      c.push_back(v.get());
    return c;
  }
};

class StmtNode : public Node {
public:
  using Node::Node;
};

class FieldNode : public Node {
public:
  std::string type_name;
  uint64_t type_col;
  std::string name;
  bool mut;
  const StructNode *struct_type = nullptr;
  FieldNode(uint64_t line, uint64_t col, std::string type_name,
            uint64_t type_col, std::string name, bool mut)
      : Node(line, col), type_name(std::move(type_name)), type_col(type_col),
        name(std::move(name)), mut(mut) {}
  void accept(Visitor &) override {}
  std::string label() const override {
    return "Field " + name + " " + type_name + (mut ? " mut" : " const");
  }
};

class StructNode : public Node {
public:
  std::string name;
  std::vector<std::unique_ptr<FieldNode>> fields;
  StructNode(uint64_t line, uint64_t col, std::string name,
             std::vector<std::unique_ptr<FieldNode>> fields)
      : Node(line, col), name(std::move(name)), fields(std::move(fields)) {}
  void accept(Visitor &visitor) override { visitor.visit_struct(*this); }
  std::string label() const override { return "Struct " + name; }
  std::vector<const Node *> children() const override {
    std::vector<const Node *> c;
    for (const std::unique_ptr<FieldNode> &f : fields)
      c.push_back(f.get());
    return c;
  }
};

class DeclNode : public StmtNode {
public:
  std::string type_name;
  uint64_t type_col;
  std::string name;
  bool mut;
  std::unique_ptr<InitNode> init;
  const StructNode *struct_type = nullptr;
  DeclNode(uint64_t line, uint64_t col, std::string type_name,
           uint64_t type_col, std::string name, bool mut,
           std::unique_ptr<InitNode> init)
      : StmtNode(line, col), type_name(std::move(type_name)),
        type_col(type_col), name(std::move(name)), mut(mut),
        init(std::move(init)) {}
  void accept(Visitor &visitor) override { visitor.visit_decl(*this); }
  std::string label() const override {
    return "Decl " + name + " " + type_name + (mut ? " mut" : " const");
  }
  std::vector<const Node *> children() const override {
    return init->children();
  }
};

class ParamNode : public DeclNode {
public:
  ParamNode(uint64_t line, uint64_t col, std::string type_name,
            uint64_t type_col, std::string name)
      : DeclNode(line, col, std::move(type_name), type_col, std::move(name),
                 false, nullptr) {}
  void accept(Visitor &) override {}
  std::string label() const override {
    return "Param " + name + " " + type_name;
  }
  std::vector<const Node *> children() const override { return {}; }
};

class AssignNode : public StmtNode {
public:
  std::string name;
  std::vector<Link> links;
  std::unique_ptr<ExprNode> value;
  const DeclNode *decl = nullptr;
  std::string type;
  AssignNode(uint64_t line, uint64_t col, std::string name,
             std::vector<Link> links, std::unique_ptr<ExprNode> value)
      : StmtNode(line, col), name(std::move(name)), links(std::move(links)),
        value(std::move(value)) {}
  void accept(Visitor &visitor) override { visitor.visit_assign(*this); }
  std::string label() const override {
    return "Assign " + chain(name, links);
  }
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

class BlockNode : public Node {
public:
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit;
  BlockNode(uint64_t line, uint64_t col,
            std::vector<std::unique_ptr<StmtNode>> statements,
            std::unique_ptr<ExitNode> exit)
      : Node(line, col), statements(std::move(statements)),
        exit(std::move(exit)) {}
  void accept(Visitor &visitor) override { visitor.visit_block(*this); }
  std::string label() const override { return "Block"; }
  std::vector<const Node *> children() const override {
    std::vector<const Node *> c;
    for (const std::unique_ptr<StmtNode> &s : statements)
      c.push_back(s.get());
    if (exit)
      c.push_back(exit.get());
    return c;
  }
};

class IfNode : public StmtNode {
public:
  std::unique_ptr<ExprNode> cond;
  std::unique_ptr<BlockNode> then_block, else_block;
  IfNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> cond,
         std::unique_ptr<BlockNode> then_block,
         std::unique_ptr<BlockNode> else_block)
      : StmtNode(line, col), cond(std::move(cond)),
        then_block(std::move(then_block)), else_block(std::move(else_block)) {}
  void accept(Visitor &visitor) override { visitor.visit_if(*this); }
  std::string label() const override { return "If"; }
  std::vector<const Node *> children() const override {
    std::vector<const Node *> c = {cond.get(), then_block.get()};
    if (else_block)
      c.push_back(else_block.get());
    return c;
  }
};

class WhileNode : public StmtNode {
public:
  std::unique_ptr<ExprNode> cond;
  std::unique_ptr<BlockNode> body;
  WhileNode(uint64_t line, uint64_t col, std::unique_ptr<ExprNode> cond,
            std::unique_ptr<BlockNode> body)
      : StmtNode(line, col), cond(std::move(cond)), body(std::move(body)) {}
  void accept(Visitor &visitor) override { visitor.visit_while(*this); }
  std::string label() const override { return "While"; }
  std::vector<const Node *> children() const override {
    return {cond.get(), body.get()};
  }
};

class FnNode : public Node {
public:
  std::string name;
  std::vector<std::unique_ptr<ParamNode>> params;
  std::string result_type;
  uint64_t result_col;
  std::unique_ptr<BlockNode> body;
  FnNode(uint64_t line, uint64_t col, std::string name,
         std::vector<std::unique_ptr<ParamNode>> params,
         std::string result_type, uint64_t result_col,
         std::unique_ptr<BlockNode> body)
      : Node(line, col), name(std::move(name)), params(std::move(params)),
        result_type(std::move(result_type)), result_col(result_col),
        body(std::move(body)) {}
  void accept(Visitor &visitor) override { visitor.visit_fn(*this); }
  std::string label() const override {
    return "Fn " + name + " " + result_type;
  }
  std::vector<const Node *> children() const override {
    std::vector<const Node *> c;
    for (const std::unique_ptr<ParamNode> &p : params)
      c.push_back(p.get());
    c.push_back(body.get());
    return c;
  }
};

class ProgramNode : public Node {
public:
  std::vector<std::unique_ptr<StructNode>> structs;
  std::vector<std::unique_ptr<FnNode>> functions;
  std::vector<std::unique_ptr<StmtNode>> statements;
  std::unique_ptr<ExitNode> exit;
  ProgramNode(uint64_t line, uint64_t col,
              std::vector<std::unique_ptr<StructNode>> structs,
              std::vector<std::unique_ptr<FnNode>> functions,
              std::vector<std::unique_ptr<StmtNode>> statements,
              std::unique_ptr<ExitNode> exit)
      : Node(line, col), structs(std::move(structs)),
        functions(std::move(functions)), statements(std::move(statements)),
        exit(std::move(exit)) {}
  void accept(Visitor &visitor) override { visitor.visit_program(*this); }
  std::string label() const override { return "Program"; }
  std::vector<const Node *> children() const override {
    std::vector<const Node *> c;
    for (const std::unique_ptr<StructNode> &s : structs)
      c.push_back(s.get());
    for (const std::unique_ptr<FnNode> &f : functions)
      c.push_back(f.get());
    for (const std::unique_ptr<StmtNode> &s : statements)
      c.push_back(s.get());
    c.push_back(exit.get());
    return c;
  }
};
