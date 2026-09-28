#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  std::string mode = argc == 3 ? argv[1] : "";
  bool dumpTokens = mode == "--tokens";
  bool dumpAst = mode == "--ast";
  bool checkOnly = mode == "--check";
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <source> <output.ll>\n"
              << "       " << argv[0] << " --tokens <source>\n"
              << "       " << argv[0] << " --ast <source>\n"
              << "       " << argv[0] << " --check <source>\n";
    return 1;
  }

  const char *srcPath = dumpTokens || dumpAst || checkOnly ? argv[2] : argv[1];
  std::ifstream in(srcPath, std::ios::binary);
  if (!in) {
    std::cerr << "error: cannot open " << srcPath << "\n";
    return 1;
  }
  std::ostringstream src;
  src << in.rdbuf();

  std::vector<std::vector<token>> lines = tokenize(src.str());
  if (dumpTokens) {
    print_tokens(lines);
    return 0;
  }
  std::unique_ptr<ProgramNode> program = Parser(lines).parse_program();
  if (dumpAst) {
    program->dump();
    return 0;
  }

  SemanticChecker checker;
  program->accept(checker);
  if (checkOnly)
    return 0;

  return compile(*program, argv[2]);
}
