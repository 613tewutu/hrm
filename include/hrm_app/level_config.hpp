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

// Throw a descriptive exception on an invalid or unreadable YAML configuration.
GameConfig load_config(const std::filesystem::path &path);

} // namespace hrm_app
