# LLVM notes

Learning references for Q's lexer, parser, and LLVM IR generator. The compiler
lives in [src/qlang.cpp](../src/qlang.cpp); its walkthrough is in [QL.md](QL.md).

- [My First Language Frontend](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)
- [Chapter 2: Parser and AST](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl02.html)
- [Chapter 3: Code generation to LLVM IR](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html)
- [Chapter 3 for LLVM 20](https://releases.llvm.org/20.1.0/docs/tutorial/MyFirstLanguageFrontend/LangImpl03.html)

Use the tutorial version matching your installed LLVM release. The current
build and tests have been checked with LLVM 20.1.7.
