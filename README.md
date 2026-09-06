# Q

A small language and compiler you can read in one C++ file. Q currently turns
scalar arithmetic and function calls into LLVM IR, with a longer-term goal of
a Python-style language for machine learning.

```q
fn square(x) => x * x;
fn loss(prediction, target) => square(prediction - target);
loss(7, 4);
```

## Quick start

From the repository root, with a C++17 compiler, Make, and LLVM development
tools on `PATH` (tested with LLVM 20.1.7):

```sh
make
./build/qlang --emit-llvm examples/calls.q > build/calls.ll
llvm-as build/calls.ll -o build/calls.bc
```

This emits and verifies IR. The compiler does not execute the program yet.
Inspect its syntax tree with `./build/qlang --dump-ast examples/calls.q`.

To explore the lexer and parser with just a C++17 compiler:

```sh
make ast
./build/qlang-ast examples/calls.q
```

Both binaries read standard input when no source filename is given.

## Tests

```sh
make test
```

This builds the LLVM-enabled compiler, assembles its output, links a small C
caller, and checks numeric results and diagnostics. Tests use Python 3 and
Clang. `llvm-as` is selected from the same installation as `llvm-config`.

Tool paths are configurable, for example:

```sh
make test LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config
```

`CXX`, `CXXFLAGS`, `PYTHON`, `CLANG`, and `LLVM_AS` can also be set on the Make
command line. `make clean` removes the two compiler binaries.

## Layout

```text
.
├── README.md          # Start here
├── Makefile           # Build and test commands
├── src/
│   └── qlang.cpp      # Lexer, AST, IR generator, parser, and CLI
├── examples/
│   ├── scalars.q      # Arithmetic building blocks
│   └── calls.q        # Composing functions
├── tests/
│   └── test_ir.py     # IR and native execution checks
└── docs/
    ├── QL.md          # Language guide and compiler walkthrough
    └── llvm.md        # LLVM learning references
```

Generated binaries and IR belong in `build/`, which Git ignores.

## Learn and extend

Read [the language guide](docs/QL.md) for syntax, supported operations, IR
examples, and the next small increments. [LLVM notes](docs/llvm.md) link to
the tutorials behind the implementation.

The next feature is `foreign` declarations. Pipelines, Python-style blocks,
and tensor support are later steps. Each feature should stay small enough to
review with its example, tests, and documentation.
