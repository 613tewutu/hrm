#pragma once

#include <cstddef>
#include <cstdint>

namespace hrm {

using Value = std::int64_t;

struct Cell {
  bool occupied = false;
  Value value = 0;

  /// Create an empty cell.
  Cell() = default;
  /// Create an occupied cell containing the supplied value, including zero.
  explicit Cell(Value number)
    : occupied(true),
      value(number) {
  }
};

enum class Opcode { Inbox, Outbox, Add, Sub, CopyTo, CopyFrom, Jump, JumpIfZero };

enum class ErrorCode {
  None,
  UnknownOpcode,
  OpcodeNotAllowed,
  WrongOperandCount,
  InvalidInteger,
  OperandOutOfRange,
  InvalidFloorIndex,
  EmptyFloorCell,
  EmptyHand,
  InvalidJumpTarget,
  ArithmeticOverflow
};

struct Diagnostic {
  ErrorCode code = ErrorCode::None;
  std::size_t instruction = 0; // Original 1-based instruction ID; zero means no error.
};

enum class ExecutionStatus { Running, Halted, Faulted };
enum class HaltReason { None, ProgramEnd, InboxEmpty };
enum class Verdict { Pending, Success, Fail, Error };
enum class StepOutcome { Advanced, Halted, Faulted, AlreadyStopped };

} // namespace hrm
