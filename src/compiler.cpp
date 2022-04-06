/*
 *  The Grace Programming Language.
 *
 *  This file contains the out of line definitions for the Compiler class, which outputs Grace bytecode based on Tokens provided by the Scanner. 
 *  
 *  Copyright (c) 2022 - Present, Ryan Jeffares.
 *  All rights reserved.
 *
 *  For licensing information, see grace.hpp
 */

#include <chrono>
#include <variant>

#include "compiler.hpp"

using namespace Grace::Scanner;
using namespace Grace::VM;

void Grace::Compiler::Compile(std::string&& fileName, std::string&& code, bool verbose)
{
  using namespace std::chrono;

  auto start = steady_clock::now();
 
  Compiler compiler(std::move(fileName), std::move(code), verbose);
  compiler.Advance();
  
  while (!compiler.Match(TokenType::EndOfFile)) {
    compiler.Declaration();
    if (compiler.HadError()) {
      break;
    }
  }

  if (compiler.HadError()) {
    fmt::print(stderr, "Terminating process due to compilation errors.\n");
  } else {
    if (verbose) {
      auto end = steady_clock::now();
      auto duration = duration_cast<microseconds>(end - start).count();
      fmt::print("Compilation succeeded in {} μs.\n", duration);
    }
    compiler.Finalise(verbose);
  }
}

using namespace Grace::Compiler;

Compiler::Compiler(std::string&& fileName, std::string&& code, bool verbose) 
  : m_Scanner(std::move(code)), 
  m_CurrentFileName(std::move(fileName)),
  m_CurrentContext(Context::TopLevel),
  m_Vm(*this),
  m_Verbose(verbose)
{

}

void Compiler::Finalise(bool verbose)
{
#ifdef GRACE_DEBUG
  if (verbose) {
    m_Vm.PrintOps();
  }
#endif
   m_Vm.Start(verbose);
}

void Compiler::Advance()
{
  m_Previous = m_Current;
  m_Current = m_Scanner.ScanToken();

#ifdef GRACE_DEBUG
  if (m_Verbose) {
    fmt::print("{}\n", m_Current.value().ToString());
  }
#endif 

  if (m_Current.value().GetType() == TokenType::Error) {
    MessageAtCurrent("Unexpected token", LogLevel::Error);
  }
}

bool Compiler::Match(TokenType type)
{
  if (!Check(type)) {
    return false;
  }

  Advance();
  return true;
}

bool Compiler::Check(TokenType type) const
{
  return m_Current.has_value() && m_Current.value().GetType() == type;
}

void Compiler::Consume(TokenType expected, const std::string& message)
{
  if (m_Current.value().GetType() == expected) {
    Advance();
    return;
  }

  MessageAtCurrent(message, LogLevel::Error);
}

void Compiler::Synchronize()
{
  m_PanicMode = false;

  while (m_Current.value().GetType() != TokenType::EndOfFile) {
    if (m_Previous.has_value() && m_Previous.value().GetType() == TokenType::Semicolon) {
      return;
    }

    switch (m_Current.value().GetType()) {
      case TokenType::Class:
      case TokenType::Func:
      case TokenType::Final:
      case TokenType::For:
      case TokenType::If:
      case TokenType::While:
      case TokenType::Print:
      case TokenType::PrintLn:
      case TokenType::Return:
      case TokenType::Var:
        return;
      default:
        break;
    }

    Advance();
  }
}

static bool IsKeyword(TokenType type, std::string& outKeyword)
{
  switch (type) {
    case TokenType::And: outKeyword = "and"; return true;
    case TokenType::Class: outKeyword = "class"; return true;
    case TokenType::End: outKeyword = "end"; return true;
    case TokenType::Final: outKeyword = "final"; return true;
    case TokenType::For: outKeyword = "for"; return true;
    case TokenType::Func: outKeyword = "func"; return true;
    case TokenType::If: outKeyword = "if"; return true;
    case TokenType::Or: outKeyword = "or"; return true;
    case TokenType::Print: outKeyword = "print"; return true;
    case TokenType::PrintLn: outKeyword = "println"; return true;
    case TokenType::Return: outKeyword = "return"; return true;
    case TokenType::Var: outKeyword = "var"; return true;
    case TokenType::While: outKeyword = "while"; return true;
    default:
      return false;
  }
}

static bool IsOperator(TokenType type)
{
  static const std::vector<TokenType> symbols = {
    TokenType::Colon,
    TokenType::Semicolon,
    TokenType::RightParen,
    TokenType::Comma,
    TokenType::Dot,
    TokenType::Plus,
    TokenType::Slash,
    TokenType::Star,
    TokenType::StarStar,
    TokenType::BangEqual,
    TokenType::Equal,
    TokenType::EqualEqual,
    TokenType::LessThan,
    TokenType::GreaterThan,
    TokenType::LessEqual,
    TokenType::GreaterEqual,
  };

  return std::any_of(symbols.begin(), symbols.end(), [type](TokenType t) {
      return t == type;
  });
}

void Compiler::EmitOp(VM::Ops op, int line)
{
  m_Vm.PushOp(op, line);
}

void Compiler::Declaration()
{
  if (Match(TokenType::Class)) {
    ClassDeclaration();
  } else if (Match(TokenType::Func)) {
    FuncDeclaration();
  } else if (Match(TokenType::Var)) {
    VarDeclaration();
  } else if (Match(TokenType::Final)) {
    FinalDeclaration();
  } else if (Match(TokenType::Break)) {
    if (m_CurrentContext != Context::Loop) {
      // don't return early from here, so the compiler can synchronize...
      MessageAtPrevious("`break` only allowed inside `for` and `while` loops", LogLevel::Error);
    } else {
      m_BreakJumpNeedsIndexes = true;
      auto constIdx = static_cast<std::int64_t>(m_Vm.GetNumConstants());
      EmitConstant(std::int64_t{});
      auto opIdx = static_cast<std::int64_t>(m_Vm.GetNumConstants());
      EmitConstant(std::int64_t{});
      EmitOp(Ops::Jump, m_Previous.value().GetLine());
      m_BreakIdxPairs.top().push_back(std::make_pair(constIdx, opIdx));
      Consume(TokenType::Semicolon, "Expected ';' after `break`");
    }
  } else if (Match(TokenType::Assert)) {
    auto line = m_Previous.value().GetLine();

    Consume(TokenType::LeftParen, "Expected '(' after `assert`");
    m_ShouldNotPopValue = true; 
    Expression(false);
    m_ShouldNotPopValue = false; 

    if (Match(TokenType::Comma)) {
      Consume(TokenType::String, "Expected message");
      EmitConstant(std::string(m_Previous.value().GetText()));
      EmitOp(Ops::AssertWithMessage, line);
      Consume(TokenType::RightParen, "Expected ')'");
    } else {
      EmitOp(Ops::Assert, line); 
      Consume(TokenType::RightParen, "Expected ')'");
    }

    Consume(TokenType::Semicolon, "Expected ';' after `assert` expression");
  } else {
    Statement();
  }
 
  if (m_PanicMode) {
    Synchronize();
  }
}

void Compiler::Statement()
{
  if (m_CurrentContext == Context::TopLevel) {
    MessageAtCurrent("Only functions and classes are allowed at top level", LogLevel::Error);
    return;
  }

  if (Match(TokenType::For)) {
    ForStatement();
  } else if (Match(TokenType::If)) {
    IfStatement();
  } else if (Match(TokenType::Print)) {
    PrintStatement();
  } else if (Match(TokenType::PrintLn)) {
    PrintLnStatement();
  } else if (Match(TokenType::Return)) {
    ReturnStatement();
  } else if (Match(TokenType::While)) {
    WhileStatement();
  } else {
    ExpressionStatement();
  }
}

void Compiler::ClassDeclaration() 
{
  GRACE_NOT_IMPLEMENTED();  
}

void Compiler::FuncDeclaration() 
{
  auto previous = m_CurrentContext;
  m_CurrentContext = Context::Function;  

  Consume(TokenType::Identifier, "Expected function name");
  auto name = std::string(m_Previous.value().GetText());

  Consume(TokenType::LeftParen, "Expected '(' after function name");

  std::vector<std::string> parameters;
  while (!Match(TokenType::RightParen)) {
    if (Match(TokenType::Final)) {
      Consume(TokenType::Identifier, "Expected identifier after `final`");
      auto p = std::string(m_Previous.value().GetText());
      if (std::find(parameters.begin(), parameters.end(), p) != parameters.end()) {
        MessageAtPrevious("Function parameters with the same name already defined", LogLevel::Error);
        return;
      }
      m_Locals.insert(std::make_pair(p, std::make_pair(true, m_Locals.size())));
      parameters.push_back(p);
    } else if (Match(TokenType::Identifier)) {
      auto p = std::string(m_Previous.value().GetText());
      if (std::find(parameters.begin(), parameters.end(), p) != parameters.end()) {
        MessageAtPrevious("Function parameters with the same name already defined", LogLevel::Error);
        return;
      }
      m_Locals.insert(std::make_pair(p, std::make_pair(false, m_Locals.size())));
      parameters.push_back(p);
    } else {
      if (!Match(TokenType::Comma)) {
        MessageAtCurrent("Expected ',' after function parameter", LogLevel::Error);
        return;
      }
    }
  }

  Consume(TokenType::Colon, "Expected ':' after function signature");

  if (!m_Vm.AddFunction(name, m_Previous.value().GetLine(), parameters.size())) {
    MessageAtPrevious("Duplicate function definitions", LogLevel::Error);
    return;
  }

  m_FunctionHadReturn = false;
  while (!Match(TokenType::End)) {
    Declaration();
    if (m_Current.value().GetType() == TokenType::EndOfFile) {
      MessageAtCurrent("Expected `end` after function", LogLevel::Error);
      return;
    }
  }

  // implicitly return if the user didnt write a return so the VM knows to return to the caller
  if (!m_FunctionHadReturn && m_Vm.GetLastFunctionName() != "main") {
    EmitConstant((void*)nullptr);
    EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
    EmitOp(Ops::Return, m_Previous.value().GetLine());
  }
  
  m_Locals.clear();
  m_CurrentContext = previous;
}

void Compiler::VarDeclaration() 
{
  if (m_CurrentContext == Context::TopLevel) {
    MessageAtPrevious("Only functions and classes are allowed at top level", LogLevel::Error);
    return;
  }

  Consume(TokenType::Identifier, "Expected identifier after `var`");
  if (m_Locals.find(std::string(m_Previous.value().GetText())) != m_Locals.end()) {
    MessageAtPrevious("A local variable with the same name already exists", LogLevel::Error);
    return;
  }

  std::string localName(m_Previous.value().GetText());
  int line = m_Previous.value().GetLine();

  std::int64_t localId = m_Locals.size();
  m_Locals.insert(std::make_pair(localName, std::make_pair(false, localId)));
  EmitOp(Ops::DeclareLocal, line);

  if (Match(TokenType::Equal)) {
    m_ShouldNotPopValue = true;
    Expression(false);
    m_ShouldNotPopValue = false;
    line = m_Previous.value().GetLine();
    EmitConstant(localId);
    EmitOp(Ops::AssignLocal, line);
  }
  Consume(TokenType::Semicolon, "Expected ';' after `var` declaration");
}

void Compiler::FinalDeclaration() 
{
  if (m_CurrentContext == Context::TopLevel) {
    MessageAtPrevious("Only functions and classes are allowed at top level", LogLevel::Error);
    return;
  } 

  Consume(TokenType::Identifier, "Expected identifier after `final`");

  if (m_Locals.find(std::string(m_Previous.value().GetText())) != m_Locals.end()) {
    MessageAtPrevious("A local variable with the same name already exists", LogLevel::Error);
    return;
  }

  std::string localName(m_Previous.value().GetText());
  int line = m_Previous.value().GetLine();

  auto localId = static_cast<std::int64_t>(m_Locals.size());
  m_Locals.insert(std::make_pair(localName, std::make_pair(true, localId)));
  EmitOp(Ops::DeclareLocal, line);

  Consume(TokenType::Equal, "Must assign to `final` upon declaration");
  m_ShouldNotPopValue = true;
  Expression(false);
  m_ShouldNotPopValue = false;
  line = m_Previous.value().GetLine();
  EmitConstant(localId);
  EmitOp(Ops::AssignLocal, line);

  Consume(TokenType::Semicolon, "Expected ';' after `final` declaration");
}

void Compiler::Expression(bool canAssign)
{
  if (IsOperator(m_Current.value().GetType())) {
    MessageAtCurrent("Expected identifier or literal at start of expression", LogLevel::Error);
    Advance();
    return;
  }

  std::string kw;
  if (IsKeyword(m_Current.value().GetType(), kw)) {
    MessageAtCurrent(fmt::format("'{}' is a keyword and not valid in this context", kw), LogLevel::Error);
    Advance();  //consume the illegal identifier
    return;
  }

  if (Check(TokenType::Identifier)) {
    Call(canAssign);
    if (Check(TokenType::Equal)) {
      if (m_Previous.value().GetType() != TokenType::Identifier) {
        MessageAtCurrent("Only identifiers can be assigned to", LogLevel::Error);
        return;
      }

      auto localName = std::string(m_Previous.value().GetText());
      if (m_Locals.find(localName) == m_Locals.end()) {
        MessageAtPrevious(fmt::format("Cannot find variable '{}' in this scope", localName), LogLevel::Error);
        return;
      }

      if (m_Locals.at(localName).first) {
        MessageAtPrevious(fmt::format("Cannot reassign to final '{}'", m_Previous.value().GetText()), LogLevel::Error);
        return;
      }

      Advance();  // consume the equals

      if (!canAssign) {
        MessageAtCurrent("Assignment is not valid in the current context", LogLevel::Error);
        return;
      }

      m_ShouldNotPopValue = true;
      Expression(false); // disallow x = y = z...
      m_ShouldNotPopValue = false;

      int line = m_Previous.value().GetLine();
      EmitConstant(m_Locals.at(localName).second);
      EmitOp(Ops::AssignLocal, line);
    } else {
      bool shouldBreak = false;
      while (!shouldBreak) {
        switch (m_Current.value().GetType()) {
          case TokenType::And:
            And(false, true);
            break;
          case TokenType::Or:
            Or(false, true);
            break;
          case TokenType::EqualEqual:
          case TokenType::BangEqual:
            Equality(false, true);
            break;
          case TokenType::GreaterThan:
          case TokenType::GreaterEqual:
          case TokenType::LessThan:
          case TokenType::LessEqual:
            Comparison(false, true);
            break;
          case TokenType::Plus:
          case TokenType::Minus:
            Term(false, true);
            break;
          case TokenType::Star:
          case TokenType::StarStar:
          case TokenType::Slash:
          case TokenType::Mod:
            Factor(false, true);
            break;
          case TokenType::Semicolon:
          case TokenType::RightParen:
          case TokenType::Comma:
          case TokenType::Colon:
            shouldBreak = true;
            break;
          default:
            MessageAtCurrent("Invalid token found in expression", LogLevel::Error);
            Advance();
            return;
        }
      }
    }
  } else {
    Or(canAssign, false);
  }
}

std::optional<std::exception> TryParseInt(const Token& token, std::int64_t& result)
{
  try {
    std::string str(token.GetText());
    result = std::stoll(str);
    return std::nullopt;
  } catch (const std::invalid_argument& e) {
    return e;
  } catch (const std::out_of_range& e) {
    return e;
  }
}

std::optional<std::exception> TryParseDouble(const Token& token, double& result)
{
  try {
    std::string str(token.GetText());
    result = std::stod(str);
    return std::nullopt;
  } catch (const std::invalid_argument& e) {
    return e;
  } catch (const std::out_of_range& e) {
    return e;
  }
}

static bool IsLiteral(const Token& token)
{
  auto type = token.GetType();
  return type == TokenType::True || type == TokenType::False
    || type == TokenType::Integer || type == TokenType::Double
    || type == TokenType::String || type == TokenType::Char;
}

void Compiler::ExpressionStatement() 
{
  if (IsLiteral(m_Current.value()) || IsOperator(m_Current.value().GetType())) {
    MessageAtCurrent("Expected identifier or keyword at start of expression", LogLevel::Error);
    return;
  }
  Expression(true);
  Consume(TokenType::Semicolon, "Expected ';' after expression");
}

void Compiler::ForStatement() 
{
  auto previousContext = m_CurrentContext;
  m_CurrentContext = Context::Loop;

  m_BreakIdxPairs.emplace();

  std::vector<std::string> startLocalsList;
  for (const auto& [name, _] : m_Locals) {
    startLocalsList.push_back(name);
  }

  Consume(TokenType::Identifier, "Expected identifier after `for`");
  auto iteratorName = std::string(m_Previous.value().GetText());
  std::int64_t iteratorId;
  if (m_Locals.find(iteratorName) == m_Locals.end()) {
    iteratorId = m_Locals.size();
    m_Locals.insert(std::make_pair(iteratorName, std::make_pair(false, iteratorId)));
    EmitOp(Ops::DeclareLocal, m_Previous.value().GetLine());
  } else {
    if (m_Locals.at(iteratorName).first) {
      MessageAtPrevious(fmt::format("Loop variable '{}' has already been declared as `final`", iteratorName), LogLevel::Error);
      return;
    }
    iteratorId = m_Locals.at(iteratorName).second;
    if (m_Verbose) {
      MessageAtPrevious(fmt::format("There is already a local variable called '{}' in this scope which will be reassigned inside the `for` loop", iteratorName), 
          LogLevel::Warning);
    }
  }

  Consume(TokenType::In, "Expected `in` after identifier");

  auto line = m_Previous.value().GetLine();
  if (Match(TokenType::Integer)) {
    std::int64_t value;
    auto result = TryParseInt(m_Previous.value(), value);
    if (result.has_value()) {
      MessageAtPrevious(fmt::format("Token could not be parsed as integer: {}", result.value().what()), LogLevel::Error);
      return;
    }
    EmitConstant(value);
    EmitOp(Ops::LoadConstant, line);
    EmitConstant(iteratorId);
    EmitOp(Ops::AssignLocal, line);
  } else if (Match(TokenType::Double)) {
    double value;
    auto result = TryParseDouble(m_Previous.value(), value);
    if (result.has_value()) {
      MessageAtPrevious(fmt::format("Token could not be parsed as float: {}", result.value().what()), LogLevel::Error);
      return;
    }
    EmitConstant(value);
    EmitOp(Ops::LoadConstant, line);
    EmitConstant(iteratorId);
    EmitOp(Ops::AssignLocal, line);
  } else if (Match(TokenType::Identifier)) {
    auto localName = std::string(m_Previous.value().GetText());
    if (m_Locals.find(localName) == m_Locals.end()) {
      MessageAtPrevious(fmt::format("Cannot find variable '{}' in this scope.", localName), LogLevel::Error);
      return;
    }
    auto localId = m_Locals.at(localName).second;
    EmitConstant(localId);
    EmitOp(Ops::LoadLocal, line);
    EmitConstant(iteratorId);
    EmitOp(Ops::AssignLocal, line);
  } else {
    MessageAtCurrent("Expected identifier or number as range min", LogLevel::Error);
    return;
  }

  Consume(TokenType::DotDot, "Expected '..' after range min");

  std::variant<std::int64_t, double, std::int64_t> max;

  if (Match(TokenType::Integer)) {
    std::int64_t value;
    auto result = TryParseInt(m_Previous.value(), value);
    if (result.has_value()) {
      MessageAtPrevious(fmt::format("Token could not be parsed as integer: {}", result.value().what()), LogLevel::Error);
      return;
    }
    max.emplace<0>(value);
  } else if (Match(TokenType::Double)) {
    double value;
    auto result = TryParseDouble(m_Previous.value(), value);
    if (result.has_value()) {
      MessageAtPrevious(fmt::format("Token could not be parsed as float: {}", result.value().what()), LogLevel::Error);
      return;
    }
    max.emplace<1>(value);
  } else if (Match(TokenType::Identifier)) {
    auto localName = std::string(m_Previous.value().GetText());
    if (m_Locals.find(localName) == m_Locals.end()) {
      MessageAtPrevious(fmt::format("Cannot find variable '{}' in this scope.", localName), LogLevel::Error);
      return;
    }
    auto localId = m_Locals.at(localName).second;
    max.emplace<2>(localId);
  } else {
    MessageAtCurrent("Expected identifier or integer as range max", LogLevel::Error);
    return;
  }

  std::variant<std::int64_t, double> increment;

  if (Match(TokenType::By)) {
    if (Match(TokenType::Integer)) {
      std::int64_t value;
      auto result = TryParseInt(m_Previous.value(), value);
      if (result.has_value()) {
        MessageAtPrevious(fmt::format("Token could not be parsed as integer: {}", result.value().what()), LogLevel::Error);
        return;
      }
      increment.emplace<0>(value);
    } else if (Match(TokenType::Double)) {
      double value;
      auto result = TryParseDouble(m_Previous.value(), value);
      if (result.has_value()) {
        MessageAtPrevious(fmt::format("Token could not be parsed as float: {}", result.value().what()), LogLevel::Error);
        return;
      }
      increment.emplace<1>(value);
    } else {
      MessageAtPrevious("Increment must be a number", LogLevel::Error);
      return;
    }
  } else {
    increment.emplace<0>(1);
  }

  Consume(TokenType::Colon, "Expected ':' after `for` statement");

  auto constantIdx = static_cast<std::int64_t>(m_Vm.GetNumConstants());
  auto opIdx = static_cast<std::int64_t>(m_Vm.GetNumOps());

  while (!Match(TokenType::End)) {
    Declaration();

    if (Match(TokenType::EndOfFile)) {
      MessageAtPrevious("Unterminated `for`", LogLevel::Error);
      return;
    }
  }

  EmitConstant(iteratorId);
  EmitOp(Ops::LoadLocal, line);
  if (increment.index() == 0) {
    EmitConstant(std::get<0>(increment));
  } else {
    EmitConstant(std::get<1>(increment));
  }
  EmitOp(Ops::LoadConstant, line);
  EmitOp(Ops::Add, line);
  EmitConstant(iteratorId);
  EmitOp(Ops::AssignLocal, line);

  EmitConstant(iteratorId);
  EmitOp(Ops::LoadLocal, line);

  switch (max.index()) {
    case 0:
      EmitConstant(std::get<0>(max));
      EmitOp(Ops::LoadConstant, line);
      break;
    case 1:
      EmitConstant(std::get<1>(max));
      EmitOp(Ops::LoadConstant, line);
      break;
    case 2:
      EmitConstant(std::get<2>(max));
      EmitOp(Ops::LoadLocal, line);
      break;
  }

  EmitOp(Ops::GreaterEqual, line);

  EmitConstant(constantIdx);
  EmitConstant(opIdx);
  EmitOp(Ops::JumpIfFalse, line);

  if (m_BreakJumpNeedsIndexes) {
    for (auto& p : m_BreakIdxPairs.top()) {
      m_Vm.SetConstantAtIndex(p.first, static_cast<std::int64_t>(m_Vm.GetNumConstants()));
      m_Vm.SetConstantAtIndex(p.second, static_cast<std::int64_t>(m_Vm.GetNumOps()));
    }
    m_BreakJumpNeedsIndexes = !m_BreakIdxPairs.empty();
    m_BreakIdxPairs.pop();
  }

  for (auto it = m_Locals.begin(); it != m_Locals.end();) {
    if (std::find(startLocalsList.begin(), startLocalsList.end(), it->first) == startLocalsList.end()) {
      EmitOp(Ops::PopLocal, line);
      it = m_Locals.erase(it);
    } else {
      it++;
    }
  }

  m_CurrentContext = previousContext;
}

void Compiler::IfStatement() 
{
  m_ShouldNotPopValue = true;
  Expression(false);
  m_ShouldNotPopValue = false;
  Consume(TokenType::Colon, "Expected ':' after condition");
  
  // store indexes of constant and instruction indexes to jump
  auto topConstantIdxToJump = m_Vm.GetNumConstants();
  EmitConstant(std::int64_t{});
  auto topOpIdxToJump = m_Vm.GetNumConstants();
  EmitConstant(std::int64_t{});

  EmitOp(Ops::JumpIfFalse, m_Previous.value().GetLine());

  // constant index, op index
  std::vector<std::tuple<std::int64_t, std::int64_t>> endJumpIndexPairs;

  std::vector<std::string> startLocalsList;
  for (const auto& [name, _] : m_Locals) {
    startLocalsList.push_back(name);
  }

  bool topJumpSet = false;
  bool elseBlockFound = false;
  bool elseIfBlockFound = false;
  bool needsElseBlock = true;
  while (true) {
    if (Match(TokenType::Else)) {
      // make any unreachables 'else' blocks a compiler error
      if (elseBlockFound) {
        MessageAtPrevious("Unreachable `else` due to previous `else`", LogLevel::Error);
        return;
      }
      
      // if the ifs condition passed and its block executed, it needs to jump to the end
      auto endConstantIdx = m_Vm.GetNumConstants();
      EmitConstant(std::int64_t{});
      auto endOpIdx = m_Vm.GetNumConstants();
      EmitConstant(std::int64_t{});
      EmitOp(Ops::Jump, m_Previous.value().GetLine());
      
      endJumpIndexPairs.emplace_back(endConstantIdx, endOpIdx);

      auto numConstants = m_Vm.GetNumConstants();
      auto numOps = m_Vm.GetNumOps();
      
      // haven't told the 'if' where to jump to yet if its condition fails 
      if (!topJumpSet) {
        m_Vm.SetConstantAtIndex(topConstantIdxToJump, static_cast<std::int64_t>(numConstants));
        m_Vm.SetConstantAtIndex(topOpIdxToJump, static_cast<std::int64_t>(numOps));
        topJumpSet = true;
      }

      if (Match(TokenType::Colon)) {
        elseBlockFound = true;
      } else if (Check(TokenType::If)) {
        elseIfBlockFound = true;
        needsElseBlock = false;
      } else {
        MessageAtCurrent("Expected `if` or `:` after `else`", LogLevel::Error);
        return;
      }
    }   

    Declaration();

    // above call to Declaration() will handle chained if/else
    if (elseIfBlockFound) {
      break;
    }
    
    if (Match(TokenType::EndOfFile)) {
      MessageAtPrevious("Unterminated `if` statement", LogLevel::Error);
      return;
    }

    if (needsElseBlock && Match(TokenType::End)) {
      break;
    }
  }

  auto numConstants = static_cast<std::int64_t>(m_Vm.GetNumConstants());
  auto numOps = static_cast<std::int64_t>(m_Vm.GetNumOps());

  for (auto [constIdx, opIdx] : endJumpIndexPairs) {
    m_Vm.SetConstantAtIndex(constIdx, numConstants);
    m_Vm.SetConstantAtIndex(opIdx, numOps);
  }
  
  // if there was no else or elseif block, jump to here
  if (!topJumpSet) {
    m_Vm.SetConstantAtIndex(topConstantIdxToJump, numConstants);
    m_Vm.SetConstantAtIndex(topOpIdxToJump, numOps);
  }

  auto line = m_Previous.value().GetLine();
  for (auto it = m_Locals.begin(); it != m_Locals.end();) {
    if (std::find(startLocalsList.begin(), startLocalsList.end(), it->first) == startLocalsList.end()) {
      EmitOp(Ops::PopLocal, line);
      it = m_Locals.erase(it);
    } else {
      it++;
    }
  }
}

void Compiler::PrintStatement() 
{
  Consume(TokenType::LeftParen, "Expected '(' after 'print'");
  if (Match(TokenType::RightParen)) {
    EmitOp(Ops::PrintTab, m_Current.value().GetLine());
  } else {
    m_ShouldNotPopValue = true;
    Expression(false);
    m_ShouldNotPopValue = false;
    EmitOp(Ops::Print, m_Current.value().GetLine());
    EmitOp(Ops::Pop, m_Current.value().GetLine());
    Consume(TokenType::RightParen, "Expected ')' after expression");
  }
  Consume(TokenType::Semicolon, "Expected ';' after expression");
}

void Compiler::PrintLnStatement() 
{
  Consume(TokenType::LeftParen, "Expected '(' after 'println'");
  if (Match(TokenType::RightParen)) {
    EmitOp(Ops::PrintEmptyLine, m_Current.value().GetLine());
  } else {
    m_ShouldNotPopValue = true;
    Expression(false);
    m_ShouldNotPopValue = false;
    EmitOp(Ops::PrintLn, m_Current.value().GetLine());
    EmitOp(Ops::Pop, m_Current.value().GetLine());
    Consume(TokenType::RightParen, "Expected ')' after expression");
  }
  Consume(TokenType::Semicolon, "Expected ';' after expression");
}

void Compiler::ReturnStatement() 
{
  if (m_CurrentContext != Context::Function) {
    MessageAtPrevious("`return` only allowed inside functions", LogLevel::Error);
    return;
  }

  if (m_Vm.GetLastFunctionName() == "main") {
    MessageAtPrevious("Cannot return from main function", LogLevel::Error);
    return;
  } 

  if (Match(TokenType::Semicolon)) {
    EmitConstant((void*)nullptr);
    EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
    EmitOp(Ops::Return, m_Previous.value().GetLine());
    return;
  }

  m_ShouldNotPopValue = true;
  Expression(false);
  m_ShouldNotPopValue = false;
  EmitOp(Ops::Return, m_Previous.value().GetLine());
  Consume(TokenType::Semicolon, "Expected ';' after expression");
  m_FunctionHadReturn = true;
}

void Compiler::WhileStatement() 
{
  auto previousContext = m_CurrentContext;
  m_CurrentContext = Context::Loop;

  m_BreakIdxPairs.emplace();

  auto constantIdx = static_cast<std::int64_t>(m_Vm.GetNumConstants());
  auto opIdx = static_cast<std::int64_t>(m_Vm.GetNumOps());

  m_ShouldNotPopValue = true;
  Expression(false);
  m_ShouldNotPopValue = false;

  auto line = m_Previous.value().GetLine();

  auto endConstantJumpIdx = m_Vm.GetNumConstants();
  EmitConstant(std::int64_t{});
  auto endOpJumpIdx = m_Vm.GetNumConstants();
  EmitConstant(std::int64_t{});
  EmitOp(Ops::JumpIfFalse, m_Previous.value().GetLine());

  Consume(TokenType::Colon, "Expected ':' after expression");

  std::vector<std::string> startLocalsList;
  for (const auto& [name, _] : m_Locals) {
    startLocalsList.push_back(name);
  }

  while (!Match(TokenType::End)) {
    Declaration();
    if (Match(TokenType::EndOfFile)) {
      MessageAtPrevious("Unterminated `while` loop", LogLevel::Error);
      return;
    }
  }

  // jump back up to the expression so it can be re-evaluated
  EmitConstant(constantIdx);
  EmitConstant(opIdx);
  EmitOp(Ops::Jump, line);

  auto numConstants = static_cast<std::int64_t>(m_Vm.GetNumConstants());
  auto numOps = static_cast<std::int64_t>(m_Vm.GetNumOps());
  m_Vm.SetConstantAtIndex(endConstantJumpIdx, numConstants);
  m_Vm.SetConstantAtIndex(endOpJumpIdx, numOps);

  if (m_BreakJumpNeedsIndexes) {
    for (auto& p : m_BreakIdxPairs.top()) {
      m_Vm.SetConstantAtIndex(p.first, numConstants);
      m_Vm.SetConstantAtIndex(p.second, numOps);
    }
    m_BreakJumpNeedsIndexes = !m_BreakIdxPairs.empty();
    m_BreakIdxPairs.pop();
  }
  
  for (auto it = m_Locals.begin(); it != m_Locals.end();) {
    if (std::find(startLocalsList.begin(), startLocalsList.end(), it->first) == startLocalsList.end()) {
      EmitOp(Ops::PopLocal, line);
      it = m_Locals.erase(it);
    } else {
      it++;
    }
  }

  m_CurrentContext = previousContext;
}

void Compiler::Or(bool canAssign, bool skipFirst)
{
  if (!skipFirst) {
    And(canAssign, false);
  }
  while (Match(TokenType::Or)) {
    And(canAssign, false);
    EmitOp(Ops::Or, m_Current.value().GetLine());
  }
}

void Compiler::And(bool canAssign, bool skipFirst)
{
  if (!skipFirst) {
    Equality(canAssign, false);
  }
  while (Match(TokenType::And)) {
    Equality(canAssign, false);
    EmitOp(Ops::And, m_Current.value().GetLine());
  }
}

void Compiler::Equality(bool canAssign, bool skipFirst)
{
  if (!skipFirst) {
    Comparison(canAssign, false);
  }
  if (Match(TokenType::EqualEqual)) {
    Comparison(canAssign, false);
    EmitOp(Ops::Equal, m_Current.value().GetLine());
  } else if (Match(TokenType::BangEqual)) {
    Comparison(canAssign, false);
    EmitOp(Ops::NotEqual, m_Current.value().GetLine());
  }
}

void Compiler::Comparison(bool canAssign, bool skipFirst)
{
  if (!skipFirst) {
    Term(canAssign, false);
  }
  if (Match(TokenType::GreaterThan)) {
    Term(canAssign, false);
    EmitOp(Ops::Greater, m_Current.value().GetLine());
  } else if (Match(TokenType::GreaterEqual)) {
    Term(canAssign, false);
    EmitOp(Ops::GreaterEqual, m_Current.value().GetLine());
  } else if (Match(TokenType::LessThan)) {
    Term(canAssign, false);
    EmitOp(Ops::Less, m_Current.value().GetLine());
  } else if (Match(TokenType::LessEqual)) {
    Term(canAssign, false);
    EmitOp(Ops::LessEqual, m_Current.value().GetLine());
  }
}

void Compiler::Term(bool canAssign, bool skipFirst)
{
  if (!skipFirst) {
    Factor(canAssign, false);
  }
  while (true) {
    if (Match(TokenType::Minus)) {
      Factor(canAssign, false);
      EmitOp(Ops::Subtract, m_Current.value().GetLine());
    } else if (Match(TokenType::Plus)) {
      Factor(canAssign, false);
      EmitOp(Ops::Add, m_Current.value().GetLine());
    } else {
      break;
    }
  }
}

void Compiler::Factor(bool canAssign, bool skipFirst)
{
  if (!skipFirst) {
    Unary(canAssign);
  }
  while (true) {
    if (Match(TokenType::StarStar)) {
      Unary(canAssign);
      EmitOp(Ops::Pow, m_Current.value().GetLine());
    } else if (Match(TokenType::Star)) {
      Unary(canAssign);
      EmitOp(Ops::Multiply, m_Current.value().GetLine());
    } else if (Match(TokenType::Slash)) {
      Unary(canAssign);
      EmitOp(Ops::Divide, m_Current.value().GetLine());
    } else if (Match(TokenType::Mod)) {
      Unary(canAssign);
      EmitOp(Ops::Mod, m_Current.value().GetLine());
    } else {
      break;
    }
  }
}

static std::unordered_map<TokenType, Ops> s_CastOps = {
  std::make_pair(TokenType::IntIdent, Ops::CastAsInt),
  std::make_pair(TokenType::FloatIdent, Ops::CastAsFloat),
  std::make_pair(TokenType::BoolIdent, Ops::CastAsBool),
  std::make_pair(TokenType::StringIdent, Ops::CastAsString),
  std::make_pair(TokenType::CharIdent, Ops::CastAsChar),
};

void Compiler::Unary(bool canAssign)
{
  if (Match(TokenType::Bang)) {
    auto line = m_Previous.value().GetLine();
    Unary(canAssign);
    EmitOp(Ops::Not, line);
  } else if (Match(TokenType::Minus)) {
    auto line = m_Previous.value().GetLine();
    Unary(canAssign);
    EmitOp(Ops::Negate, line);
  } else {
    Call(canAssign);
  }
}

void Compiler::Call(bool canAssign)
{
  Primary(canAssign);
  auto prev = m_Previous.value();
  auto prevText = std::string(m_Previous.value().GetText());

  if (prev.GetType() != TokenType::Identifier && Check(TokenType::LeftParen)) {
    MessageAtCurrent("'(' only allowed after functions and classes", LogLevel::Error);
    return;
  }

  if (prev.GetType() == TokenType::Identifier) {
    if (Match(TokenType::LeftParen)) {
      std::int64_t numArgs = 0;
      if (!Match(TokenType::RightParen)) {
        while (true) {        
          Expression(false);
          numArgs++;
          if (Match(TokenType::RightParen)) {
            break;
          }
          Consume(TokenType::Comma, "Expected ',' after funcion call argument");
        }
      }
      
      auto hash = static_cast<std::int64_t>(m_Hasher(prevText));
      EmitConstant(hash);
      EmitConstant(numArgs);
      EmitOp(Ops::Call, m_Previous.value().GetLine());

      if (!m_ShouldNotPopValue) {
        // pop unused return value
        EmitOp(Ops::Pop, m_Previous.value().GetLine());
      }
    } else if (Match(TokenType::Dot)) {
      // TODO: account for dot
    } else {
      // not a call or member access, so we are just trying to call on the value of the local
      // or reassign it 
      // if its not a reassignment, we are trying to load its value 
      // Primary() has already but the variable's id on the stack
      if (!Check(TokenType::Equal)) {
        if (m_Locals.find(prevText) == m_Locals.end()) {
          MessageAtPrevious(fmt::format("Cannot find variable '{}' in this scope", prevText), LogLevel::Error);
          return;
        }
        EmitConstant(m_Locals.at(prevText).second);
        EmitOp(Ops::LoadLocal, prev.GetLine());
      }
    }
  }
}

static bool IsTypeIdent(const Token& token)
{
  auto type = token.GetType();
  return type == TokenType::IntIdent || type == TokenType::FloatIdent 
    || type == TokenType::BoolIdent || type == TokenType::StringIdent 
    || type == TokenType::CharIdent;
}

void Compiler::Primary(bool canAssign)
{
  if (Match(TokenType::True)) {
    EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
    EmitConstant(true);
  } else if (Match(TokenType::False)) {
    EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
    EmitConstant(false);
  } else if (Match(TokenType::This)) {
    // TODO: this 
  } else if (Match(TokenType::Integer)) {
    try {
      std::string str(m_Previous.value().GetText());
      std::int64_t value = std::stoll(str);
      EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
      EmitConstant(value);
    } catch (const std::invalid_argument& e) {
      MessageAtPrevious(fmt::format("Token could not be parsed as an int: {}", e.what()), LogLevel::Error);
      return;
    } catch (const std::out_of_range&) {
      MessageAtPrevious("Int out of range.", LogLevel::Error);
      return;
    }
  } else if (Match(TokenType::Double)) {
    try {
      std::string str(m_Previous.value().GetText());
      auto value = std::stod(str);
      EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
      EmitConstant(value);
    } catch (const std::invalid_argument& e) {
      MessageAtPrevious(fmt::format("Token could not be parsed as an float: {}", e.what()), LogLevel::Error);
      return;
    } catch (const std::out_of_range&) {
      MessageAtPrevious("Float out of range.", LogLevel::Error);
      return;
    }
  } else if (Match(TokenType::String)) {
    String();
  } else if (Match(TokenType::Char)) {
    Char();
  } else if (Match(TokenType::Identifier)) {
    // do nothing, but consume the identifier and return
    // caller functions will handle it
  } else if (Match(TokenType::Null)) {
    EmitConstant((void*)nullptr);
    EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
  } else if (Match(TokenType::LeftParen)) {
    Expression(canAssign);
    Consume(TokenType::RightParen, "Expected ')'");
  } else if (Match(TokenType::InstanceOf)) {
    InstanceOf();
  } else if (IsTypeIdent(m_Current.value())) {
    Cast();
  } else {
    Expression(canAssign);
  }
}

static const char s_EscapeChars[] = {'t', 'b', 'n', 'r', 'f', '\'', '"', '\\'};
static const std::unordered_map<char, char> s_EscapeCharsLookup = {
  std::make_pair('t', '\t'),
  std::make_pair('b', '\b'),
  std::make_pair('r', '\r'),
  std::make_pair('n', '\n'),
  std::make_pair('f', '\f'),
  std::make_pair('\'', '\''),
  std::make_pair('"', '\"'),
  std::make_pair('\\', '\\'),
};

static bool IsEscapeChar(char c, char& result) 
{
  for (auto escapeChar : s_EscapeChars) {
    if (c == escapeChar) {
      result = s_EscapeCharsLookup.at(escapeChar);
      return true;
    }
  }
  return false;
}

void Compiler::Char()
{
  auto text = m_Previous.value().GetText();
  auto trimmed = text.substr(1, text.length() - 2);
  switch (trimmed.length()) {
    case 2:
      if (trimmed[0] != '\\') {
        MessageAtPrevious("`char` must contain a single character or escape character", LogLevel::Error);
        return;
      }
      char c;
      if (IsEscapeChar(trimmed[1], c)) {
        EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
        EmitConstant(c);
      } else {
        MessageAtPrevious("Unrecognised escape character", LogLevel::Error);
      }
      break;
    case 1:
      if (trimmed[0] == '\\') {
        MessageAtPrevious("Expected escape character after backslash", LogLevel::Error);
        return;
      }
      EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
      EmitConstant(static_cast<char>(trimmed[0]));
      break;
    default:
      MessageAtPrevious("`char` must contain a single character or escape character", LogLevel::Error);
      break;
  }
}

void Compiler::String()
{
  auto text = m_Previous.value().GetText();
  std::string res;
  for (auto i = 1; i < text.length() - 1; i++) {
    if (text[i] == '\\') {
      i++;
      if (i == text.length() - 2) {
        MessageAtPrevious("Expected escape character", LogLevel::Error);
        return;
      }
      char c;
      if (IsEscapeChar(text[i], c)) {
        res.push_back(c);
      } else {
        MessageAtPrevious("Unrecognised escape character", LogLevel::Error);
        return;
      }
    } else {
      res.push_back(text[i]);
    }
  }
  EmitOp(Ops::LoadConstant, m_Previous.value().GetLine());
  EmitConstant(res);
}

void Compiler::InstanceOf()
{
  Consume(TokenType::LeftParen, "Expected '(' after 'instanceof'");
  Expression(false);
  Consume(TokenType::Comma, "Expected ',' after expression");

  switch (m_Current.value().GetType()) {
    case TokenType::BoolIdent:
      EmitConstant(std::int64_t(0));
      break;
    case TokenType::CharIdent:
      EmitConstant(std::int64_t(1));
      break;
    case TokenType::FloatIdent:
      EmitConstant(std::int64_t(2));
      break;
    case TokenType::IntIdent:
      EmitConstant(std::int64_t(3));
      break;
    case TokenType::Null:
      EmitConstant(std::int64_t(4));
      break;
    case TokenType::StringIdent:
      EmitConstant(std::int64_t(5));
      break;
    default:
      MessageAtCurrent("Expected type as second argument for `instanceof`", LogLevel::Error);
      return;
  }

  EmitOp(Ops::CheckType, m_Current.value().GetLine());

  Advance();  // Consume the type ident
  Consume(TokenType::RightParen, "Expected ')'");

  if (!m_ShouldNotPopValue) {
    // pop unused return value
    EmitOp(Ops::Pop, m_Previous.value().GetLine());
  }
}

void Compiler::Cast()
{
  auto type = m_Current.value().GetType();
  Advance();
  Consume(TokenType::LeftParen, "Expected '(' after type ident");
  Expression(false);
  EmitOp(s_CastOps[type], m_Current.value().GetLine());
  Consume(TokenType::RightParen, "Expected ')' after expression");
  if (!m_ShouldNotPopValue) {
    // pop unused return value
    EmitOp(Ops::Pop, m_Previous.value().GetLine());
  }
}

void Compiler::MessageAtCurrent(const std::string& message, LogLevel level)
{
  Message(m_Current, message, level);
}

void Compiler::MessageAtPrevious(const std::string& message, LogLevel level)
{
  Message(m_Previous, message, level);
}

void Compiler::Message(const std::optional<Token>& token, const std::string& message, LogLevel level)
{
  if (level == LogLevel::Error) {
    if (m_PanicMode) return;
    m_PanicMode = true;
  }

  auto colour = fmt::fg(level == LogLevel::Error ? fmt::color::red : fmt::color::orange);
  fmt::print(stderr, colour | fmt::emphasis::bold, level == LogLevel::Error ? "ERROR: " : "WARNING: ");
  
  auto type = token.value().GetType();
  switch (type) {
    case TokenType::EndOfFile:
      fmt::print(stderr, "at end: ");
      fmt::print(stderr, "{}\n", message);
      break;
    case TokenType::Error:
      fmt::print(stderr, "{}\n", token.value().GetErrorMessage());
      break;
    default:
      fmt::print(stderr, "at '{}': ", token.value().GetText());
      fmt::print(stderr, "{}\n", message);
      break;
  }

  auto lineNo = token.value().GetLine();
  auto column = token.value().GetColumn() - token.value().GetLength();  // need the START of the token
  fmt::print(stderr, "       --> {}:{}:{}\n", m_CurrentFileName, lineNo, column + 1); 
  fmt::print(stderr, "        |\n");
  fmt::print(stderr, "{:>7} | {}\n", lineNo, m_Scanner.GetCodeAtLine(lineNo));
  fmt::print(stderr, "        | ");
  for (auto i = 0; i < column; i++) {
    fmt::print(stderr, " ");
  }
  for (auto i = 0; i < token.value().GetLength(); i++) {
    fmt::print(stderr, colour, "^");
  }
  fmt::print("\n\n");

  if (level == LogLevel::Error) {
    m_HadError = true;
  }
}

