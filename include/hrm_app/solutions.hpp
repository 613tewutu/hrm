#pragma once

#include "hrm/core.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hrm_app {

/// Resolve XDG_CONFIG_HOME/hrm, falling back to HOME/.config/hrm.
/// @throws std::runtime_error If neither configuration environment variable is available.
std::filesystem::path solution_directory();

/// Store successful programs independently of frontend state and the C++11 kernel.
class SolutionStore {
public:
  /// Use the supplied directory without creating it until a solution is saved.
  explicit SolutionStore(std::filesystem::path directory);
  /// Load a solution only when its level data match and replay still succeeds.
  ///
  /// @param level Current level rules, input, and target used to verify the saved solution.
  /// @return Instruction text, or no value for a missing or outdated solution.
  /// @throws std::runtime_error If a file is unreadable, malformed, or fails verification.
  std::optional<std::vector<std::string>> load(const hrm::LevelDefinition &level) const;
  /// Atomically replace a level's saved solution after successful execution.
  ///
  /// @param machine Finished successful machine, including the original program and level.
  /// @throws std::runtime_error If execution did not succeed or writing fails.
  void save(const hrm::Machine &machine) const;

private:
  std::filesystem::path directory_;
};

} // namespace hrm_app
