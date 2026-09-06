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
learning. This checkpoint adds the first small LLVM increment: numeric
expressions and function parameters. Python-style blocks, tensors, and training
are future work; the current Q syntax remains the foundation.

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

The parser records pipelines, but this first IR increment does not compile
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

From `q-llvm/Q-language`:

```sh
clang++ -std=c++17 -Wall -Wextra -Wpedantic Qlanguage.cpp -o qlang
./qlang program.q
```

It can also read standard input:

```sh
printf 'fn square(x) => x * x; 5 |> square;' | ./qlang
```

For valid input, `qlang` prints `Program` followed by its AST. Syntax errors are
printed to standard error and produce a nonzero exit status.

### Build with LLVM IR support

With LLVM development headers, libraries, and `llvm-config` on `PATH`, run from
`q-llvm/Q-language` (tested with LLVM 20.1.7):

```sh
clang++ -DQ_ENABLE_LLVM Qlanguage.cpp \
  $(llvm-config --cxxflags --ldflags --system-libs --libs core) -o qlang
./qlang --emit-llvm examples/scalars.q > scalars.ll
llvm-as scalars.ll -o /tmp/q-scalars.bc
./qlang --dump-ast examples/scalars.q
```

Both modes accept a source filename or read standard input when it is omitted.
No flag still selects AST output. `--emit-llvm` on an AST-only build reports how
to enable LLVM support. IR goes to standard output; diagnostics go to standard
error. A syntax or code-generation error exits with status 1 and prints no IR,
even if earlier functions were valid. Command-line errors exit with status 2.

## LLVM increment 1: scalar arithmetic

This is one reviewable step toward Chapter 3, with the implementation grouped
in `IRGenerator` inside `Qlanguage.cpp`.

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

`foreign`, calls, pipelines, `!`, comparisons, `%`, and `^` still parse and dump
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

### Check this increment

After building the LLVM-enabled `qlang`, run:

```sh
python3 tests/test_ir.py ./qlang
```

The test requires `llvm-as` and `clang` on `PATH` (overridable with `LLVM_AS` and
`CLANG`). It assembles the IR, compiles it with a small C caller, and checks
numeric results, parameter scope, rejected features, diagnostics, and AST mode.
Temporary build files are cleaned up automatically.

## Small increments toward Python-style ML

Review and commit this scalar IR increment before adding the next feature.
Suggested commit message: `Add LLVM IR emission for scalar arithmetic functions`.

1. **Next:** `foreign` declarations and function calls, including argument-count
   checks and agreement between declarations and definitions.
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
