#include "codegen.h"
#include "error.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>

using namespace llvm;

typedef struct {
  Value *slot;
  bool mut;
} variable;

class CodeGen : public Visitor {
  LLVMContext ctx;
  IRBuilder<> builder{ctx};
  std::unique_ptr<Module> module;
  Type *i32;
  Function *printfFn;
  Value *fmt;
  std::map<std::string, variable> vars;
  Value *result = nullptr;

  Value *eval(const ExprNode &node) {
    node.accept(*this);
    return result;
  }

  const variable &lookup(const std::string &name, uint64_t line,
                         uint64_t col) {
    auto it = vars.find(name);
    if (it == vars.end())
      error(line, col, "variable '" + name + "' is used before its declaration");
    return it->second;
  }

public:
  CodeGen() {
    i32 = Type::getInt32Ty(ctx);
    module = std::make_unique<Module>("practice1", ctx);
#if LLVM_VERSION_MAJOR >= 21
    module->setTargetTriple(Triple(sys::getDefaultTargetTriple()));
#else
    module->setTargetTriple(sys::getDefaultTargetTriple());
#endif

    Function *mainFn =
        Function::Create(FunctionType::get(i32, false),
                         Function::ExternalLinkage, "main", module.get());
    builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", mainFn));

    Type *i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
    printfFn = Function::Create(FunctionType::get(i32, {i8ptr}, true),
                                Function::ExternalLinkage, "printf",
                                module.get());
    fmt = builder.CreateGlobalStringPtr("Program exit with result %d\n");
  }

  const Module &getModule() const { return *module; }

  void visit_program(const ProgramNode &node) override {
    for (const std::unique_ptr<StmtNode> &stmt : node.statements)
      stmt->accept(*this);
    node.exit->accept(*this);
  }

  void visit_decl(const DeclNode &node) override {
    if (vars.count(node.name))
      error(node.line, node.col,
            "variable '" + node.name + "' is already declared");
    Value *init = eval(*node.init);
    Value *slot = builder.CreateAlloca(i32, nullptr, node.name);
    builder.CreateStore(init, slot);
    vars[node.name] = {slot, node.mut};
  }

  void visit_assign(const AssignNode &node) override {
    const variable &var = lookup(node.name, node.line, node.col);
    if (!var.mut)
      error(node.line, node.col,
            "cannot assign to '" + node.name + "': it is not mut");
    builder.CreateStore(eval(*node.value), var.slot);
  }

  void visit_exit(const ExitNode &node) override {
    builder.CreateCall(printfFn, {fmt, eval(*node.value)});
    builder.CreateRet(ConstantInt::get(i32, 0));
  }

  void visit_binop(const BinOpNode &node) override {
    Value *l = eval(*node.left);
    Value *r = eval(*node.right);
    if (node.op == '+')
      result = builder.CreateAdd(l, r, "add");
    else if (node.op == '-')
      result = builder.CreateSub(l, r, "sub");
    else
      result = builder.CreateMul(l, r, "mul");
  }

  void visit_var(const VarNode &node) override {
    result = builder.CreateLoad(
        i32, lookup(node.name, node.line, node.col).slot, node.name);
  }

  void visit_const(const ConstNode &node) override {
    result = ConstantInt::get(i32, node.value);
  }
};

int compile(const ProgramNode &program, const char *out_path) {
  CodeGen codegen;
  program.accept(codegen);

  std::error_code ec;
  raw_fd_ostream out(out_path, ec);
  if (ec) {
    std::cerr << "error: cannot write " << out_path << "\n";
    return 1;
  }
  codegen.getModule().print(out, nullptr);
  return 0;
}
