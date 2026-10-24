#include "hrm_app/robot_animation.hpp"

#include <algorithm>
#include <cmath>

namespace hrm_app {
namespace {

/// Return a smooth interpolation factor with stationary endpoints.
double ease(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value * value * (3.0 - 2.0 * value);
}

/// Return whether two locations refer to the same physical workroom position.
bool same_place(RobotLocation left, RobotLocation right) {
  return left.place == right.place &&
    (left.place != RobotPlace::Floor || left.floor == right.floor);
}

} // namespace

bool RobotAnimation::start(
  hrm::DecodedInstruction instruction,
  const hrm::StepResult &result,
  const hrm::MachineState &before,
  const hrm::MachineState &after,
  std::uint64_t row
) {
  if (result.outcome == hrm::StepOutcome::AlreadyStopped) {
    return false;
  }
  finish();
  before_ = before;
  after_ = after;
  frame_ = RobotFrame();
  frame_.from = location_;
  frame_.to = location_;
  frame_.phase = 0.0;
  frame_.row = row;
  active_ = true;
  if (result.outcome == hrm::StepOutcome::Faulted) {
    frame_.effect = RobotEffect::Fault;
    return true;
  }
  switch (instruction.opcode) {
  case hrm::Opcode::Inbox:
    frame_.to.place = RobotPlace::Inbox;
    frame_.effect = result.completed ? RobotEffect::Pickup : RobotEffect::EmptyInbox;
    break;
  case hrm::Opcode::Outbox:
    frame_.to.place = RobotPlace::Outbox;
    frame_.effect = RobotEffect::Deliver;
    break;
  case hrm::Opcode::CopyTo:
  case hrm::Opcode::CopyFrom:
  case hrm::Opcode::Add:
  case hrm::Opcode::Sub:
    frame_.to.place = RobotPlace::Floor;
    frame_.to.floor = static_cast<std::size_t>(instruction.operand);
    frame_.effect = instruction.opcode == hrm::Opcode::CopyTo ? RobotEffect::Store
      : instruction.opcode == hrm::Opcode::CopyFrom           ? RobotEffect::Copy
      : instruction.opcode == hrm::Opcode::Add                ? RobotEffect::Add
                                                              : RobotEffect::Subtract;
    break;
  case hrm::Opcode::Jump:
    frame_.effect = RobotEffect::Jump;
    break;
  case hrm::Opcode::JumpIfZero:
    frame_.effect = before.hand.value == 0 ? RobotEffect::Jump : RobotEffect::Continue;
    break;
  }
  return true;
}

bool RobotAnimation::advance(double seconds, int speed, bool paused) {
  if (!active_ || paused || seconds <= 0.0 || !std::isfinite(seconds)) {
    return false;
  }
  frame_.phase = std::min(1.0, frame_.phase + seconds * std::clamp(speed, 1, 12));
  if (frame_.phase >= 1.0) {
    finish();
  }
  return true;
}

bool RobotAnimation::active() const {
  return active_;
}

RobotFrame RobotAnimation::frame() const {
  auto result = frame_;
  result.travel = ease(result.phase / 0.55);
  result.interaction = std::clamp((result.phase - 0.55) / 0.30, 0.0, 1.0);
  result.transferred = result.phase >= 0.70;
  result.walking = active_ && result.phase < 0.55 && !same_place(result.from, result.to);
  result.gait = static_cast<int>(result.phase * 6.0) % 2;
  return result;
}

const hrm::MachineState &RobotAnimation::display_state(const hrm::MachineState &idle) const {
  return !active_ ? idle : frame().transferred ? after_ : before_;
}

const hrm::MachineState &RobotAnimation::before() const {
  return before_;
}
const hrm::MachineState &RobotAnimation::after() const {
  return after_;
}

void RobotAnimation::finish() {
  if (active_) {
    location_ = frame_.to;
  }
  active_ = false;
  frame_.phase = 1.0;
}

void RobotAnimation::reset() {
  location_ = RobotLocation();
  frame_ = RobotFrame();
  before_ = hrm::MachineState();
  after_ = hrm::MachineState();
  active_ = false;
}

void RobotAnimation::seek(
  const hrm::LevelDefinition &level,
  const hrm::Program &program,
  std::uint64_t attempts
) {
  reset();
  hrm::Machine replay(level, program);
  for (std::uint64_t i = 0; i < attempts && replay.state().status == hrm::ExecutionStatus::Running;
    ++i) {
    const auto before = replay.state();
    const auto instruction = hrm::decode_line(program.instructions[before.pc].text).instruction;
    const auto result = replay.step();
    start(instruction, result, before, replay.state(), 0);
    finish();
  }
}

} // namespace hrm_app
