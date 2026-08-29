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

The lexer will provide token based on the provided enm choices or a undefined token. We can start with the following function `gettok()`, which will return the following token from standard input. 

```cpp
static int gettok() {
    static int last_char = ' ';
    while (issapce(last_char)) last_char = getchar();
}
```

We utilize the C function `getchar()` to read input one at a time from standard input. As it reads characters we look for spaces in between characters and skip them before processing symbpls. As `gettok` reads symbols it has to recognize the identifiers and special keywords like `def` .

```cpp 
if (isalpha(last_char)) { // where identifier: [a-zA-Z][a-zA-Z0-9]*
    Identifier_str = last_char;
    while (isalnum(last_char = getchar())) Identifier_str += last_char;
    if (Identifier_str == "def") return tok_def;
    if (Identifier_str == "extern") return tok_extern;
    return tok_identifier;
}
```

For handling numeric values we can start Q with:

```cpp
if (isdigit(last_char) || last_char == '.') { // Number [0-9]+ 
    std::string num_str;
    do {
        num_str += last_char;
        last_char = getchar();
    } while (isdigit(last_char) || last_char == '.');
    num_val = strtod(num_str.c_str(), 0);
    return tok_num
}
```

We use the C function `strtod` to convert a string to numeric value and assign it to `num_val`. We also need to consider errors that we will do later and now process comments.

We can just skip till end of the line and return the following token. 

```cpp
if (last_char == EOF) return tok_eof;

// ow return ascii value
int ThisChar = last_char;
last_char = getchar();
return ThisChar;
```

## AST 