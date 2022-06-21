/*
 *  The Grace Programming Language.
 *
 *  This file contains the out of line definitions for the GraceDictionary class, the underlying class for Dictionaries in Grace.
 *
 *  Copyright (c) 2022 - Present, Ryan Jeffares.
 *  All rights reserved.
 *
 *  For licensing information, see grace.hpp
 */

#include <fmt/core.h>
#include <fmt/format.h>

#include "grace_dictionary.hpp"

static constexpr std::size_t s_InitialCapacity = 8;
static constexpr float s_GrowFactor = 0.75f;

namespace Grace
{
  GraceDictionary::GraceDictionary()
    : m_Data(s_InitialCapacity),
      m_CellStates(s_InitialCapacity, CellState::NeverUsed),
      m_Size(0)
  {
    
  }

  GraceDictionary::GraceDictionary(const GraceDictionary& other)
    : m_Data(other.m_Data),
      m_CellStates(other.m_CellStates),
      m_Size(other.m_Size)
  {

  }

  GraceDictionary::GraceDictionary(GraceDictionary&& other)
    : m_Data(std::move(other.m_Data)),
      m_CellStates(std::move(other.m_CellStates)),
      m_Size(other.m_Size)
  {
    other.m_Size = 0;
  }

  void GraceDictionary::DebugPrint() const
  {
    fmt::print("Dictionary: {}\n", ToString());
  }

  void GraceDictionary::Print(bool err) const
  {
    auto stream = err ? stderr : stdout;
    fmt::print(stream, "{}", ToString());
  }

  void GraceDictionary::PrintLn(bool err) const
  {
    auto stream = err ? stderr : stdout;
    fmt::print(stream, "{}\n", ToString());
  }

  std::string GraceDictionary::ToString() const
  {
    std::string res = "{";
    std::size_t count = 0;
    for (std::size_t i = 0; i < m_Data.size(); ++i) {
      if (m_CellStates[i] != CellState::Occupied) continue;
      auto kvp = dynamic_cast<GraceKeyValuePair*>(m_Data[i].GetObject());
      res.append(kvp->ToString());
      if (count++ < m_Size - 1) {
        res.append(", ");
      }
    }
    res.append("}");
    return res;
  }

  bool GraceDictionary::AsBool() const
  {
    return !m_Data.empty();
  }

  GraceDictionary::Iterator GraceDictionary::Begin()
  {
    for (auto it = m_Data.begin(); it != m_Data.end(); ++it) {
      if (it->GetType() != VM::Value::Type::Null) {
        return it;
      }
    }
    return m_Data.end();
  }

  GraceDictionary::Iterator GraceDictionary::End()
  {
    return m_Data.end();
  }

  void GraceDictionary::IncrementIterator(Iterator& toIncrement) const
  {
    do {
      toIncrement++;
    } while (toIncrement->GetType() == VM::Value::Type::Null && toIncrement != m_Data.end());
  }

  bool GraceDictionary::Insert(VM::Value&& key, VM::Value&& value)
  {
    auto fullness = static_cast<float>(m_Size) / static_cast<float>(m_Data.size());
    if (fullness > s_GrowFactor) {
      m_Data.resize(m_Data.size() * 2);
      m_CellStates.resize(m_CellStates.size() * 2, CellState::NeverUsed);
      Rehash();
      InvalidateIterators();
    }

    auto hash = m_Hasher(key);
    auto index = hash % m_Data.size();

    auto state = m_CellStates[index];
    switch (state) {
      case CellState::NeverUsed:
      case CellState::Tombstone:
        m_Data[index] = VM::Value::CreateObject<GraceKeyValuePair>(std::move(key), std::move(value));
        m_CellStates[index] = CellState::Occupied;
        m_Size++;
        return true;
      case CellState::Occupied: {
        if (dynamic_cast<GraceKeyValuePair*>(m_Data[index].GetObject())->Key() == key) {
          return false;
        }
        for (auto i = index + 1; ; ++i) {
          if (i == m_Data.size()) {
            i = 0;
          }
          if (m_CellStates[i] == CellState::Occupied) {
            if (dynamic_cast<GraceKeyValuePair*>(m_Data[i].GetObject())->Key() == key) {
              return false;
            }
            continue;
          }
          m_Data[i] = VM::Value::CreateObject<GraceKeyValuePair>(std::move(key), std::move(value));
          m_CellStates[i] = CellState::Occupied;
          m_Size++;
          return true;
        }
      }
      default:
        GRACE_UNREACHABLE();
        return false;
    }
  }

  VM::Value GraceDictionary::Get(const VM::Value& key)
  {
    for (auto& kvp : m_Data) {
      if (kvp.GetType() == VM::Value::Type::Null) continue;
      auto p = dynamic_cast<GraceKeyValuePair*>(kvp.GetObject());
      if (key == p->Key()) {
        return p->Value();
      }
    }
    return VM::Value();
  }

  std::vector<VM::Value> GraceDictionary::ToVector() const
  {
    std::vector<VM::Value> res;
    res.reserve(m_Size);
    for (const auto& value : m_Data) {
      if (value.GetType() == VM::Value::Type::Null) continue;
      res.push_back(value);
    }
    return res;
  }

  void GraceDictionary::Rehash()
  {
    auto pairs = ToVector();
    std::fill(m_Data.begin(), m_Data.end(), VM::Value());
    std::fill(m_CellStates.begin(), m_CellStates.end(), CellState::NeverUsed);

    for (auto& pair : pairs) {
      auto kvp = dynamic_cast<GraceKeyValuePair*>(pair.GetObject());
      const auto& key = kvp->Key();
      auto hash = m_Hasher(key);
      auto index = hash % m_Data.size();

      auto state = m_CellStates[index];
      if (state == CellState::NeverUsed) {
        m_Data[index] = std::move(pair);
        m_CellStates[index] = CellState::Occupied;
      } else {
        // if its not NeverUsed it will be Occupied
        // iterate until we find a NeverUsed
        for (auto i = index + 1; ; ++i) {
          if (i == m_Data.size()) {
            i = 0;
          }
          if (m_CellStates[i] == CellState::NeverUsed) {
            m_Data[i] = std::move(pair);
            m_CellStates[i] = CellState::Occupied;
            break;
          }          
        }
      }
    }
  }
} // namespace Grace
