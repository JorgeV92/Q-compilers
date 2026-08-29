# Q Language 

Building a simple language using llvm c++ componenets. 

We being to a construct of the Q language building a procedural system were we are allowed to define functions, conditionals, math and more as we go along. As we also would like to add `JIT` at the end when we have added `if else for user operators`.  

## Lexer 
We need our Q language to understand given a text file what every symbol and type mean according the denitions inside Q. This is the functionality the lexer will play, where its job is to break the given input into token identifiers. `llvm` provides what that could look like for a given language and what Q can represent for the moment. 

```cpp
enum Token {
    tok_eof = -1,

    // list of commands 
    // expand with more
    tok_def = -2,
    tok_extern = -3,

    // primary  symbols 
    tok_identifier = -4, 
    tok_number = -5,
};

static std::string Identifier_str; 
static double num_val;
```


