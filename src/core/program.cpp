#include "hrm/program.hpp"

#include <limits>
#include <sstream>

namespace hrm {

IntegerResult parse_integer(const std::string &token) {
  IntegerResult result;
  std::size_t begin = 0;
  const bool negative = !token.empty() && token[0] == '-';
  if (!token.empty() && (token[0] == '+' || token[0] == '-')) {
    begin = 1;
  }
  if (begin == token.size()) {
    result.error = ErrorCode::InvalidInteger;
    return result;
  }
  // Validate the whole token before reporting a range error.
  for (std::size_t i = begin; i < token.size(); ++i) {
    if (token[i] < '0' || token[i] > '9') {
      result.error = ErrorCode::InvalidInteger;
      return result;
    }
  }

  const std::uint64_t maximum = static_cast<std::uint64_t>(std::numeric_limits<Value>::max());
  const std::uint64_t limit = negative ? maximum + 1 : maximum;
  std::uint64_t magnitude = 0;
  for (std::size_t i = begin; i < token.size(); ++i) {
    const unsigned digit = static_cast<unsigned>(token[i] - '0');
    if (magnitude > (limit - digit) / 10) {
      result.error = ErrorCode::OperandOutOfRange;
      return result;
    }
    magnitude = magnitude * 10 + digit;
  }
  if (negative && magnitude == maximum + 1) {
    result.value = std::numeric_limits<Value>::min();
  } else {
    result.value = static_cast<Value>(magnitude);
    if (negative) {
      result.value = -result.value;
    }
  }
  return result;
}

DecodeResult decode_line(const std::string &line) {
  DecodeResult result;
  std::istringstream tokens(line);
  std::string name;
  tokens >> name;
  const char *names[] =
    {"inbox", "outbox", "add", "sub", "copyto", "copyfrom", "jump", "jumpifzero", "jumpifnegative"};
  std::size_t index = 0;
  while (index < opcode_count && name != names[index]) {
    ++index;
  }
  if (index == opcode_count) {
    result.error = ErrorCode::UnknownOpcode;
    return result;
  }
  result.instruction.opcode = static_cast<Opcode>(index);

  std::string operand;
  std::string extra;
  const bool has_operand = static_cast<bool>(tokens >> operand);
  const bool needs_operand = index >= 2;
  if (has_operand != needs_operand || (tokens >> extra)) {
    result.error = ErrorCode::WrongOperandCount;
    return result;
  }
  if (needs_operand) {
    const IntegerResult number = parse_integer(operand);
    result.error = number.error;
    result.instruction.operand = number.value;
  }
  return result;
}

} // namespace hrm
