#include "hrm_app/solutions.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace hrm_app {
namespace {

/// Serialize execution-relevant data so changed YAML levels cannot reuse stale completion.
std::string level_identity(const hrm::LevelDefinition &level) {
  YAML::Emitter output;
  output << YAML::BeginSeq << level.id << level.input << level.target << level.floor_size
         << level.allowed_opcodes.to_string() << YAML::EndSeq;
  return output.c_str();
}

} // namespace

std::filesystem::path solution_directory() {
  if (const char *config = std::getenv("XDG_CONFIG_HOME"); config && *config) {
    if (std::filesystem::path(config).is_absolute()) {
      return std::filesystem::path(config) / "hrm";
    }
  }
  if (const char *home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / ".config/hrm";
  }
  throw std::runtime_error("Cannot locate the HRM solution directory: HOME is unset");
}

SolutionStore::SolutionStore(std::filesystem::path directory)
  : directory_(std::move(directory)) {
}

std::optional<std::vector<std::string>>
SolutionStore::load(const hrm::LevelDefinition &level) const {
  const auto path = directory_ / ("level-" + std::to_string(level.id) + ".yaml");
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }
  try {
    const auto document = YAML::LoadFile(path.string());
    if (document["version"].as<int>() != 1) {
      throw std::runtime_error("Unsupported solution version");
    }
    if (document["level"].as<std::string>() != level_identity(level)) {
      return std::nullopt;
    }
    const auto attempts = document["attempts"].as<std::uint64_t>();
    if (attempts == 0 || attempts > 1000000) {
      throw std::runtime_error("Invalid solution replay length");
    }
    const auto lines = document["program"].as<std::vector<std::string>>();
    hrm::Program program;
    for (const auto &line : lines) {
      if (hrm::decode_line(line).error != hrm::ErrorCode::None) {
        throw std::runtime_error("Invalid saved instruction");
      }
      program.instructions.push_back({line, 0});
    }
    hrm::Machine machine(level, program);
    for (std::uint64_t i = 0;
      i < attempts && machine.state().status == hrm::ExecutionStatus::Running;
      ++i) {
      machine.step();
    }
    if (machine.state().verdict != hrm::Verdict::Success || machine.state().attempted != attempts) {
      throw std::runtime_error("Saved program no longer solves this level");
    }
    return lines;
  } catch (const std::exception &error) {
    throw std::runtime_error("Cannot load " + path.string() + ": " + error.what());
  }
}

void SolutionStore::save(const hrm::Machine &machine) const {
  if (machine.state().verdict != hrm::Verdict::Success || machine.state().attempted == 0 ||
    machine.state().attempted > 1000000) {
    throw std::runtime_error("Only successful programs within the replay limit can be saved");
  }
  std::vector<std::string> lines;
  for (const auto &instruction : machine.program().instructions) {
    lines.push_back(instruction.text);
  }
  YAML::Emitter output;
  output << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1 << YAML::Key << "level"
         << YAML::Value << level_identity(machine.level()) << YAML::Key << "attempts" << YAML::Value
         << machine.state().attempted << YAML::Key << "program" << YAML::Value << lines
         << YAML::EndMap;
  std::filesystem::create_directories(directory_);
  const auto destination = directory_ / ("level-" + std::to_string(machine.level().id) + ".yaml");
  auto temporary = destination;
  temporary += ".tmp";
  try {
    std::ofstream file(temporary, std::ios::trunc);
    file.exceptions(std::ios::failbit | std::ios::badbit);
    file << output.c_str() << '\n';
    file.close();
    std::filesystem::rename(temporary, destination);
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

} // namespace hrm_app
