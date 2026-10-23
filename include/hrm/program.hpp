#pragma once

#include "hrm/types.hpp"

#include <string>
#include <vector>

namespace hrm {

struct SourceInstruction {
  std::string text;
  std::size_t source_line = 0; // Optional physical line; unrelated to jump targets.
};

struct Program {
  std::vector<SourceInstruction> instructions;
};

struct IntegerResult {
  Value value = 0;
  ErrorCode error = ErrorCode::None;
};

struct DecodedInstruction {
  Opcode opcode = Opcode::Inbox;
  Value operand = 0;
};

struct DecodeResult {
  DecodedInstruction instruction;
  ErrorCode error = ErrorCode::None;
};

// Parse a complete signed decimal token without exceptions or overflow.
IntegerResult parse_integer(const std::string &token);

// Decode one line without checking level permissions or machine state.
DecodeResult decode_line(const std::string &line);

} // namespace hrm
