# Languages and Compilers Practice

A small compiler for a toy language, written in C++ on top of LLVM. It tokenizes the source, parses it into an AST, type-checks it, and emits LLVM IR.

## The language

A program is structs (with their member functions), then functions, then statements, ending with `exit`:

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

- `struct Name`, then `{`, one field per line, then optionally [member functions](#member-functions), then `}`. A field is `type [mut] name`, and its type is a built-in or a struct declared above
- A struct has at least one field and cannot contain itself
- An object takes one value per field, in order: `Point p{10, 20}`
- The value for a struct field is an object, a call, or a nested `{}` with the same rules: `Circle c{{10, 20}, 5}`
- One value of the struct's own type copies the whole object: `Point q{p}`
- `.` reads a field (`c.center.x`) and, on the left of `:=`, writes it. A write needs every link to be `mut`: the object and each field on the way
- `.` may also follow a call whose result is a struct: `origin().x`, `c.grown(3).radius`. The result is a temporary copy: its fields can be read and any member function can be called on it, but nothing can be assigned to it
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

### Member functions

A struct may declare functions after its fields. A member function sees the fields of its object by name and is called on an object:

```
struct Point
{
    i32 mut x
    i32 mut y

    fn d2 := () -> i32
    {
        i32 r{x * x + y * y}
        exit r
    }

    fn shift := (i32 dx) -> i32
    {
        x := x + dx
        exit x
    }
}
Point mut p{3, 4}
i32 d{p.d2()}
i32 nx{p.shift(2)}
i32 r{d + nx + p.x}
exit r
```

This prints `Program exit with result 35`.

- Fields come first, then member functions; a field and a member function of one struct cannot share a name, and neither can two member functions
- `obj.f(...)` stands wherever a value can, also after `exit`; the object can be a field chain (`c.center.d2()`) or a call result (`p.moved(1).shift(2)`). It is called on an object, never on a type: `Point.d2()` is an error
- Inside a member function a name is looked up among the parameters and local variables first, then among the fields, so a parameter or local named like a field hides it. Top-level variables are not visible
- A call `f(...)` inside a member function looks among the member functions of the same struct first, then among the global functions; `d2()` inside `Point` calls `d2` on the same object
- A field without `mut` cannot be assigned, also inside a member function
- The object is passed by address, so a member function that assigns a field changes the caller's object
- A member function is **mutating** if it assigns a field of its object, or calls a mutating member function on its object or one of its fields (worked out after all bodies are checked, so cycles are fine). A mutating function may be called only through a chain where every link is `mut`: the object and each field on the way. A parameter counts as not `mut`. A temporary call result can take any call

### Callable objects

A struct that declares a member function named `call` can be called like a function: `triple(14)` means `triple.call(14)`, and the longer form stays legal.

- For `name(...)`, if `name` is a visible variable (a parameter, a local, or a field inside a member function), its type must be a struct with `call`; otherwise `name` is looked up as a function, as before
- A name that is both a visible variable and a function cannot be called: `'twice' is both a variable and a function: rename one of them`. Declaring both is allowed
- After a dot the same rule holds: if `scale` is a field of `s`, `s.scale(7)` is `s.scale.call(7)`; inside a member function, `scale(side)` calls the field of its own object
- The call is checked like the member call it stands for: the arguments, and the mutation rule. With `Counter c{0}` whose `call` changes `n`, `c()` is an error
- The semantic pass replaces the call with that member call, so both forms produce the same IR. `--ast` shows the parse tree (`Call triple`), `--ast-checked` the tree after the rewrite (`CallableCall triple`)

### Lambdas

A lambda is a function declared where a statement stands, together with copies of the variables it needs, its captures:

```
i32 mut k{10}
fn add_k := [k](i32 y) -> i32
{
    i32 r{k + y}
    exit r
}
k := 100
print(add_k(5))
exit add_k(1)
```

This prints `15` and `Program exit with result 11`.

- `fn name := [captures](params) -> type`, then a block ending with `exit`. It stands at the top level among the statements and inside any block: a function, a member function, `if`, `while`, another lambda. `fn` without `[` inside a block is still an error, and so is a capture list on a member function
- A capture is a copy taken at the declaration: changing the variable later does not change the lambda, and a lambda declared in a loop takes the current value on every iteration. Inside the body a capture is const unless it is written `[mut n]`
- A `mut` capture is the lambda's own mutable copy: its field is `mut`, a `call` that assigns it is a mutating member function, and the lambda variable is `mut` so the call is allowed where it is declared. The copy lives in the lambda, so it keeps counting from call to call while the original variable never changes; a `mut` capture of a const variable is allowed. A captured lambda is const unless written `[mut next]`, and calling a mutating one through a const capture is an error: `cannot call 'next': its 'call' changes it, and the capture 'next' is const`
- The body sees its parameters, its locals, its captures and the global functions. Nothing else: not the top-level variables, not the locals, parameters or fields around it, unless they are captured. A lambda cannot call itself, since its name is declared after its body; it may capture another lambda
- A capture names a visible variable (a parameter, a local, a field inside a member function, or a capture of an enclosing lambda), at most once, and no parameter may share its name
- A lambda's type has no name: it cannot be passed as an argument, be the value of `exit`, initialise a variable or a field, be assigned or be printed, and its captures are not visible outside its body (`add_k.k` is an error)
- The semantic pass turns a lambda into a struct `lambda.add_k` with one field per capture, its member function `call`, and a variable `lambda.add_k add_k{k}`; `add_k(5)` is then a [callable object](#callable-objects). A dot cannot appear in a declared name, so the struct never clashes with one; two lambdas with the same name get `lambda.by` and `lambda.by.2`

### Input and output

```
Point p{read(), read()}
print("p is:")
print(p)
print(p.x == 3)
exit p.y
```

With `3 4` on the standard input this prints `p is:`, `Point{x: 3, y: 4}`, `true` and `Program exit with result 4`.

- `read()` reads one integer from the standard input; its result is `i32` and it stands wherever a value can. It takes no arguments
- `print(arg)` is a statement that prints its argument and a newline; it stands wherever a statement can, has no value, and takes exactly one argument:
  - an `i32` or `i64`: the number
  - a `bool`: `true` or `false`
  - a string literal: `"`, any bytes except `"` and a newline, then `"`. There are no escape sequences, and a literal stands only as the argument of `print`
  - a struct object (a variable, a field chain or a call result): `Circle{center: Point{x: 3, y: 4}, radius: 2}`
- `read` and `print` are reserved and cannot name a variable, function, parameter, field or member function
- The output of `print` comes before the `Program exit with result` line

### Control flow

- `if cond` takes a `bool`; `{`, `}` and `else` stand alone on their lines; `else` is optional
- `while cond` repeats its block while `cond` (a `bool`) holds
- A block opens its own scope: an inner declaration may shadow an outer name with any type, and a block's names are gone after its `}`
- A block may end with `exit`; nothing follows an `exit` in the same block
- `exit` takes one operand: a constant, a variable, a field chain, a call, a member call or `read()`
- At the top level `exit` ends the program and prints `Program exit with result <value>`; the value has a built-in type
- In a function `exit` ends the function, also from inside an `if` or `while`, and its value is the result

### Generated IR

- A struct is a named type, `%Point = type { i32, i32 }`, and an object is an `alloca` of it
- A field is reached with one `getelementptr` by index; the indices are resolved by the semantic pass
- A nested `{}` is one struct value built with `insertvalue`
- A function `name` becomes `@fn_name`, so it cannot clash with `main` or `printf`; its parameters are stored into slots in its entry block
- A member function `shift` of `Point` becomes `@Point_shift(ptr %this, ...)`. A field in its body is a `getelementptr` on `%this`, so `x := x + dx` is a `getelementptr`, a `load`, an `add` and a `store`. A call passes the object's `alloca`, or a `getelementptr` into it for `c.center.d2()`
- A lambda becomes the type `%"lambda.add_k" = type { i32 }` and the function `@"lambda.add_k_call"(ptr %this, ...)`; its declaration is an `alloca`, and a `getelementptr` and a `store` per capture
- A field of a call result is read with `extractvalue`; for a member call on a call result, the result is stored into a temporary `alloca` whose address is passed as `%this`
- `printf` is declared once and shared by `exit` and `print`; `scanf` is declared when `read()` is used and writes into an `alloca` in the entry block. Formats and texts are global constants: `"%d\n"` for `i32`, `"%lld\n"` for `i64`, `"%s\n"` with a `select` between `"true"` and `"false"` for `bool`, and `"%s\n"` for a literal
- A struct is printed by walking its fields: `printf("Point{")`, then a `getelementptr`, a `load` and a `printf` with a format like `", y: %d"` per field (one level down for a struct field), then `printf("}")`
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
./compiler --ast-checked program.txt # print the AST after the semantic pass
./compiler --check program.txt     # type-check only
```

To compile and run in one step:

```sh
sh cmprun.sh program.txt
sh cmprun.sh program.txt < input.txt   # a program that calls read()
```

Errors are reported as `compilation error: line L:C: <message>` and no output file is written.

## Layout

- [src/lexer.cpp](src/lexer.cpp): tokenizer
- [src/parser.cpp](src/parser.cpp): recursive-descent parser producing the AST in [src/ast.h](src/ast.h)
- [src/semantic.cpp](src/semantic.cpp): type, mutability, field, call and member-function checks
- [src/codegen.cpp](src/codegen.cpp): LLVM IR generation
- [src/compiler.cpp](src/compiler.cpp): command-line entry point

## Tests

```sh
tests/run_tests.sh
```

- `tests/ok/*.txt` must compile; the program's whole standard output (`print` lines and the exit line) is compared to the matching `.expected`, and the `--ast` and `--ast-checked` dumps to `.ast` and `.ast-checked` when those files exist
- a program that calls `read()` keeps its input next to it in `NAME.in`, which is passed as the standard input
- `tests/err/*.txt` must fail; the error message is compared to `.expected`
