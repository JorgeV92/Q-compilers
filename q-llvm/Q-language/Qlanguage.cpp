#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef Q_ENABLE_LLVM
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include <unordered_map>
#endif

// Q's front end remains usable without LLVM. Define Q_ENABLE_LLVM to build
// the first IR increment: scalar arithmetic and single-expression functions.

namespace q {

struct SourceLocation {
  std::size_t line = 1;
  std::size_t column = 1;
};

enum TokenKind {
  tok_eof = -1,

  // Keywords.
  tok_fn = -2,
  tok_foreign = -3,

  // Primary values.
  tok_identifier = -4,
  tok_number = -5,

  // Multi-character punctuation and operators.
  tok_arrow = -6,          // =>
  tok_pipe = -7,           // |>
  tok_equal_equal = -8,    // ==
  tok_bang_equal = -9,     // !=
  tok_less_equal = -10,    // <=
  tok_greater_equal = -11, // >=

  tok_invalid = -12,
};

struct Token {
  int kind = tok_invalid;
  std::string text;
  double number = 0.0;
  SourceLocation location;
};

class Lexer {
public:
  explicit Lexer(std::string source) : source_(std::move(source)) {}

  Token next() {
    skipTrivia();

    const SourceLocation start{line_, column_};
    if (atEnd())
      return makeToken(tok_eof, "", start);

    const char current = peek();

    if (isIdentifierStart(current))
      return lexIdentifier(start);

    if (isDigit(current) || (current == '.' && isDigit(peek(1))))
      return lexNumber(start);

    if (current == '=' && peek(1) == '>')
      return lexPair(tok_arrow, start);
    if (current == '|' && peek(1) == '>')
      return lexPair(tok_pipe, start);
    if (current == '=' && peek(1) == '=')
      return lexPair(tok_equal_equal, start);
    if (current == '!' && peek(1) == '=')
      return lexPair(tok_bang_equal, start);
    if (current == '<' && peek(1) == '=')
      return lexPair(tok_less_equal, start);
    if (current == '>' && peek(1) == '=')
      return lexPair(tok_greater_equal, start);

    // Single-character punctuation is represented by its unsigned ASCII value,
    // just as in the LLVM tutorial.
    const unsigned char punctuation = static_cast<unsigned char>(advance());
    return makeToken(static_cast<int>(punctuation),
                     std::string(1, static_cast<char>(punctuation)), start);
  }

private:
  std::string source_;
  std::size_t index_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;

  bool atEnd() const { return index_ >= source_.size(); }

  char peek(std::size_t offset = 0) const {
    const std::size_t position = index_ + offset;
    return position < source_.size() ? source_[position] : '\0';
  }

  char advance() {
    if (atEnd())
      return '\0';

    const char value = source_[index_++];
    if (value == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
    return value;
  }

  static bool isSpace(char value) {
    return std::isspace(static_cast<unsigned char>(value)) != 0;
  }

  static bool isDigit(char value) {
    return std::isdigit(static_cast<unsigned char>(value)) != 0;
  }

  static bool isIdentifierStart(char value) {
    const unsigned char byte = static_cast<unsigned char>(value);
    return std::isalpha(byte) != 0 || value == '_';
  }

  static bool isIdentifierContinue(char value) {
    const unsigned char byte = static_cast<unsigned char>(value);
    return std::isalnum(byte) != 0 || value == '_';
  }

  void skipTrivia() {
    while (true) {
      while (!atEnd() && isSpace(peek()))
        advance();

      if (peek() != '/' || peek(1) != '/')
        return;

      // Q comments start with // and end immediately before a newline or EOF.
      while (!atEnd() && peek() != '\n')
        advance();
    }
  }

  Token makeToken(int kind, std::string text, SourceLocation location,
                  double number = 0.0) const {
    return Token{kind, std::move(text), number, location};
  }

  Token lexPair(int kind, SourceLocation start) {
    std::string text;
    text += advance();
    text += advance();
    return makeToken(kind, std::move(text), start);
  }

  Token lexIdentifier(SourceLocation start) {
    const std::size_t beginning = index_;
    while (isIdentifierContinue(peek()))
      advance();

    std::string text = source_.substr(beginning, index_ - beginning);
    if (text == "fn")
      return makeToken(tok_fn, std::move(text), start);
    if (text == "foreign")
      return makeToken(tok_foreign, std::move(text), start);
    return makeToken(tok_identifier, std::move(text), start);
  }

  Token lexNumber(SourceLocation start) {
    const std::size_t beginning = index_;

    while (isDigit(peek()))
      advance();

    if (peek() == '.') {
      advance();
      while (isDigit(peek()))
        advance();
    }

    if (peek() == 'e' || peek() == 'E') {
      advance();
      if (peek() == '+' || peek() == '-')
        advance();

      if (!isDigit(peek())) {
        while (isIdentifierContinue(peek()))
          advance();
        return makeToken(tok_invalid,
                         source_.substr(beginning, index_ - beginning), start);
      }

      while (isDigit(peek()))
        advance();
    }

    std::string text = source_.substr(beginning, index_ - beginning);
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size())
      return makeToken(tok_invalid, std::move(text), start);

    return makeToken(tok_number, std::move(text), start, value);
  }
};

//===----------------------------------------------------------------------===//
// Abstract syntax tree
//===----------------------------------------------------------------------===//

static void indent(std::ostream &out, unsigned depth) {
  out << std::string(depth * 2, ' ');
}

static void printLocation(std::ostream &out, SourceLocation location) {
  out << " @ " << location.line << ':' << location.column;
}

class ExprAST {
public:
  explicit ExprAST(SourceLocation location) : location_(location) {}
  virtual ~ExprAST() = default;

  SourceLocation location() const { return location_; }
  virtual void dump(std::ostream &out, unsigned depth) const = 0;

private:
  SourceLocation location_;
};

class NumberExprAST final : public ExprAST {
public:
  NumberExprAST(double value, SourceLocation location)
      : ExprAST(location), value_(value) {}

  double value() const { return value_; }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "Number(" << std::setprecision(15) << value_ << ')';
    printLocation(out, location());
    out << '\n';
  }

private:
  double value_;
};

class NameExprAST final : public ExprAST {
public:
  NameExprAST(std::string name, SourceLocation location)
      : ExprAST(location), name_(std::move(name)) {}

  const std::string &name() const { return name_; }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "Name(" << name_ << ')';
    printLocation(out, location());
    out << '\n';
  }

private:
  std::string name_;
};

class UnaryExprAST final : public ExprAST {
public:
  UnaryExprAST(std::string op, std::unique_ptr<ExprAST> operand,
               SourceLocation location)
      : ExprAST(location), op_(std::move(op)), operand_(std::move(operand)) {}

  const std::string &op() const { return op_; }
  const ExprAST &operand() const { return *operand_; }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "Unary(" << op_ << ')';
    printLocation(out, location());
    out << '\n';
    operand_->dump(out, depth + 1);
  }

private:
  std::string op_;
  std::unique_ptr<ExprAST> operand_;
};

class BinaryExprAST final : public ExprAST {
public:
  BinaryExprAST(std::string op, std::unique_ptr<ExprAST> left,
                std::unique_ptr<ExprAST> right, SourceLocation location)
      : ExprAST(location), op_(std::move(op)), left_(std::move(left)),
        right_(std::move(right)) {}

  const std::string &op() const { return op_; }
  const ExprAST &left() const { return *left_; }
  const ExprAST &right() const { return *right_; }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "Binary(" << op_ << ')';
    printLocation(out, location());
    out << '\n';
    left_->dump(out, depth + 1);
    right_->dump(out, depth + 1);
  }

private:
  std::string op_;
  std::unique_ptr<ExprAST> left_;
  std::unique_ptr<ExprAST> right_;
};

// A pipeline is a first-class Q expression, not just a generic binary operator.
// A future semantic pass will lower `value |> f(x)` to `f(value, x)`.
class PipelineExprAST final : public ExprAST {
public:
  PipelineExprAST(std::unique_ptr<ExprAST> value,
                  std::unique_ptr<ExprAST> destination, SourceLocation location)
      : ExprAST(location), value_(std::move(value)),
        destination_(std::move(destination)) {}

  const ExprAST &value() const { return *value_; }
  const ExprAST &destination() const { return *destination_; }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "Pipeline";
    printLocation(out, location());
    out << '\n';
    value_->dump(out, depth + 1);
    destination_->dump(out, depth + 1);
  }

private:
  std::unique_ptr<ExprAST> value_;
  std::unique_ptr<ExprAST> destination_;
};

class CallExprAST final : public ExprAST {
public:
  CallExprAST(std::string callee,
              std::vector<std::unique_ptr<ExprAST>> arguments,
              SourceLocation location)
      : ExprAST(location), callee_(std::move(callee)),
        arguments_(std::move(arguments)) {}

  const std::string &callee() const { return callee_; }
  const std::vector<std::unique_ptr<ExprAST>> &arguments() const {
    return arguments_;
  }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "Call(" << callee_ << ')';
    printLocation(out, location());
    out << '\n';
    for (const auto &argument : arguments_)
      argument->dump(out, depth + 1);
  }

private:
  std::string callee_;
  std::vector<std::unique_ptr<ExprAST>> arguments_;
};

class PrototypeAST {
public:
  PrototypeAST(std::string name, std::vector<std::string> parameters,
               SourceLocation location)
      : name_(std::move(name)), parameters_(std::move(parameters)),
        location_(location) {}

  const std::string &name() const { return name_; }
  const std::vector<std::string> &parameters() const { return parameters_; }
  SourceLocation location() const { return location_; }

  void dump(std::ostream &out, unsigned depth) const {
    indent(out, depth);
    out << "Prototype(" << name_;
    for (const std::string &parameter : parameters_)
      out << ' ' << parameter;
    out << ')';
    printLocation(out, location_);
    out << '\n';
  }

private:
  std::string name_;
  std::vector<std::string> parameters_;
  SourceLocation location_;
};

class TopLevelAST {
public:
  explicit TopLevelAST(SourceLocation location) : location_(location) {}
  virtual ~TopLevelAST() = default;

  SourceLocation location() const { return location_; }
  virtual void dump(std::ostream &out, unsigned depth) const = 0;

private:
  SourceLocation location_;
};

class FunctionAST final : public TopLevelAST {
public:
  FunctionAST(std::unique_ptr<PrototypeAST> prototype,
              std::unique_ptr<ExprAST> body, SourceLocation location)
      : TopLevelAST(location), prototype_(std::move(prototype)),
        body_(std::move(body)) {}

  const PrototypeAST &prototype() const { return *prototype_; }
  const ExprAST &body() const { return *body_; }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "Function";
    printLocation(out, location());
    out << '\n';
    prototype_->dump(out, depth + 1);
    body_->dump(out, depth + 1);
  }

private:
  std::unique_ptr<PrototypeAST> prototype_;
  std::unique_ptr<ExprAST> body_;
};

class ForeignAST final : public TopLevelAST {
public:
  ForeignAST(std::unique_ptr<PrototypeAST> prototype, SourceLocation location)
      : TopLevelAST(location), prototype_(std::move(prototype)) {}

  const PrototypeAST &prototype() const { return *prototype_; }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "Foreign";
    printLocation(out, location());
    out << '\n';
    prototype_->dump(out, depth + 1);
  }

private:
  std::unique_ptr<PrototypeAST> prototype_;
};

class TopLevelExprAST final : public TopLevelAST {
public:
  TopLevelExprAST(std::unique_ptr<ExprAST> expression, SourceLocation location)
      : TopLevelAST(location), expression_(std::move(expression)) {}

  const ExprAST &expression() const { return *expression_; }

  void dump(std::ostream &out, unsigned depth) const override {
    indent(out, depth);
    out << "TopLevelExpression";
    printLocation(out, location());
    out << '\n';
    expression_->dump(out, depth + 1);
  }

private:
  std::unique_ptr<ExprAST> expression_;
};

class ProgramAST {
public:
  void add(std::unique_ptr<TopLevelAST> item) {
    items_.push_back(std::move(item));
  }

  const std::vector<std::unique_ptr<TopLevelAST>> &items() const {
    return items_;
  }

  void dump(std::ostream &out) const {
    out << "Program\n";
    for (const auto &item : items_)
      item->dump(out, 1);
  }

private:
  std::vector<std::unique_ptr<TopLevelAST>> items_;
};

#ifdef Q_ENABLE_LLVM
//===----------------------------------------------------------------------===//
// LLVM IR, increment 1: scalar arithmetic functions
//===----------------------------------------------------------------------===//

// Use the AST's existing accessors so parsing and dumping stay LLVM-independent.
class IRGenerator {
public:
  bool emit(const ProgramAST &program) {
    std::size_t expressionIndex = 0;
    for (const auto &item : program.items()) {
      if (const auto *function = dynamic_cast<const FunctionAST *>(item.get())) {
        if (!emitFunction(function->prototype(), function->body()))
          return false;
      } else if (const auto *expression =
                     dynamic_cast<const TopLevelExprAST *>(item.get())) {
        // A dot cannot occur in a Q identifier, preventing user-name collisions.
        PrototypeAST prototype("__q_expr." + std::to_string(expressionIndex++),
                               {}, expression->location());
        if (!emitFunction(prototype, expression->expression()))
          return false;
      } else {
        error(item->location(),
              "LLVM IR for 'foreign' is not supported in this increment");
        return false;
      }
    }

    // Never print a partial module when parsing or code generation fails.
    if (llvm::verifyModule(module_, &llvm::errs()))
      return false;
    module_.print(llvm::outs(), nullptr);
    return true;
  }

private:
  llvm::LLVMContext context_;
  llvm::Module module_{"q", context_};
  llvm::IRBuilder<> builder_{context_};
  std::unordered_map<std::string, llvm::Value *> namedValues_;

  llvm::Value *error(SourceLocation location, const std::string &message) {
    std::cerr << location.line << ':' << location.column
              << ": error: " << message << '\n';
    return nullptr;
  }

  llvm::Value *emitExpression(const ExprAST &expression) {
    if (const auto *number = dynamic_cast<const NumberExprAST *>(&expression))
      return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_),
                                  number->value());

    if (const auto *name = dynamic_cast<const NameExprAST *>(&expression)) {
      const auto found = namedValues_.find(name->name());
      if (found == namedValues_.end())
        return error(name->location(), "unknown name '" + name->name() + "'");
      return found->second;
    }

    if (const auto *unary = dynamic_cast<const UnaryExprAST *>(&expression)) {
      if (unary->op() != "+" && unary->op() != "-")
        return error(unary->location(),
                     "LLVM IR for unary operator '" + unary->op() +
                         "' is not supported in this increment");
      llvm::Value *operand = emitExpression(unary->operand());
      if (!operand)
        return nullptr;
      return unary->op() == "+" ? operand : builder_.CreateFNeg(operand, "neg");
    }

    if (const auto *binary = dynamic_cast<const BinaryExprAST *>(&expression)) {
      const std::string &op = binary->op();
      if (op != "+" && op != "-" && op != "*" && op != "/")
        return error(binary->location(),
                     "LLVM IR for binary operator '" + op +
                         "' is not supported in this increment");
      llvm::Value *left = emitExpression(binary->left());
      if (!left)
        return nullptr;
      llvm::Value *right = emitExpression(binary->right());
      if (!right)
        return nullptr;
      if (op == "+")
        return builder_.CreateFAdd(left, right, "add");
      if (op == "-")
        return builder_.CreateFSub(left, right, "sub");
      if (op == "*")
        return builder_.CreateFMul(left, right, "mul");
      return builder_.CreateFDiv(left, right, "div");
    }

    return error(expression.location(),
                 "LLVM IR for calls and pipelines is not supported in this increment");
  }

  bool emitFunction(const PrototypeAST &prototype, const ExprAST &body) {
    if (module_.getFunction(prototype.name())) {
      error(prototype.location(), "duplicate function '" + prototype.name() + "'");
      return false;
    }

    llvm::Type *numberType = llvm::Type::getDoubleTy(context_);
    std::vector<llvm::Type *> parameters(prototype.parameters().size(), numberType);
    auto *type = llvm::FunctionType::get(numberType, parameters, false);
    auto *function = llvm::Function::Create(type, llvm::Function::ExternalLinkage,
                                          prototype.name(), module_);
    namedValues_.clear();
    std::size_t index = 0;
    for (auto &argument : function->args()) {
      const std::string &name = prototype.parameters()[index++];
      argument.setName(name);
      namedValues_[name] = &argument;
    }

    builder_.SetInsertPoint(llvm::BasicBlock::Create(context_, "entry", function));
    llvm::Value *result = emitExpression(body);
    if (!result)
      return false;
    builder_.CreateRet(result);
    return !llvm::verifyFunction(*function, &llvm::errs());
  }
};
#endif

//===----------------------------------------------------------------------===//
// Recursive-descent and precedence-climbing parser
//===----------------------------------------------------------------------===//

class Parser {
public:
  explicit Parser(std::string source) : lexer_(std::move(source)) { advance(); }

  std::unique_ptr<ProgramAST> parseProgram() {
    auto program = std::make_unique<ProgramAST>();

    while (current_.kind != tok_eof) {
      if (current_.kind == ';') {
        advance();
        continue;
      }

      std::unique_ptr<TopLevelAST> item;
      if (current_.kind == tok_fn)
        item = parseFunction();
      else if (current_.kind == tok_foreign)
        item = parseForeign();
      else
        item = parseTopLevelExpression();

      if (!item) {
        synchronize();
        continue;
      }

      program->add(std::move(item));
      if (current_.kind != ';') {
        report(current_.location, "expected ';' after top-level item");
        synchronize();
        continue;
      }
      advance();
    }

    return program;
  }

  bool hasErrors() const { return !diagnostics_.empty(); }

  const std::vector<std::string> &diagnostics() const { return diagnostics_; }

private:
  Lexer lexer_;
  Token current_;
  std::vector<std::string> diagnostics_;

  void advance() { current_ = lexer_.next(); }

  void report(SourceLocation location, const std::string &message) {
    std::ostringstream diagnostic;
    diagnostic << location.line << ':' << location.column
               << ": error: " << message;
    if (current_.kind != tok_eof && !current_.text.empty())
      diagnostic << " (found '" << current_.text << "')";
    diagnostics_.push_back(diagnostic.str());
  }

  void synchronize() {
    while (current_.kind != tok_eof && current_.kind != ';')
      advance();
    if (current_.kind == ';')
      advance();
  }

  std::unique_ptr<TopLevelAST> parseFunction() {
    const SourceLocation start = current_.location;
    advance(); // consume fn

    auto prototype = parsePrototype();
    if (!prototype)
      return nullptr;

    if (current_.kind != tok_arrow) {
      report(current_.location, "expected '=>' before function body");
      return nullptr;
    }
    advance();

    auto body = parseExpression();
    if (!body)
      return nullptr;

    return std::make_unique<FunctionAST>(std::move(prototype), std::move(body),
                                         start);
  }

  std::unique_ptr<TopLevelAST> parseForeign() {
    const SourceLocation start = current_.location;
    advance(); // consume foreign

    auto prototype = parsePrototype();
    if (!prototype)
      return nullptr;
    return std::make_unique<ForeignAST>(std::move(prototype), start);
  }

  std::unique_ptr<TopLevelAST> parseTopLevelExpression() {
    const SourceLocation start = current_.location;
    auto expression = parseExpression();
    if (!expression)
      return nullptr;
    return std::make_unique<TopLevelExprAST>(std::move(expression), start);
  }

  std::unique_ptr<PrototypeAST> parsePrototype() {
    if (current_.kind != tok_identifier) {
      report(current_.location, "expected a function name");
      return nullptr;
    }

    const SourceLocation start = current_.location;
    std::string name = current_.text;
    advance();

    if (current_.kind != '(') {
      report(current_.location, "expected '(' after function name");
      return nullptr;
    }
    advance();

    std::vector<std::string> parameters;
    std::unordered_set<std::string> seenParameters;
    if (current_.kind != ')') {
      while (true) {
        if (current_.kind != tok_identifier) {
          report(current_.location, "expected a parameter name");
          return nullptr;
        }

        if (!seenParameters.insert(current_.text).second) {
          report(current_.location,
                 "duplicate parameter '" + current_.text + "'");
          return nullptr;
        }
        parameters.push_back(current_.text);
        advance();

        if (current_.kind == ')')
          break;
        if (current_.kind != ',') {
          report(current_.location, "expected ',' or ')' after parameter name");
          return nullptr;
        }
        advance();
      }
    }

    advance(); // consume )
    return std::make_unique<PrototypeAST>(std::move(name),
                                          std::move(parameters), start);
  }

  std::unique_ptr<ExprAST> parseExpression(int minimumPrecedence = 0) {
    auto left = parseUnary();
    if (!left)
      return nullptr;

    while (true) {
      const int precedence = currentPrecedence();
      if (precedence < minimumPrecedence)
        return left;

      const Token op = current_;
      const bool rightAssociative = op.text == "^";
      advance();

      auto right = parseExpression(precedence + (rightAssociative ? 0 : 1));
      if (!right)
        return nullptr;

      if (op.kind == tok_pipe) {
        left = std::make_unique<PipelineExprAST>(std::move(left),
                                                 std::move(right), op.location);
      } else {
        left = std::make_unique<BinaryExprAST>(op.text, std::move(left),
                                               std::move(right), op.location);
      }
    }
  }

  std::unique_ptr<ExprAST> parseUnary() {
    if (current_.kind != '+' && current_.kind != '-' && current_.kind != '!')
      return parsePrimary();

    const Token op = current_;
    advance();
    auto operand = parseUnary();
    if (!operand)
      return nullptr;
    return std::make_unique<UnaryExprAST>(op.text, std::move(operand),
                                          op.location);
  }

  std::unique_ptr<ExprAST> parsePrimary() {
    switch (current_.kind) {
    case tok_identifier:
      return parseIdentifierExpression();
    case tok_number:
      return parseNumberExpression();
    case '(':
      return parseParenthesizedExpression();
    case tok_invalid:
      report(current_.location, "invalid token");
      return nullptr;
    default:
      report(current_.location, "expected an expression");
      return nullptr;
    }
  }

  std::unique_ptr<ExprAST> parseNumberExpression() {
    auto result =
        std::make_unique<NumberExprAST>(current_.number, current_.location);
    advance();
    return result;
  }

  std::unique_ptr<ExprAST> parseParenthesizedExpression() {
    advance(); // consume (
    auto expression = parseExpression();
    if (!expression)
      return nullptr;

    if (current_.kind != ')') {
      report(current_.location, "expected ')'");
      return nullptr;
    }
    advance();
    return expression;
  }

  std::unique_ptr<ExprAST> parseIdentifierExpression() {
    const SourceLocation start = current_.location;
    std::string name = current_.text;
    advance();

    if (current_.kind != '(')
      return std::make_unique<NameExprAST>(std::move(name), start);

    advance(); // consume (
    std::vector<std::unique_ptr<ExprAST>> arguments;
    if (current_.kind != ')') {
      while (true) {
        auto argument = parseExpression();
        if (!argument)
          return nullptr;
        arguments.push_back(std::move(argument));

        if (current_.kind == ')')
          break;
        if (current_.kind != ',') {
          report(current_.location,
                 "expected ',' or ')' after function argument");
          return nullptr;
        }
        advance();
      }
    }

    advance(); // consume )
    return std::make_unique<CallExprAST>(std::move(name), std::move(arguments),
                                         start);
  }

  int currentPrecedence() const {
    if (current_.kind == tok_pipe)
      return 5;
    if (current_.kind == tok_equal_equal || current_.kind == tok_bang_equal)
      return 10;
    if (current_.kind == '<' || current_.kind == '>' ||
        current_.kind == tok_less_equal || current_.kind == tok_greater_equal)
      return 20;
    if (current_.kind == '+' || current_.kind == '-')
      return 30;
    if (current_.kind == '*' || current_.kind == '/' || current_.kind == '%')
      return 40;
    if (current_.kind == '^')
      return 50;
    return -1;
  }
};

} // namespace q

static bool readSource(const char *filename, std::string &source) {
  std::ostringstream buffer;
  if (filename) {
    std::ifstream input(filename);
    if (!input) {
      std::cerr << "error: could not open '" << filename << "'\n";
      return false;
    }
    buffer << input.rdbuf();
  } else {
    buffer << std::cin.rdbuf();
  }

  source = buffer.str();
  return true;
}

int main(int argc, char **argv) {
  const bool emitLLVM = argc > 1 && std::string(argv[1]) == "--emit-llvm";
  const bool dumpAST = argc > 1 && std::string(argv[1]) == "--dump-ast";
  const int sourceIndex = (emitLLVM || dumpAST) ? 2 : 1;
  if (argc > sourceIndex + 1 ||
      (argc > sourceIndex && argv[sourceIndex][0] == '-')) {
    std::cerr << "usage: " << argv[0]
              << " [--dump-ast | --emit-llvm] [source.q]\n";
    return 2;
  }
#ifndef Q_ENABLE_LLVM
  if (emitLLVM) {
    std::cerr << "error: --emit-llvm requires a build with Q_ENABLE_LLVM; see QL.md\n";
    return 2;
  }
#endif

  std::string source;
  if (!readSource(argc > sourceIndex ? argv[sourceIndex] : nullptr, source))
    return 2;

  q::Parser parser(std::move(source));
  auto program = parser.parseProgram();

  if (parser.hasErrors()) {
    for (const std::string &diagnostic : parser.diagnostics())
      std::cerr << diagnostic << '\n';
    return 1;
  }

#ifdef Q_ENABLE_LLVM
  if (emitLLVM) {
    q::IRGenerator generator;
    return generator.emit(*program) ? 0 : 1;
  }
#endif
  program->dump(std::cout);
  return 0;
}
