#pragma once

#include "hrm/level.hpp"
#include "hrm/program.hpp"

namespace hrm {

struct MachineState {
  std::size_t pc = 0; // Next 0-based instruction index.
  std::size_t input_cursor = 0;
  Cell hand;
  std::vector<Cell> floor;
  std::vector<Value> output;
  std::uint64_t attempted = 0;
  std::uint64_t completed = 0;
  ExecutionStatus status = ExecutionStatus::Running;
  HaltReason halt_reason = HaltReason::None;
  Verdict verdict = Verdict::Pending;
  Diagnostic diagnostic;
};

struct StepResult {
  StepOutcome outcome = StepOutcome::AlreadyStopped;
  std::size_t instruction = 0; // Zero when no instruction was attempted.
  std::size_t previous_pc = 0;
  std::size_t next_pc = 0;
  bool completed = false;
  Diagnostic diagnostic;
};

class Machine {
public:
  /// Own a level and program and initialize a deterministic execution session.
  ///
  /// @param level Input, expected output, floor size, and allowed instructions.
  /// @param program Original instruction slots; malformed lines remain deferred until reached.
  Machine(LevelDefinition level, Program program);

  /// Attempt the current instruction and atomically apply its effects when valid.
  ///
  /// Syntax and runtime errors are checked only for the reached instruction.
  /// Errors preserve the data state and program counter while recording the attempt.
  /// An empty inbox or falling off the program ends execution and evaluates output.
  ///
  /// @return The attempted instruction ID and transition; terminal calls return AlreadyStopped.
  StepResult step();
  /// Return the current read-only machine state.
  const MachineState &state() const;
  /// Return the owned level definition.
  const LevelDefinition &level() const;
  /// Return the owned original program.
  const Program &program() const;
  /// Restart the program with preset floor cells and empty hand, output, counters, and cache.
  void reset();

private:
  struct CachedInstruction {
    bool decoded = false;
    DecodeResult result;
  };

  /// Halt normally and compare the complete output against the target.
  void halt(HaltReason reason);
  /// Record a reached instruction error without changing its program counter or data.
  ///
  /// @param result Attempt metadata created before the failing instruction executes.
  /// @param code The syntax or runtime failure to attach to the terminal state.
  /// @return The same step metadata marked Faulted with the original instruction ID.
  StepResult fault(StepResult result, ErrorCode code);

  LevelDefinition level_;
  Program program_;
  MachineState state_;
  std::vector<CachedInstruction> cache_;
};

} // namespace hrm
