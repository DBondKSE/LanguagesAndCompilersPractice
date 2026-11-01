#include "codegen.h"
#include "lexer.h"
#include "optimizer.h"
#include "parser.h"
#include "semantic.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  std::string mode;
  bool removeUnused = false;
  bool usage = false;
  std::vector<std::string> paths;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--remove-unused")
      removeUnused = true;
    else if (arg == "--tokens" || arg == "--ast" || arg == "--ast-checked" ||
             arg == "--check") {
      usage = usage || !mode.empty();
      mode = arg;
    } else
      paths.push_back(arg);
  }
  bool dumpTokens = mode == "--tokens";
  bool dumpAst = mode == "--ast";
  bool dumpAstChecked = mode == "--ast-checked";
  bool checkOnly = mode == "--check";
  if (usage || paths.size() != (mode.empty() ? 2u : 1u)) {
    std::cerr << "usage: " << argv[0]
              << " [--remove-unused] <source> <output.ll>\n"
              << "       " << argv[0] << " --tokens <source>\n"
              << "       " << argv[0] << " --ast <source>\n"
              << "       " << argv[0]
              << " [--remove-unused] --ast-checked <source>\n"
              << "       " << argv[0] << " [--remove-unused] --check <source>\n";
    return 1;
  }

  const std::string &srcPath = paths[0];
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
  if (removeUnused)
    remove_unused(*program);
  if (dumpAstChecked) {
    program->dump();
    return 0;
  }
  if (checkOnly)
    return 0;

  return compile(*program, paths[1].c_str());
}
