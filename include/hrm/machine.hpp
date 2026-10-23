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
  Machine(LevelDefinition level, Program program);

  // Execute at most one instruction; terminal calls leave all state unchanged.
  StepResult step();
  const MachineState &state() const;
  const LevelDefinition &level() const;
  const Program &program() const;
  void reset();

private:
  struct CachedInstruction {
    bool decoded = false;
    DecodeResult result;
  };

  void halt(HaltReason reason);
  StepResult fault(StepResult result, ErrorCode code);

  LevelDefinition level_;
  Program program_;
  MachineState state_;
  std::vector<CachedInstruction> cache_;
};

} // namespace hrm
