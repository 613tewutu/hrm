#pragma once

#include "hrm/level.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace hrm_app {

struct GameLevel {
  std::string title;
  hrm::LevelDefinition definition;
};

struct GameConfig {
  std::string title;
  std::vector<GameLevel> levels;
};

/// Load and validate YAML game metadata and level definitions.
///
/// @param path Configuration file using schema version 1 and unique positive level IDs.
/// @return The validated game configuration, with empty player programs.
/// @throws std::runtime_error If reading, YAML conversion, or schema validation fails.
GameConfig load_config(const std::filesystem::path &path);

/// Load validated default levels embedded from assets/game.yaml during the build.
/// @return The bundled game configuration without accessing external resource files.
/// @throws std::runtime_error If embedded configuration validation fails.
GameConfig load_bundled_config();

} // namespace hrm_app
