#include "vm.hpp"
#include "vm_binary_op_handlers.hpp"

using namespace Grace::VM;

struct Result 
{
  VM::Constant c1, c2;
};

static inline Result PopLastTwo(std::vector<VM::Constant>& stack)
{
  auto c1 = stack[stack.size() - 2];
  auto c2 = stack[stack.size() - 1];
  stack.pop_back();
  stack.pop_back();
  return {c1, c2};
}

InterpretResult VM::Run()
{
  std::vector<Constant> constantsStack;

  while (m_OpCurrent < m_OpList.size()) {
    auto [op, line] = m_OpList[m_OpCurrent++];
    switch (op) {
      case Ops::Add: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleAddition(c1, c2, constantsStack)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::Subtract: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleSubtraction(c1, c2, constantsStack)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::Multiply: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleMultiplication(c1, c2, constantsStack)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::Divide: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleDivision(c1, c2, constantsStack)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::And: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (c1.GetType() != Constant::Type::Bool || c2.GetType() != Constant::Type::Bool) {
          RuntimeError("`and` can only be used with boolean operands", InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError; 
        }
        constantsStack.emplace_back(c1.Get<bool>() == c2.Get<bool>());
        break;
      }
      case Ops::Or: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (c1.GetType() != Constant::Type::Bool || c2.GetType() != Constant::Type::Bool) {
          RuntimeError("`or` can only be used with boolean operands", InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError; 
        }
        constantsStack.emplace_back(c1.Get<bool>() || c2.Get<bool>());
        break;
      }
      case Ops::Equal: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleEquality(c1, c2, constantsStack, true)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::NotEqual: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleEquality(c1, c2, constantsStack, false)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::Greater: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleGreaterThan(c1, c2, constantsStack)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::GreaterEqual: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleGreaterEqual(c1, c2, constantsStack)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::Less: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleLessThan(c1, c2, constantsStack)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::LessEqual: {
        auto [c1, c2] = PopLastTwo(constantsStack);
        if (!HandleLessEqual(c1, c2, constantsStack)) {
          RuntimeError(fmt::format("cannot compare `{}` with `{}`", c1.GetType(), c2.GetType()), 
              InterpretError::InvalidOperand, line);
          return InterpretResult::RuntimeError;
        }
        break;
      }
      case Ops::LoadConstant:
        constantsStack.push_back(m_ConstantList[m_ConstantCurrent++]);
        break;
      case Ops::Pop:
        constantsStack.pop_back();
        break;
      case Ops::Print:
        constantsStack.back().Print();
        break;
    }
  }

  fmt::print("STACK:\n");
  for (const auto& c : constantsStack) {
    c.Print();
  }

  return InterpretResult::RuntimeOk;
}

void VM::RuntimeError(const std::string& message, InterpretError errorType, int line)
{
  fmt::print(stderr, fmt::fg(fmt::color::red) | fmt::emphasis::bold, "ERROR: ");
  fmt::print(stderr, "[line {}] {}: {}. Stopping execution\n", line, errorType, message);
}
