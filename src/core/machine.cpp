#include "hrm/machine.hpp"

#include <limits>
#include <utility>

namespace hrm {
namespace {

/// Return whether a signed address fits the zero-based container range without narrowing.
bool valid_index(Value index, std::size_t size) {
  return index >= 0 && static_cast<std::uintmax_t>(index) < static_cast<std::uintmax_t>(size);
}

/// Compute signed arithmetic only when its result is representable.
///
/// @param left Current hand value.
/// @param right Floor-cell value to add or subtract.
/// @param subtract Select subtraction instead of addition.
/// @param result Receives the computed value only on success.
/// @return False on overflow, leaving result unchanged; true on a valid operation.
bool checked_arithmetic(Value left, Value right, bool subtract, Value &result) {
  const Value minimum = std::numeric_limits<Value>::min();
  const Value maximum = std::numeric_limits<Value>::max();
  if (subtract) {
    if ((right > 0 && left < minimum + right) || (right < 0 && left > maximum + right)) {
      return false;
    }
    result = left - right;
  } else {
    if ((right > 0 && left > maximum - right) || (right < 0 && left < minimum - right)) {
      return false;
    }
    result = left + right;
  }
  return true;
}

} // namespace

Machine::Machine(LevelDefinition level, Program program)
  : level_(std::move(level)),
    program_(std::move(program)) {
  reset();
}

const MachineState &Machine::state() const {
  return state_;
}
const LevelDefinition &Machine::level() const {
  return level_;
}
const Program &Machine::program() const {
  return program_;
}

void Machine::reset() {
  state_ = MachineState();
  state_.floor.resize(level_.floor_size);
  cache_.assign(program_.instructions.size(), CachedInstruction());
  if (program_.instructions.empty()) {
    halt(HaltReason::ProgramEnd);
  }
}

void Machine::halt(HaltReason reason) {
  state_.status = ExecutionStatus::Halted;
  state_.halt_reason = reason;
  state_.verdict = state_.output == level_.target ? Verdict::Success : Verdict::Fail;
}

StepResult Machine::fault(StepResult result, ErrorCode code) {
  state_.status = ExecutionStatus::Faulted;
  state_.verdict = Verdict::Error;
  state_.diagnostic.code = code;
  state_.diagnostic.instruction = result.instruction;
  result.outcome = StepOutcome::Faulted;
  result.diagnostic = state_.diagnostic;
  return result;
}

StepResult Machine::step() {
  StepResult result;
  result.previous_pc = state_.pc;
  result.next_pc = state_.pc;
  if (state_.status != ExecutionStatus::Running) {
    return result;
  }
  result.instruction = state_.pc + 1;
  ++state_.attempted;
  CachedInstruction &cached = cache_[state_.pc];
  if (!cached.decoded) {
    cached.result = decode_line(program_.instructions[state_.pc].text);
    cached.decoded = true;
  }
  if (cached.result.error != ErrorCode::None) {
    return fault(result, cached.result.error);
  }
  const DecodedInstruction instruction = cached.result.instruction;
  const Opcode opcode = instruction.opcode;
  if (!level_.allowed_opcodes.test(static_cast<std::size_t>(opcode))) {
    return fault(result, ErrorCode::OpcodeNotAllowed);
  }

  const bool uses_floor = opcode == Opcode::Add || opcode == Opcode::Sub ||
    opcode == Opcode::CopyTo || opcode == Opcode::CopyFrom;
  const bool jumps = opcode == Opcode::Jump || opcode == Opcode::JumpIfZero;
  if (uses_floor && !valid_index(instruction.operand, state_.floor.size())) {
    return fault(result, ErrorCode::InvalidFloorIndex);
  }
  if (jumps &&
    (instruction.operand < 1 ||
      !valid_index(instruction.operand - 1, program_.instructions.size()))) {
    return fault(result, ErrorCode::InvalidJumpTarget);
  }
  const bool needs_hand = opcode == Opcode::Outbox || opcode == Opcode::Add ||
    opcode == Opcode::Sub || opcode == Opcode::CopyTo || opcode == Opcode::JumpIfZero;
  if (needs_hand && !state_.hand.occupied) {
    return fault(result, ErrorCode::EmptyHand);
  }
  const std::size_t floor_index = uses_floor ? static_cast<std::size_t>(instruction.operand) : 0;
  if (uses_floor && opcode != Opcode::CopyTo && !state_.floor[floor_index].occupied) {
    return fault(result, ErrorCode::EmptyFloorCell);
  }

  std::size_t next_pc = state_.pc + 1;
  switch (opcode) {
  case Opcode::Inbox:
    if (state_.input_cursor == level_.input.size()) {
      halt(HaltReason::InboxEmpty);
      result.outcome = StepOutcome::Halted;
      return result;
    }
    state_.hand = Cell(level_.input[state_.input_cursor++]);
    break;
  case Opcode::Outbox:
    state_.output.push_back(state_.hand.value);
    state_.hand = Cell();
    break;
  case Opcode::CopyTo:
    state_.floor[floor_index] = state_.hand;
    break;
  case Opcode::CopyFrom:
    state_.hand = state_.floor[floor_index];
    break;
  case Opcode::Add:
  case Opcode::Sub: {
    Value value = 0;
    if (!checked_arithmetic(
          state_.hand.value,
          state_.floor[floor_index].value,
          opcode == Opcode::Sub,
          value
        )) {
      return fault(result, ErrorCode::ArithmeticOverflow);
    }
    state_.hand.value = value;
    break;
  }
  case Opcode::Jump:
    next_pc = static_cast<std::size_t>(instruction.operand - 1);
    break;
  case Opcode::JumpIfZero:
    if (state_.hand.value == 0) {
      next_pc = static_cast<std::size_t>(instruction.operand - 1);
    }
    break;
  }

  ++state_.completed;
  state_.pc = next_pc;
  result.next_pc = next_pc;
  result.completed = true;
  result.outcome = StepOutcome::Advanced;
  if (next_pc == program_.instructions.size()) {
    halt(HaltReason::ProgramEnd);
    result.outcome = StepOutcome::Halted;
  }
  return result;
}

} // namespace hrm
