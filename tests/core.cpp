#include "hrm/core.hpp"

#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#define CHECK(expression) \
  do { \
    if (!(expression)) { \
      throw std::runtime_error( \
        std::string("Line ") + std::to_string(__LINE__) + ": " + #expression \
      ); \
    } \
  } while (false)

namespace {

/// Build a source program while preserving every supplied instruction slot.
hrm::Program program(std::initializer_list<const char *> lines) {
  hrm::Program result;
  for (const char *line : lines) {
    hrm::SourceInstruction instruction;
    instruction.text = line;
    result.instructions.push_back(instruction);
  }
  return result;
}

/// Build a small unrestricted level for focused state-transition tests.
hrm::LevelDefinition custom(
  std::initializer_list<hrm::Value> input,
  std::initializer_list<hrm::Value> target,
  std::size_t floor = 1
) {
  hrm::LevelDefinition level;
  level.input = input;
  level.target = target;
  level.floor_size = floor;
  level.allowed_opcodes.set();
  return level;
}

/// Load a known built-in level and fail immediately if it is missing.
hrm::LevelDefinition builtin(int id) {
  hrm::LevelDefinition level;
  CHECK(hrm::load_builtin_level(id, level));
  return level;
}

/// Execute a bounded regression program and fail if it does not terminate.
void finish(hrm::Machine &machine) {
  for (int i = 0; i < 1000 && machine.state().status == hrm::ExecutionStatus::Running; ++i) {
    machine.step();
  }
  CHECK(machine.state().status != hrm::ExecutionStatus::Running);
}

/// Verify the terminal diagnostic and original instruction ID of a failing program.
void expect_fault(
  hrm::LevelDefinition level,
  hrm::Program source,
  hrm::ErrorCode code,
  std::size_t instruction
) {
  hrm::Machine machine(std::move(level), std::move(source));
  finish(machine);
  CHECK(machine.state().verdict == hrm::Verdict::Error);
  CHECK(machine.state().diagnostic.code == code);
  CHECK(machine.state().diagnostic.instruction == instruction);
}

/// Verify strict integer tokens, opcode recognition, whitespace, and operand arity.
void decoder_tests() {
  using hrm::ErrorCode;
  CHECK(hrm::parse_integer("").error == ErrorCode::InvalidInteger);
  CHECK(hrm::parse_integer("+").error == ErrorCode::InvalidInteger);
  CHECK(hrm::parse_integer("1x").error == ErrorCode::InvalidInteger);
  CHECK(hrm::parse_integer(" 1").error == ErrorCode::InvalidInteger);
  CHECK(hrm::parse_integer("1.0").error == ErrorCode::InvalidInteger);
  CHECK(hrm::parse_integer("+00012").value == 12);
  CHECK(hrm::parse_integer("-0").value == 0);
  CHECK(hrm::parse_integer("9223372036854775807").value == std::numeric_limits<hrm::Value>::max());
  CHECK(hrm::parse_integer("-9223372036854775808").value == std::numeric_limits<hrm::Value>::min());
  CHECK(hrm::parse_integer("9223372036854775808").error == ErrorCode::OperandOutOfRange);
  CHECK(hrm::parse_integer("-9223372036854775809").error == ErrorCode::OperandOutOfRange);
  CHECK(hrm::parse_integer(std::string(200, '9')).error == ErrorCode::OperandOutOfRange);

  const char *valid[] =
    {"inbox", "outbox", "add 0", "sub -1", "copyto +1", "copyfrom 2", "jump 1", "jumpifzero 2"};
  for (std::size_t i = 0; i < 8; ++i) {
    const hrm::DecodeResult result = hrm::decode_line(valid[i]);
    CHECK(result.error == ErrorCode::None);
    CHECK(static_cast<std::size_t>(result.instruction.opcode) == i);
  }
  CHECK(hrm::decode_line(" \tcopyto\t+0 \r").error == ErrorCode::None);
  for (const char *line : {"inbox 1", "outbox 1", "add", "jump", "sub 1 2"}) {
    CHECK(hrm::decode_line(line).error == ErrorCode::WrongOperandCount);
  }
  for (const char *line : {"", "INBOX", "unknown", "# comment"}) {
    CHECK(hrm::decode_line(line).error == ErrorCode::UnknownOpcode);
  }
  CHECK(hrm::decode_line("add 1x").error == ErrorCode::InvalidInteger);
}

/// Execute complete solutions for the three assignment levels.
void builtin_tests() {
  hrm::Machine first(builtin(1), program({"inbox", "outbox", "inbox", "outbox"}));
  finish(first);
  CHECK(first.state().verdict == hrm::Verdict::Success);
  CHECK(first.state().halt_reason == hrm::HaltReason::ProgramEnd);
  CHECK(first.state().completed == 4);

  hrm::Machine second(
    builtin(2),
    program(
      {"inbox",
        "copyto 0",
        "inbox",
        "copyto 1",
        "copyfrom 0",
        "sub 1",
        "outbox",
        "copyfrom 1",
        "sub 0",
        "outbox",
        "jump 1"}
    )
  );
  finish(second);
  CHECK(second.state().verdict == hrm::Verdict::Success);
  CHECK(second.state().halt_reason == hrm::HaltReason::InboxEmpty);

  hrm::Machine third(
    builtin(3),
    program(
      {"inbox",
        "copyto 0",
        "inbox",
        "sub 0",
        "jumpifzero 7",
        "jump 1",
        "copyfrom 0",
        "outbox",
        "jump 1"}
    )
  );
  finish(third);
  CHECK(third.state().verdict == hrm::Verdict::Success);
  hrm::LevelDefinition level = builtin(1);
  CHECK(!hrm::load_builtin_level(4, level));
  CHECK(level.id == 1 && level.input == builtin(1).input);
}

/// Verify hand/floor ownership, output changes, terminal calls, and reset determinism.
void state_tests() {
  hrm::Machine machine(
    custom({0, 7}, {7, 0}),
    program({"inbox", "copyto 0", "inbox", "outbox", "copyfrom 0", "outbox"})
  );
  CHECK(!machine.state().hand.occupied && !machine.state().floor[0].occupied);
  machine.step();
  CHECK(machine.state().hand.occupied && machine.state().hand.value == 0);
  machine.step();
  CHECK(machine.state().hand.occupied && machine.state().floor[0].occupied);
  machine.step();
  CHECK(machine.state().hand.value == 7 && machine.state().floor[0].value == 0);
  machine.step();
  CHECK(!machine.state().hand.occupied);
  finish(machine);
  CHECK(machine.state().verdict == hrm::Verdict::Success);
  CHECK(machine.state().floor[0].occupied && machine.state().floor[0].value == 0);
  const hrm::MachineState stopped = machine.state();
  const hrm::StepResult repeat = machine.step();
  CHECK(repeat.outcome == hrm::StepOutcome::AlreadyStopped);
  CHECK(repeat.instruction == 0 && machine.state().attempted == stopped.attempted);
  CHECK(machine.state().pc == stopped.pc && machine.state().output == stopped.output);
  machine.reset();
  CHECK(machine.state().status == hrm::ExecutionStatus::Running);
  CHECK(machine.state().pc == 0 && machine.state().attempted == 0);
  CHECK(machine.state().output.empty() && !machine.state().floor[0].occupied);
  finish(machine);
  CHECK(machine.state().output == stopped.output);

  hrm::Machine overwrite(
    custom({2, 9}, {9}),
    program({"inbox", "copyto 0", "inbox", "copyto 0", "copyfrom 0", "outbox"})
  );
  finish(overwrite);
  CHECK(overwrite.state().verdict == hrm::Verdict::Success);
  CHECK(overwrite.state().floor[0].value == 9);

  hrm::Machine arithmetic(
    custom({3, 10}, {16, 13}),
    program(
      {"inbox",
        "copyto 0",
        "inbox",
        "add 0",
        "add 0",
        "outbox",
        "copyfrom 0",
        "add 0",
        "copyto 0",
        "inbox"}
    )
  );
  finish(arithmetic);
  CHECK(arithmetic.state().output == std::vector<hrm::Value>({16}));
  CHECK(arithmetic.state().floor[0].value == 6);
  CHECK(arithmetic.state().hand.value == 6); // Empty inbox retains the hand.
  CHECK(arithmetic.state().completed == 9 && arithmetic.state().attempted == 10);
  CHECK(arithmetic.state().pc == 9);
}

/// Verify bounds, occupancy, permissions, original IDs, and atomic runtime failures.
void fault_tests() {
  using hrm::ErrorCode;
  expect_fault(builtin(1), program({"jump 1"}), ErrorCode::OpcodeNotAllowed, 1);
  expect_fault(builtin(2), program({"outbox"}), ErrorCode::EmptyHand, 1);
  expect_fault(builtin(2), program({"copyto 0"}), ErrorCode::EmptyHand, 1);
  expect_fault(builtin(2), program({"copyfrom 0"}), ErrorCode::EmptyFloorCell, 1);
  expect_fault(builtin(2), program({"inbox", "add 0"}), ErrorCode::EmptyFloorCell, 2);
  expect_fault(builtin(2), program({"inbox", "sub 0"}), ErrorCode::EmptyFloorCell, 2);
  for (const char *line : {"copyto -1", "copyfrom 3", "add 3", "sub -1"}) {
    expect_fault(builtin(2), program({"inbox", line}), ErrorCode::InvalidFloorIndex, 2);
  }
  for (const char *line : {"jump 0", "jump -1", "jump 2", "jump 9223372036854775807"}) {
    expect_fault(builtin(2), program({line}), ErrorCode::InvalidJumpTarget, 1);
  }
  expect_fault(builtin(2), program({"jumpifzero 1"}), ErrorCode::EmptyHand, 1);
  expect_fault(builtin(2), program({"inbox", "jumpifzero 99"}), ErrorCode::InvalidJumpTarget, 2);
  expect_fault(builtin(2), program({"jump 2", "bad"}), ErrorCode::UnknownOpcode, 2);
  expect_fault(
    builtin(1),
    program({"inbox", "outbox", "inbox", "outbox", "outbox"}),
    ErrorCode::EmptyHand,
    5
  );

  hrm::Machine atomic(custom({8}, {}), program({"inbox", "add 0"}));
  atomic.step();
  const hrm::MachineState before = atomic.state();
  const hrm::StepResult fault = atomic.step();
  CHECK(fault.outcome == hrm::StepOutcome::Faulted && !fault.completed);
  CHECK(atomic.state().pc == before.pc && atomic.state().hand.value == before.hand.value);
  CHECK(
    atomic.state().input_cursor == before.input_cursor && atomic.state().output == before.output
  );
  CHECK(!atomic.state().floor[0].occupied);
  CHECK(atomic.state().completed == 1 && atomic.state().attempted == 2);
  atomic.step();
  CHECK(atomic.state().attempted == 2);

  hrm::Program physical = program({""});
  physical.instructions[0].source_line = 900;
  expect_fault(builtin(1), physical, ErrorCode::UnknownOpcode, 1);
}

/// Check deferred validation, both conditional branches, and repeated instruction IDs.
void reachability_tests() {
  for (const char *skipped : {"bad", "", "add nope", "jump 99", "copyto -1", "inbox 1"}) {
    hrm::Machine machine(builtin(2), program({"jump 3", skipped, "inbox"}));
    finish(machine);
    CHECK(machine.state().verdict == hrm::Verdict::Fail);
  }
  hrm::Machine early(
    builtin(1),
    program({"inbox", "outbox", "inbox", "outbox", "inbox", "copyto nonsense"})
  );
  finish(early);
  CHECK(early.state().verdict == hrm::Verdict::Success);
  CHECK(early.state().completed == 4 && early.state().attempted == 5);
  hrm::Machine branch(custom({0}, {0}), program({"inbox", "jumpifzero 4", "bad", "outbox"}));
  finish(branch);
  CHECK(branch.state().verdict == hrm::Verdict::Success);
  hrm::Machine nonzero(custom({2}, {2}), program({"inbox", "jumpifzero 4", "outbox", "inbox"}));
  finish(nonzero);
  CHECK(nonzero.state().verdict == hrm::Verdict::Success);

  hrm::Machine loop(builtin(2), program({"jump 1"}));
  for (int i = 0; i < 100; ++i) {
    CHECK(loop.step().instruction == 1);
  }
  CHECK(loop.state().status == hrm::ExecutionStatus::Running);
  CHECK(loop.state().verdict == hrm::Verdict::Pending && loop.state().pc == 0);
  CHECK(loop.state().completed == 100);

  hrm::Machine repeated(custom({4, 5}, {}), program({"inbox", "jumpifzero 1", "outbox", "jump 2"}));
  finish(repeated);
  CHECK(repeated.state().diagnostic.instruction == 2);
  CHECK(repeated.state().attempted == 5); // Instruction 2 faults on its second visit.
}

/// Check empty programs, empty inbox termination, and exact output comparison.
void termination_tests() {
  hrm::Machine empty(custom({}, {}), program({}));
  CHECK(empty.state().verdict == hrm::Verdict::Success);
  CHECK(empty.step().outcome == hrm::StepOutcome::AlreadyStopped);
  hrm::Machine missing(builtin(1), program({}));
  CHECK(missing.state().verdict == hrm::Verdict::Fail);
  hrm::Machine retain(custom({8}, {}), program({"inbox", "inbox", "bad"}));
  finish(retain);
  CHECK(retain.state().hand.occupied && retain.state().hand.value == 8);
  CHECK(retain.state().halt_reason == hrm::HaltReason::InboxEmpty);
  hrm::Machine exhausted(custom({8}, {8}), program({"inbox", "outbox"}));
  exhausted.step();
  CHECK(exhausted.state().status == hrm::ExecutionStatus::Running);
  CHECK(exhausted.step().completed);
  CHECK(exhausted.state().verdict == hrm::Verdict::Success);
  for (const hrm::LevelDefinition &level :
    {custom({1, 2}, {1}), custom({1}, {1, 2}), custom({2, 1}, {1, 2}), custom({3, 4}, {1, 2})}) {
    hrm::Machine machine(level, program({"inbox", "outbox", "inbox", "outbox"}));
    finish(machine);
    CHECK(machine.state().verdict == hrm::Verdict::Fail);
  }
}

/// Exercise both arithmetic boundaries without triggering undefined signed overflow.
void overflow_tests() {
  const hrm::Value maximum = std::numeric_limits<hrm::Value>::max();
  const hrm::Value minimum = std::numeric_limits<hrm::Value>::min();
  for (const auto &inputs : {std::vector<hrm::Value>{maximum, 1}, {minimum, -1}}) {
    hrm::LevelDefinition level = custom({}, {});
    level.input = inputs;
    hrm::Machine machine(level, program({"inbox", "copyto 0", "inbox", "add 0"}));
    finish(machine);
    CHECK(machine.state().diagnostic.code == hrm::ErrorCode::ArithmeticOverflow);
    CHECK(machine.state().hand.value == inputs[1]);
    CHECK(machine.state().floor[0].value == inputs[0]);
    CHECK(machine.state().pc == 3 && machine.state().completed == 3);
  }
  expect_fault(
    custom({-1, maximum}, {}),
    program({"inbox", "copyto 0", "inbox", "sub 0"}),
    hrm::ErrorCode::ArithmeticOverflow,
    4
  );
  expect_fault(
    custom({1, minimum}, {}),
    program({"inbox", "copyto 0", "inbox", "sub 0"}),
    hrm::ErrorCode::ArithmeticOverflow,
    4
  );
  hrm::Machine boundary(
    custom({minimum}, {minimum}),
    program({"inbox", "copyto 0", "sub 0", "add 0", "outbox"})
  );
  finish(boundary);
  CHECK(boundary.state().verdict == hrm::Verdict::Success);
}

} // namespace

/// Run kernel regressions and report the first failing invariant.
int main() {
  try {
    decoder_tests();
    builtin_tests();
    state_tests();
    fault_tests();
    reachability_tests();
    termination_tests();
    overflow_tests();
    std::cout << "Kernel tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
