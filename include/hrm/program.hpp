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

/// Parse a complete signed decimal token without exceptions or overflow.
///
/// @param token Optional sign followed by decimal digits, with no surrounding whitespace.
/// @return The parsed value, or an invalid-integer/range diagnostic; no partial value is accepted.
IntegerResult parse_integer(const std::string &token);

/// Decode one original instruction line without validating reachability or machine state.
///
/// @param line Lowercase opcode and the exact required number of whitespace-separated operands.
/// @return A decoded instruction or a syntax diagnostic. Callers decide when to report errors.
DecodeResult decode_line(const std::string &line);

} // namespace hrm
