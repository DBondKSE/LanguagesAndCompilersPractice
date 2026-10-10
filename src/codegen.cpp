#include "codegen.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>

using namespace llvm;

static std::string wider(const std::string &a, const std::string &b) {
  return a == "i64" || b == "i64" ? "i64" : a;
}

class CodeGen : public Visitor {
  LLVMContext ctx;
  IRBuilder<> builder{ctx};
  std::unique_ptr<Module> module;
  Type *i1, *i32, *i64;
  Function *printfFn;
  Function *function;
  std::map<const DeclNode *, Value *> slots;
  Value *result = nullptr;

  Type *llvm_type(const std::string &type) {
    if (type == "i64")
      return i64;
    if (type == "bool")
      return i1;
    return i32;
  }

  Value *eval(ExprNode &node) {
    node.accept(*this);
    return result;
  }

  Value *coerce(Value *value, const std::string &have,
                const std::string &want) {
    if (have == "i32" && want == "i64")
      return builder.CreateSExt(value, i64, "wide");
    return value;
  }

  Value *eval_as(ExprNode &node, const std::string &want) {
    return coerce(eval(node), node.type, want);
  }

  Value *entry_alloca(Type *type, const std::string &name) {
    IRBuilderBase::InsertPointGuard guard(builder);
    BasicBlock &entry = function->getEntryBlock();
    builder.SetInsertPoint(&entry, entry.getFirstNonPHIOrDbgOrAlloca());
    return builder.CreateAlloca(type, nullptr, name);
  }

  void emit_block(BlockNode &block, BasicBlock *bb, BasicBlock *next_bb) {
    builder.SetInsertPoint(bb);
    block.accept(*this);
    if (!builder.GetInsertBlock()->getTerminator())
      builder.CreateBr(next_bb);
  }

public:
  CodeGen() {
    i1 = Type::getInt1Ty(ctx);
    i32 = Type::getInt32Ty(ctx);
    i64 = Type::getInt64Ty(ctx);
    module = std::make_unique<Module>("practice1", ctx);
#if LLVM_VERSION_MAJOR >= 21
    module->setTargetTriple(Triple(sys::getDefaultTargetTriple()));
#else
    module->setTargetTriple(sys::getDefaultTargetTriple());
#endif

    function =
        Function::Create(FunctionType::get(i32, false),
                         Function::ExternalLinkage, "main", module.get());
    builder.SetInsertPoint(BasicBlock::Create(ctx, "entry", function));

    Type *i8ptr = PointerType::get(Type::getInt8Ty(ctx), 0);
    printfFn = Function::Create(FunctionType::get(i32, {i8ptr}, true),
                                Function::ExternalLinkage, "printf",
                                module.get());
  }

  Module &getModule() { return *module; }

  void visit_program(ProgramNode &node) override {
    for (std::unique_ptr<StmtNode> &stmt : node.statements)
      stmt->accept(*this);
    node.exit->accept(*this);
  }

  void visit_decl(DeclNode &node) override {
    Value *init = eval_as(*node.init, node.type_name);
    Value *slot = entry_alloca(llvm_type(node.type_name), node.name);
    builder.CreateStore(init, slot);
    slots[&node] = slot;
  }

  void visit_assign(AssignNode &node) override {
    builder.CreateStore(eval_as(*node.value, node.decl->type_name),
                        slots.at(node.decl));
  }

  void visit_exit(ExitNode &node) override {
    Value *value = eval_as(*node.value, "i64");
    if (node.value->type == "bool") {
      Value *text = builder.CreateSelect(
          value, builder.CreateGlobalStringPtr("true"),
          builder.CreateGlobalStringPtr("false"), "text");
      builder.CreateCall(
          printfFn,
          {builder.CreateGlobalStringPtr("Program exit with result %s\n"),
           text});
    } else {
      builder.CreateCall(
          printfFn,
          {builder.CreateGlobalStringPtr("Program exit with result %lld\n"),
           value});
    }
    builder.CreateRet(ConstantInt::get(i32, 0));
  }

  void visit_block(BlockNode &node) override {
    for (std::unique_ptr<StmtNode> &stmt : node.statements)
      stmt->accept(*this);
    if (node.exit)
      node.exit->accept(*this);
  }

  void visit_if(IfNode &node) override {
    Value *cond = eval(*node.cond);
    BasicBlock *then_bb = BasicBlock::Create(ctx, "then", function);
    BasicBlock *else_bb =
        node.else_block ? BasicBlock::Create(ctx, "else", function) : nullptr;
    BasicBlock *merge_bb = BasicBlock::Create(ctx, "merge", function);
    builder.CreateCondBr(cond, then_bb, else_bb ? else_bb : merge_bb);

    emit_block(*node.then_block, then_bb, merge_bb);
    if (else_bb)
      emit_block(*node.else_block, else_bb, merge_bb);
    builder.SetInsertPoint(merge_bb);
  }

  void visit_while(WhileNode &node) override {
    BasicBlock *cond_bb = BasicBlock::Create(ctx, "cond", function);
    BasicBlock *body_bb = BasicBlock::Create(ctx, "body", function);
    BasicBlock *end_bb = BasicBlock::Create(ctx, "end", function);
    builder.CreateBr(cond_bb);

    builder.SetInsertPoint(cond_bb);
    builder.CreateCondBr(eval(*node.cond), body_bb, end_bb);

    emit_block(*node.body, body_bb, cond_bb);
    builder.SetInsertPoint(end_bb);
  }

  void visit_not(NotNode &node) override {
    result = builder.CreateNot(eval(*node.operand), "not");
  }

  void visit_binop(BinOpNode &node) override {
    std::string type = wider(node.left->type, node.right->type);
    Value *l = eval_as(*node.left, type);
    Value *r = eval_as(*node.right, type);
    if (node.op == "+")
      result = builder.CreateAdd(l, r, "add");
    else if (node.op == "-")
      result = builder.CreateSub(l, r, "sub");
    else if (node.op == "*")
      result = builder.CreateMul(l, r, "mul");
    else if (node.op == "==")
      result = builder.CreateICmpEQ(l, r, "eq");
    else
      result = builder.CreateICmpNE(l, r, "ne");
  }

  void visit_var(VarNode &node) override {
    result = builder.CreateLoad(llvm_type(node.decl->type_name),
                                slots.at(node.decl), node.name);
  }

  void visit_const(ConstNode &node) override {
    result = ConstantInt::get(llvm_type(node.type), node.value, true);
  }

  void visit_bool(BoolNode &node) override {
    result = ConstantInt::get(i1, node.value);
  }
};

int compile(ProgramNode &program, const char *out_path) {
  CodeGen codegen;
  program.accept(codegen);
  if (verifyModule(codegen.getModule(), &errs()))
    return 1;

  std::error_code ec;
  raw_fd_ostream out(out_path, ec);
  if (ec) {
    std::cerr << "error: cannot write " << out_path << "\n";
    return 1;
  }
  codegen.getModule().print(out, nullptr);
  return 0;
}
