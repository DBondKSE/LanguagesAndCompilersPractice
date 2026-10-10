# Languages and Compilers Practice

A small compiler for a toy language, written in C++ on top of LLVM. It tokenizes the source, parses it into an AST, type-checks it, and emits LLVM IR.

## The language

A program is structs, then functions, then statements, ending with `exit`:

```
struct Point
{
    i32 mut x
    i32 mut y
}
struct Circle
{
    Point center
    i64 mut radius
}
fn moved := (Point p, i32 dx) -> Point
{
    Point q{p.x + dx, p.y}
    exit q
}
Circle mut c{{10, 20}, 5}
Point r{moved(c.center, 1)}
if r.x != 11
{
    exit false
}
c.radius := c.radius * r.x + 1
exit c.radius
```

This prints `Program exit with result 56`.

### Values and variables

- Built-in types: `i32`, `i64`, `bool`
- A variable is declared as `type [mut] name{value}` and is immutable unless declared `mut`
- Operators: `+`, `-`, `*`, `==`, `!=`, and `!` on a `bool`; there are no grouping parentheses and no unary minus
- `i32` values widen to `i64` automatically; nothing narrows

### Structs

- `struct Name`, then `{`, one field per line, then `}`. A field is `type [mut] name`, and its type is a built-in or a struct declared above
- A struct has at least one field and cannot contain itself
- An object takes one value per field, in order: `Point p{10, 20}`
- The value for a struct field is an object, a call, or a nested `{}` with the same rules: `Circle c{{10, 20}, 5}`
- One value of the struct's own type copies the whole object: `Point q{p}`
- `.` reads a field (`c.center.x`) and, on the left of `:=`, writes it. A write needs every link to be `mut`: the object and each field on the way
- `:=` replaces a whole object only if all its fields, at every depth, are `mut`
- Structs cannot be compared or used in arithmetic

### Functions

- `fn name := (type a, type b) -> type`, then a block whose last line is `exit`
- Parameter types and the result type are built-ins or structs
- Parameters are copies and cannot be assigned in the body
- A body sees only its parameters, its own variables and the functions of the program, not the top-level variables
- A function may call any function of the program, including itself and one declared below it
- An `i32` argument widens to an `i64` parameter, and an `i32` value to an `i64` result
- A call can stand wherever a value can, but a call alone is not a statement
- Structs and functions are declared only at the top level, in that order, before the first statement

### Control flow

- `if cond` takes a `bool`; `{`, `}` and `else` stand alone on their lines; `else` is optional
- `while cond` repeats its block while `cond` (a `bool`) holds
- A block opens its own scope: an inner declaration may shadow an outer name with any type, and a block's names are gone after its `}`
- A block may end with `exit`; nothing follows an `exit` in the same block
- `exit` takes one operand: a constant, a variable, a field chain or a call
- At the top level `exit` ends the program and prints `Program exit with result <value>`; the value has a built-in type
- In a function `exit` ends the function, also from inside an `if` or `while`, and its value is the result

### Generated IR

- A struct is a named type, `%Point = type { i32, i32 }`, and an object is an `alloca` of it
- A field is reached with one `getelementptr` by index; the indices are resolved by the semantic pass
- A nested `{}` is one struct value built with `insertvalue`
- A function `name` becomes `@fn_name`, so it cannot clash with `main` or `printf`; its parameters are stored into slots in its entry block
- `opt -passes=mem2reg -S output.ll` promotes the variable slots to registers and shows the `phi` nodes at merge points

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
- [src/semantic.cpp](src/semantic.cpp): type, mutability, field and call checks
- [src/codegen.cpp](src/codegen.cpp): LLVM IR generation
- [src/compiler.cpp](src/compiler.cpp): command-line entry point

## Tests

```sh
tests/run_tests.sh
```

- `tests/ok/*.txt` must compile; the program's output is compared to the matching `.expected`, and the `--ast` dump to `.ast` when that file exists
- `tests/err/*.txt` must fail; the error message is compared to `.expected`
