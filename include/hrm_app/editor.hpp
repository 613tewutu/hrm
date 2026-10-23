#pragma once

#include "hrm/core.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hrm_app {

using RowId = std::uint64_t;

struct CommandRow {
  RowId id = 0;
  bool landing = true;
  hrm::Opcode opcode = hrm::Opcode::Inbox;
  std::string operand = "0";
  RowId target = 0;
};

const char *opcode_name(hrm::Opcode opcode);
bool is_jump(hrm::Opcode opcode);
bool uses_floor(hrm::Opcode opcode);

struct CompiledProgram {
  hrm::Program program;
  std::vector<RowId> instruction_rows;
};

class Editor {
public:
  explicit Editor(const std::vector<std::string> &source = {});
  const std::vector<CommandRow> &rows() const;
  const CommandRow *find(RowId id) const;
  RowId insert_command(RowId after, hrm::Opcode opcode, std::string operand = "0");
  // A zero destination appends; other destinations insert before that stable row ID.
  RowId insert_before(RowId before, hrm::Opcode opcode, std::string operand = "0");
  bool move_before(RowId source, RowId before);
  RowId insert_landing(RowId before);
  void erase(RowId id);
  void set_operand(RowId id, std::string operand);
  RowId set_target(RowId jump, RowId destination);
  CompiledProgram compile() const;

private:
  RowId insert_at(std::size_t index, hrm::Opcode opcode, std::string operand);
  void tidy_landings();
  RowId next_id_ = 1;
  std::vector<CommandRow> rows_;
};

class Session {
public:
  Session(hrm::LevelDefinition level, Editor editor);
  const hrm::Machine &machine() const;
  const Editor &editor() const;
  Editor &edit();
  void reset();
  void next();
  void previous();
  RowId current_row() const;

private:
  hrm::LevelDefinition level_;
  Editor editor_;
  CompiledProgram compiled_;
  std::unique_ptr<hrm::Machine> machine_;
};

} // namespace hrm_app
