# The Q language

Q is a small expression-oriented language built while working through LLVM's
"My First Language Frontend" tutorial. It uses the same useful compiler
architecture as Kaleidoscope—lexer, parser, AST, LLVM IR, and eventually a
JIT—but it has its own source syntax and room for its own semantics.

1. The **lexer** turns source text into tokens.
2. The **parser** checks the token sequence using Q's grammar.
3. The **AST** records the meaning and structure of a valid Q program.

LLVM is intentionally not used yet. LLVM enters at the next checkpoint, when
the AST nodes learn how to generate LLVM IR.

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

At this checkpoint the parser records that structure, but no expression is
executed yet.

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

Every node also keeps its starting source location. The current `dump()` methods
are a temporary but useful stand-in for the LLVM `codegen()` methods added in
the next stage.

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

The current checkpoint needs only a C++17 compiler; it does not need LLVM
libraries yet.

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

## Next checkpoint

The next LLVM tutorial stage can be adapted by adding a `codegen()` operation to
the expression and top-level AST nodes. That stage should also define Q's
numeric and truth-value semantics and lower `PipelineExprAST` into ordinary
function calls before emitting LLVM IR.

Reference: [LLVM tutorial, Chapter 2: Implementing a Parser and AST](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl02.html).
