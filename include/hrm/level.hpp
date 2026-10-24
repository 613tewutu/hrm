#pragma once

#include "hrm/types.hpp"

#include <bitset>
#include <string>
#include <vector>

namespace hrm {

struct LevelDefinition {
  int id = 0;
  std::string description;
  std::vector<Value> input;
  std::vector<Value> target;
  std::size_t floor_size = 0;
  std::vector<Cell> initial_floor; // Optional preset cells; omitted positions start empty.
  std::bitset<opcode_count> allowed_opcodes;
};

/// Load one of the three built-in assignment levels.
///
/// @param id One-based built-in level ID.
/// @param destination Receives the complete level definition on success.
/// @return True when the ID exists; false leaves destination unchanged.
bool load_builtin_level(int id, LevelDefinition &destination);

} // namespace hrm
