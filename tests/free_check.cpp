#include "../src/lexer.h"
#include "../src/optimizer.h"
#include "../src/parser.h"
#include "../src/semantic.h"

#include <iostream>
#include <sanitizer/asan_interface.h>

int main() {
  std::unique_ptr<ProgramNode> program =
      Parser(tokenize("i32 x{10}\ni32 y{x}\ni32 z{15}\nexit z\n"))
          .parse_program();
  SemanticChecker checker;
  program->accept(checker);

  const Node *x = program->statements[0].get();
  const Node *y = program->statements[1].get();
  std::cout << "before the pass: " << x->label() << " freed "
            << __asan_address_is_poisoned(x) << ", " << y->label()
            << " freed " << __asan_address_is_poisoned(y) << std::endl;
  remove_unused(*program);
  std::cout << "after the pass: x freed " << __asan_address_is_poisoned(x)
            << ", y freed " << __asan_address_is_poisoned(y) << ", "
            << program->statements.size() << " statement left" << std::endl;
  std::cout << y->label() << "\n";
}
