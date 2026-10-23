#include "hrm/level.hpp"

#include <utility>

namespace hrm {

bool load_builtin_level(int id, LevelDefinition &destination) {
  LevelDefinition level;
  level.id = id;
  if (id == 1) {
    level.description = "Send every input value to the output.";
    level.input = {1, 2};
    level.target = {1, 2};
    level.allowed_opcodes.set(static_cast<std::size_t>(Opcode::Inbox));
    level.allowed_opcodes.set(static_cast<std::size_t>(Opcode::Outbox));
  } else if (id == 2) {
    level.description = "For each pair, output first minus second, then second minus first.";
    level.input = {3, 9, 5, 1, -2, -2, 9, -9};
    level.target = {-6, 6, 4, -4, 0, 0, 18, -18};
    level.floor_size = 3;
    level.allowed_opcodes.set();
  } else if (id == 3) {
    level.description = "For each equal input pair, output one of its values.";
    level.input = {6, 2, 7, 7, -9, 3, -3, -3};
    level.target = {7, -3};
    level.floor_size = 3;
    level.allowed_opcodes.set();
  } else {
    return false;
  }
  destination = std::move(level);
  return true;
}

} // namespace hrm
