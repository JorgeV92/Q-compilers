# The Q language

Q is a small expression-oriented language built while working through LLVM's
"My First Language Frontend" tutorial. It uses the same useful compiler
architecture as Kaleidoscope—lexer, parser, AST, LLVM IR, and eventually a
JIT—but it has its own source syntax and room for its own semantics.

1. The **lexer** turns source text into tokens.
2. The **parser** checks the token sequence using Q's grammar.
3. The **AST** records the meaning and structure of a valid Q program.
4. The optional **IR generator** translates scalar arithmetic functions to LLVM IR.

The long-term goal is a Python-style language for machine learning and deep
learning. The first two LLVM increments support numeric expressions, function
parameters, and calls between Q functions. Python-style blocks, tensors, and training
are future work; the current Q syntax remains the foundation.

The compiler is in [src/qlang.cpp](../src/qlang.cpp), with runnable inputs in
[examples/](../examples/) and checks in [tests/](../tests/). See the
[README](../README.md) for the project layout and quick start. All commands
below run from the repository root.

## What makes Q different

Kaleidoscope uses `def`, `extern`, whitespace-separated parameters, and `#`
comments. Q starts with several deliberate differences:

- Functions use an arrow: `fn square(x) => x * x;`
- Native declarations use `foreign`: `foreign print(value);`
- Parameters and arguments are comma-separated.
- Comments begin with `//`.
- Every top-level item ends in `;`.
- Q supports unary `+`, `-`, and `!`, comparisons, remainder, and power.
- `^` is right-associative, so `2 ^ 3 ^ 2` means `2 ^ (3 ^ 2)`.
- Q has a pipeline expression: `value |> transform |> print`.

The pipeline is represented by its own `PipelineExprAST`. Its intended future
meaning is:

```q
value |> transform       // transform(value)
value |> scale(2)        // scale(value, 2)
```

The parser records pipelines, but the current IR generator does not compile
them. Nothing is executed by the front end yet.

## A first Q program

```q
// Functions provided by the host program.
foreign print(value);
foreign sqrt(value);

fn square(x) => x ^ 2;
fn length(x, y) => sqrt(square(x) + square(y));

3 + 4 * 5 |> square |> print;
```

Running the current front end prints an indented AST. That output makes it easy
to verify precedence and tree structure before adding code generation.

## Grammar

The current grammar, in a small EBNF notation, is:

```text
program        = { ";" | top-level ";" } EOF ;
top-level      = function | foreign | expression ;

function       = "fn" prototype "=>" expression ;
foreign        = "foreign" prototype ;
prototype      = identifier "(" [ parameters ] ")" ;
parameters     = identifier { "," identifier } ;

expression     = unary { binary-op expression } ;
unary          = ("+" | "-" | "!") unary | primary ;
primary        = number
               | identifier
               | call
               | "(" expression ")" ;
call           = identifier "(" [ arguments ] ")" ;
arguments      = expression { "," expression } ;
```

The parser uses precedence climbing for binary expressions. From weakest to
strongest, Q currently recognizes:

| Precedence | Operators | Associativity |
| ---: | --- | --- |
| 5 | `\|>` | left |
| 10 | `==`, `!=` | left |
| 20 | `<`, `<=`, `>`, `>=` | left |
| 30 | `+`, `-` | left |
| 40 | `*`, `/`, `%` | left |
| 50 | `^` | right |

Unary operators bind more tightly than the binary operators in this first
grammar.

## Lexer

The lexer lives in the `Lexer` class. Unlike the tutorial's global `gettok()`
function, it owns a source string and its current position. Calling `next()`
returns a `Token` containing:

- the token kind;
- the original source spelling;
- a numeric value when the token is a number; and
- its line and column for diagnostics.

Q recognizes these named token kinds:

```cpp
enum TokenKind {
  tok_eof = -1,
  tok_fn = -2,
  tok_foreign = -3,
  tok_identifier = -4,
  tok_number = -5,
  tok_arrow = -6,
  tok_pipe = -7,
  tok_equal_equal = -8,
  tok_bang_equal = -9,
  tok_less_equal = -10,
  tok_greater_equal = -11,
  tok_invalid = -12,
};
```

Single-character punctuation such as `(`, `)`, `+`, and `;` uses its ASCII
value as the token kind. Identifiers follow
`[A-Za-z_][A-Za-z0-9_]*`. Numbers may include a fractional part and a decimal
exponent, for example `42`, `.5`, `2.0`, or `6.02e23`.

## AST

The AST separates source syntax from later LLVM code generation. Ownership is
expressed with `std::unique_ptr`, so deleting a root node safely deletes the
whole tree.

Expression nodes:

- `NumberExprAST` stores a numeric literal.
- `NameExprAST` stores a reference to a name.
- `UnaryExprAST` stores an operator and one operand.
- `BinaryExprAST` stores an operator and two operands.
- `PipelineExprAST` stores a value and its pipeline destination.
- `CallExprAST` stores a callee name and argument expressions.

Top-level nodes:

- `PrototypeAST` stores a function name and parameter names.
- `FunctionAST` stores a prototype and body expression.
- `ForeignAST` stores a host-provided function prototype.
- `TopLevelExprAST` wraps an expression appearing at file scope.
- `ProgramAST` owns all top-level items in a source file.

Every node also keeps its starting source location. The `dump()` methods remain
available for inspecting syntax. `IRGenerator` uses the existing AST accessors
to generate code without adding LLVM dependencies to the AST classes.

## Parser

`Parser` keeps one current token as lookahead. Most grammar productions have a
matching method:

- `parsePrototype()`, `parseFunction()`, and `parseForeign()` parse declarations.
- `parsePrimary()` selects literals, names, calls, or parenthesized expressions.
- `parseUnary()` handles prefix operators.
- `parseExpression()` uses precedence climbing for binary and pipeline operators.
- `parseProgram()` collects complete, semicolon-terminated top-level items.

The parser reports line-and-column errors, rejects duplicate parameter names,
and skips to the next semicolon after an error so it can find additional errors
in the same file.

## Build and run

The AST-only build still needs only a C++17 compiler:

```sh
make ast
./build/qlang-ast examples/calls.q
```

It can also read standard input:

```sh
printf 'fn square(x) => x * x; 5 |> square;' | ./build/qlang-ast
```

For valid input, `qlang` prints `Program` followed by its AST. Syntax errors are
printed to standard error and produce a nonzero exit status.

### Build with LLVM IR support

With LLVM development headers, libraries, and `llvm-config` on `PATH`
(tested with LLVM 20.1.7):

```sh
make
./build/qlang --emit-llvm examples/scalars.q > build/scalars.ll
llvm-as build/scalars.ll -o build/scalars.bc
./build/qlang --dump-ast examples/scalars.q
```

`make` (or `make llvm`) builds `build/qlang`; `make ast` builds a separate
`build/qlang-ast` without consulting LLVM tools. This lets both builds coexist.
Set `LLVM_CONFIG` when using a versioned or non-default installation, for
example `make LLVM_CONFIG=llvm-config-20`.

The [Makefile](../Makefile) keeps the underlying compiler commands visible.
For a direct LLVM-enabled build:

```sh
mkdir -p build
clang++ -DQ_ENABLE_LLVM src/qlang.cpp \
  $(llvm-config --cxxflags --ldflags --system-libs --libs core) -o build/qlang
```

Both modes accept a source filename or read standard input when it is omitted.
No flag still selects AST output. `--emit-llvm` on an AST-only build reports how
to enable LLVM support. IR goes to standard output; diagnostics go to standard
error. A syntax or code-generation error exits with status 1 and prints no IR,
even if earlier functions were valid. Command-line errors exit with status 2.

## LLVM increment 1: scalar arithmetic

This is one reviewable step toward Chapter 3, with the implementation grouped
in `IRGenerator` inside [src/qlang.cpp](../src/qlang.cpp).

```q
fn linear(x, weight, bias) => x * weight + bias;
fn squared_error(prediction, target) => (prediction - target) * (prediction - target);
fn mean2(a, b) => (a + b) / 2;
```

Supported in IR mode:

- Numeric literals, parameter references, and parentheses.
- Unary `+` and `-`; binary `+`, `-`, `*`, and `/` with existing Q precedence.
- `fn` definitions with zero or more parameters and one expression as the body.
- Standalone arithmetic expressions, each wrapped in a function named
  `__q_expr.0`, `__q_expr.1`, and so on. The dot prevents collisions with Q names.

All numbers, parameters, and return values use LLVM `double` (64-bit floating
point). `/` is floating-point division: `5 / 2` produces `2.5`. Division by zero
uses LLVM floating-point behavior rather than raising a Python exception.
There are no integer or boolean types yet. A name must refer to a parameter of
the current function; parameter names do not carry over to other functions.

`foreign`, pipelines, `!`, comparisons, `%`, and `^` still parse and dump
as ASTs, but IR mode reports them as unsupported at their source location. Use
`x * x` for squaring in this increment. Duplicate function definitions and
unknown names are also errors.

### How the translation works

`LLVMContext` holds LLVM types and constants. `Module` owns generated functions;
`IRBuilder` inserts instructions into a function's `entry` block. A map connects
each Q parameter to its LLVM argument. Numeric literals become floating-point
constants; arithmetic recursively emits its operands and then an instruction.
Each body ends with `ret`. The generator verifies functions and the complete
module before printing it. These are the building blocks introduced in
[LLVM Chapter 3](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html).

For `linear`, the emitted function is:

```llvm
define double @linear(double %x, double %weight, double %bias) {
entry:
  %mul = fmul double %x, %weight
  %add = fadd double %mul, %bias
  ret double %add
}
```

`%mul` and `%add` name intermediate results. `fmul` multiplies two doubles;
`fadd` adds them. Constant expressions may fold immediately, so `2 + 3` can
appear as `ret double 5.000000e+00` without an addition instruction.

The compiler emits IR; it has no JIT or automatic entry point. A host program
can compile and link the generated functions, as the test below demonstrates.
Top-level wrappers are emitted but are not invoked automatically.

## LLVM increment 2: function calls

Functions can now call previously defined Q functions:

```q
fn square(x) => x * x;
fn loss(prediction, target) => square(prediction - target);
loss(7, 4);
```

Generate IR for this example with:

```sh
./build/qlang --emit-llvm examples/calls.q > build/calls.ll
llvm-as build/calls.ll -o build/calls.bc
```

`IRGenerator::emitExpression()` handles `CallExprAST` by looking up the callee
in the module, checking the argument count, generating argument expressions
from left to right, and using `IRBuilder::CreateCall`. Arguments and the result
are still `double`. For `loss`, the body includes:

```llvm
%sub = fsub double %prediction, %target
%call = call double @square(double %sub)
ret double %call
```

Nested calls such as `square(square(2))` and calls to functions with no
parameters work. A function becomes visible when its definition starts, so
calls to later definitions are errors. A function can reference itself, but
there is no conditional base case yet; executing such recursion will not
terminate normally. Calls use named functions, not function-valued parameters.

Unknown functions, incorrect argument counts, and errors inside arguments
produce line-and-column diagnostics and no IR. For example, `square(1, 2)`
reports `incorrect argument count for 'square': expected 1, got 2`.
`foreign` declarations and pipelines remain separate future increments.

### Check the IR increments

Build the LLVM-enabled compiler and run its tests:

```sh
make test
```

You can also run `python3 tests/test_ir.py build/qlang` directly. The Makefile
selects `llvm-as` from the `LLVM_CONFIG` installation and uses `clang` on `PATH`;
both tools are overridable with `LLVM_AS` and `CLANG`. The test assembles the
IR, compiles it with a small C caller, and checks
numeric results, nested and zero-argument calls, argument order, definition
order, parameter scope, rejected features, diagnostics, and AST mode.
Temporary build files are cleaned up automatically.

## Small increments toward Python-style ML

Keep each language feature in its own commit with its example and tests.
Review layout changes separately from language changes.

1. **Next:** `foreign` declarations, including agreement between declarations
   and definitions, so Q can call native math functions.
2. Lower `value |> scale(2)` to `scale(value, 2)`, evaluating the value once.
3. Define truth values, comparisons, power, and remainder semantics.
4. Add Python-style `def`, `return`, and indentation in a separate parser change;
   then local bindings and control flow in further increments.
5. Design tensor types and shapes before adding tensor operations, a runtime,
   automatic differentiation, or accelerator support.

Each increment should include one working example, focused validation, and its
own update here. This checkpoint provides scalar building blocks for formulas;
it does not yet implement a machine-learning runtime.

References: [LLVM Chapter 2: Parser and AST](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl02.html),
[Chapter 3 for LLVM 20](https://releases.llvm.org/20.1.0/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html).
