# Languages and Compilers Practice

A small compiler for a toy language, written in C++ on top of LLVM. It tokenizes the source, parses it into an AST, type-checks it, and emits LLVM IR.

## The language

A program is a list of declarations, assignments, `if` and `while` statements, ending with `exit`:

```
i32 x{15}
i64 mut y{x}
bool big{y != 0}
if !big
{
    exit 0
}
else
{
    y := y * x + 1
}
exit y
```

- Types: `i32`, `i64`, `bool`
- Variables are immutable unless declared `mut`
- Operators: `+`, `-`, `*`, `==`, `!=`, and `!` on a `bool`
- `i32` values widen to `i64` automatically; nothing narrows
- `if cond` takes a `bool`; `{`, `}` and `else` stand alone on their lines; `else` is optional
- `while cond` repeats its block while `cond` (a `bool`) holds
- A block opens its own scope: an inner declaration may shadow an outer name with any type, and a block's names are gone after its `}`
- A block may end with `exit`; nothing follows an `exit` in the same block
- `exit` prints `Program exit with result <value>`

`opt -passes=mem2reg -S output.ll` promotes the variable slots to registers and shows the `phi` nodes at merge points.

The full grammar is in [grammar.ebnf](grammar.ebnf).

## Building

Requires `clang++` and LLVM (`llvm-config`, `llc`, `lli` on your `PATH`).

```sh
sh cpile
```

This produces the `compiler` binary.

## Usage

```sh
./compiler program.txt output.ll   # compile to LLVM IR
./compiler --tokens program.txt    # print tokens
./compiler --ast program.txt       # print the AST
./compiler --check program.txt     # type-check only
```

To compile and run in one step:

```sh
sh cmprun.sh program.txt
```

Errors are reported as `compilation error: line L:C: <message>` and no output file is written.

## Layout

- [src/lexer.cpp](src/lexer.cpp): tokenizer
- [src/parser.cpp](src/parser.cpp): recursive-descent parser producing the AST in [src/ast.h](src/ast.h)
- [src/semantic.cpp](src/semantic.cpp): type and mutability checks
- [src/codegen.cpp](src/codegen.cpp): LLVM IR generation
- [src/compiler.cpp](src/compiler.cpp): command-line entry point

## Tests

```sh
tests/run_tests.sh
```

- `tests/ok/*.txt` must compile; the program's output is compared to the matching `.expected`, and the `--ast` dump to `.ast` when that file exists
- `tests/err/*.txt` must fail; the error message is compared to `.expected`
