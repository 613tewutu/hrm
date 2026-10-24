#pragma once

#include "hrm/core.hpp"

#include <cstdint>

namespace hrm_app {

enum class RobotPlace { Home, Inbox, Outbox, Floor };
enum class RobotEffect {
  Idle,
  Pickup,
  Deliver,
  Store,
  Copy,
  Add,
  Subtract,
  Jump,
  Continue,
  EmptyInbox,
  Fault
};

/// Address a physical station independently of terminal dimensions.
struct RobotLocation {
  RobotPlace place = RobotPlace::Home;
  std::size_t floor = 0;
};

/// Provide normalized 0-1 phases and logical endpoints to responsive renderers.
struct RobotFrame {
  RobotLocation from;
  RobotLocation to;
  RobotEffect effect = RobotEffect::Idle;
  double phase = 1.0;
  double travel = 1.0;
  double interaction = 1.0;
  bool transferred = true;
  bool walking = false;
  int gait = 0;
  std::uint64_t row = 0;
};

/// Animate completed kernel attempts without changing execution semantics.
class RobotAnimation {
public:
  /// Capture a single attempted instruction for visual playback.
  ///
  /// @param instruction Decoded command; invalid commands are represented by their fault result.
  /// @param result Actual kernel transition, including terminal empty-inbox attempts.
  /// @param before State to display before the interaction's transfer point.
  /// @param after State to display after that point.
  /// @param row Stable command-card ID to keep highlighted throughout the animation.
  /// @return True when an animation starts; terminal no-op calls leave the pose unchanged.
  bool start(
    hrm::DecodedInstruction instruction,
    const hrm::StepResult &result,
    const hrm::MachineState &before,
    const hrm::MachineState &after,
    std::uint64_t row
  );
  /// Advance visual time at the selected instruction rate.
  ///
  /// @param seconds Elapsed monotonic time; negative intervals are ignored.
  /// @param speed Instruction rate clamped to the transport's 1-12 range.
  /// @param paused Whether the current pose must stay frozen.
  /// @return True when the frame changes, including the final settled pose.
  bool advance(double seconds, int speed, bool paused);
  /// Return whether a captured attempt is still animating.
  bool active() const;
  /// Return the interpolated motion and interaction phases.
  RobotFrame frame() const;
  /// Return the state appropriate to the visual transfer point, or the live idle state.
  const hrm::MachineState &display_state(const hrm::MachineState &idle) const;
  /// Return the captured source state for block-transfer effects.
  const hrm::MachineState &before() const;
  /// Return the captured destination state for block-transfer effects.
  const hrm::MachineState &after() const;
  /// Settle the captured attempt immediately without executing another instruction.
  void finish();
  /// Clear visual history and restore the robot's home position.
  void reset();
  /// Reconstruct a settled robot location after rewinding or changing levels.
  ///
  /// @param level Level used by the replayed machine.
  /// @param program Original program to replay without presentation side effects.
  /// @param attempts Number of attempted instructions to restore.
  void seek(const hrm::LevelDefinition &level, const hrm::Program &program, std::uint64_t attempts);

private:
  RobotLocation location_;
  RobotFrame frame_;
  hrm::MachineState before_;
  hrm::MachineState after_;
  bool active_ = false;
};

} // namespace hrm_app
