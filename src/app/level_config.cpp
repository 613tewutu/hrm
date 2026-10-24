#include "hrm_app/level_config.hpp"

#include "hrm/program.hpp"

#include <set>
#include <stdexcept>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace hrm_app {
namespace {

const unsigned char bundled_game[] = {
#include "game.yaml.h"
};

/// Throw a configuration error when a required schema condition is false.
void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

/// Convert a YAML sequence to exact signed 64-bit values.
///
/// @param node Sequence whose scalar tokens are validated without partial conversions.
/// @param field Field name included in validation diagnostics.
/// @return Values in their original order.
/// @throws std::runtime_error If the sequence or any integer token is invalid.
std::vector<hrm::Value> values(const YAML::Node &node, const std::string &field) {
  require(node.IsSequence(), field + " must be a sequence of integers");
  std::vector<hrm::Value> result;
  for (const auto &item : node) {
    require(item.IsScalar(), field + " must contain integers");
    const auto parsed = hrm::parse_integer(item.Scalar());
    require(parsed.error == hrm::ErrorCode::None, field + " contains an invalid 64-bit integer");
    result.push_back(parsed.value);
  }
  return result;
}

/// Apply the same schema validation to embedded and explicitly selected YAML documents.
GameConfig decode_config(const YAML::Node &root) {
  require(root.IsMap(), "game configuration must be a map");
  require(root["version"].as<int>() == 1, "unsupported configuration version");
  GameConfig config;
  config.title = root["title"].as<std::string>();
  const auto levels = root["levels"];
  require(levels.IsSequence() && levels.size() > 0, "levels must be a nonempty sequence");
  std::set<int> ids;
  for (const auto &node : levels) {
    require(node.IsMap(), "each level must be a map");
    GameLevel level;
    auto &definition = level.definition;
    definition.id = node["id"].as<int>();
    require(
      definition.id > 0 && ids.insert(definition.id).second,
      "level IDs must be unique and positive"
    );
    level.title = node["title"].as<std::string>();
    definition.description = node["description"].as<std::string>();
    definition.input = values(node["input"], "input");
    definition.target = values(node["target"], "target");
    const auto floor = hrm::parse_integer(node["floor_size"].as<std::string>());
    require(
      floor.error == hrm::ErrorCode::None && floor.value >= 0 && floor.value <= 1024,
      "floor_size must be between 0 and 1024"
    );
    definition.floor_size = static_cast<std::size_t>(floor.value);
    if (const auto initial = node["initial_floor"]) {
      require(initial.IsMap(), "initial_floor must map floor indices to integer values");
      std::set<std::size_t> initialized;
      for (const auto &entry : initial) {
        require(entry.first.IsScalar() && entry.second.IsScalar(), "invalid initial_floor cell");
        const auto index = hrm::parse_integer(entry.first.Scalar());
        const auto value = hrm::parse_integer(entry.second.Scalar());
        require(
          index.error == hrm::ErrorCode::None && index.value >= 0 &&
            static_cast<std::uint64_t>(index.value) < definition.floor_size,
          "initial_floor index must name an existing floor slot"
        );
        require(value.error == hrm::ErrorCode::None, "initial_floor values must be integers");
        const auto slot = static_cast<std::size_t>(index.value);
        require(initialized.insert(slot).second, "initial_floor indices must be unique");
        definition.initial_floor.resize(definition.floor_size);
        definition.initial_floor[slot] = hrm::Cell(value.value);
      }
    }
    const auto instructions = node["instructions"];
    require(
      instructions.IsSequence() && instructions.size() > 0,
      "instructions must be a nonempty sequence"
    );
    for (const auto &item : instructions) {
      const auto name = item.as<std::string>();
      auto decoded = hrm::decode_line(name);
      if (decoded.error == hrm::ErrorCode::WrongOperandCount) {
        decoded = hrm::decode_line(name + " 0");
      }
      require(
        decoded.error == hrm::ErrorCode::None && name.find_first_of(" \t\r\n") == std::string::npos,
        "unknown instruction: " + name
      );
      definition.allowed_opcodes.set(static_cast<std::size_t>(decoded.instruction.opcode));
    }
    config.levels.push_back(std::move(level));
  }
  return config;
}

} // namespace

GameConfig load_config(const std::filesystem::path &path) {
  try {
    return decode_config(YAML::LoadFile(path.string()));
  } catch (const std::exception &error) {
    throw std::runtime_error(path.string() + ": " + error.what());
  }
}

GameConfig load_bundled_config() {
  try {
    return decode_config(YAML::Load(reinterpret_cast<const char *>(bundled_game)));
  } catch (const std::exception &error) {
    throw std::runtime_error("Bundled game configuration: " + std::string(error.what()));
  }
}

} // namespace hrm_app
