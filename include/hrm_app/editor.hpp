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

/// Return the lowercase spelling of a valid kernel opcode.
const char *opcode_name(hrm::Opcode opcode);
/// Return whether the opcode uses an instruction destination.
bool is_jump(hrm::Opcode opcode);
/// Return whether the opcode uses a floor-cell operand.
bool uses_floor(hrm::Opcode opcode);

struct CompiledProgram {
  hrm::Program program;
  std::vector<RowId> instruction_rows;
};

class Editor {
public:
  /// Import a program into stable command rows and explicit jump landing cards.
  ///
  /// @param source Syntactically valid instruction lines; an empty sequence creates a drop prompt.
  /// @throws std::invalid_argument If an imported instruction cannot be decoded.
  explicit Editor(const std::vector<std::string> &source = {});
  /// Return the current read-only command and landing rows.
  const std::vector<CommandRow> &rows() const;
  /// Return the row with this stable ID, or nullptr when it is absent.
  const CommandRow *find(RowId id) const;
  /// Insert a command after a selected row and remove unused interior landing cards.
  ///
  /// @param after Selected row ID; an absent ID inserts before the terminal drop prompt.
  /// @param opcode Instruction to insert.
  /// @param operand Text retained for floor instructions, including invalid text for deferred
  /// errors.
  /// @return The new stable command ID. Inserting below a landing changes its destination.
  RowId insert_command(RowId after, hrm::Opcode opcode, std::string operand = "0");
  /// Insert a dropped command before a stable destination or append at the end.
  ///
  /// @param before Destination row ID, or zero to append below any terminal landing.
  /// @param opcode Instruction to insert.
  /// @param operand Floor operand text preserved for the kernel to validate when reached.
  /// @return The new stable command ID, or zero for a stale destination without changing the
  /// editor.
  RowId insert_before(RowId before, hrm::Opcode opcode, std::string operand = "0");
  /// Reorder a command while preserving its ID, operand, and outgoing jump link.
  ///
  /// @param source Existing command ID; landing cards cannot be moved as commands.
  /// @param before Destination row ID, or zero to append.
  /// @return True when the order changes; false for invalid destinations or no-op drops.
  bool move_before(RowId source, RowId before);
  /// Ensure an empty landing immediately precedes a destination.
  ///
  /// @param before Destination row ID; an absent ID requests an end landing.
  /// @return An existing adjacent landing ID or a new empty landing ID.
  RowId insert_landing(RowId before);
  /// Remove a row and disconnect references to a deleted landing.
  ///
  /// @param id Row to remove; missing IDs and the terminal drop prompt are unchanged.
  void erase(RowId id);
  /// Set a floor command's operand text without eagerly validating it.
  void set_operand(RowId id, std::string operand);
  /// Link a jump to an empty landing card before the selected destination.
  ///
  /// @param jump Source unconditional or conditional jump command ID.
  /// @param destination Existing command or landing row ID.
  /// @return The stable landing ID, or zero when either endpoint is invalid.
  RowId set_target(RowId jump, RowId destination);
  /// Lower editor rows to kernel instructions and a source-row mapping.
  ///
  /// Landing cards do not consume instruction IDs. Jumps resolve to the first
  /// executable row below their landing; unbound/end landings lower to target zero.
  ///
  /// @return Original instruction text and row IDs in executable order.
  CompiledProgram compile() const;

private:
  /// Insert an owned command row and restore the landing/drop-prompt invariants.
  ///
  /// @param index Valid insertion offset within the row vector, including its end.
  /// @param opcode Instruction to insert.
  /// @param operand Floor operand text to store unchanged.
  /// @return The newly allocated stable command ID.
  RowId insert_at(std::size_t index, hrm::Opcode opcode, std::string operand);
  /// Keep one terminal drop prompt and remove unreferenced interior landing cards.
  void tidy_landings();
  RowId next_id_ = 1;
  std::vector<CommandRow> rows_;
};

class Session {
public:
  /// Own an editor and level and compile their initial machine session.
  ///
  /// @param level Immutable execution rules and input/target data for this session.
  /// @param editor Player command rows to own and compile.
  Session(hrm::LevelDefinition level, Editor editor);
  /// Return the read-only execution machine.
  const hrm::Machine &machine() const;
  /// Return the read-only editor.
  const Editor &editor() const;
  /// Return the editor for mutation; call reset after changing its program.
  Editor &edit();
  /// Recompile the editor and restart execution from its initial state.
  void reset();
  /// Attempt one instruction using the kernel's terminal-call semantics.
  ///
  /// @return Actual kernel transition for presentation adapters to animate.
  hrm::StepResult next();
  /// Restore the state before the most recent attempted instruction by deterministic replay.
  ///
  /// A fresh session is unchanged. Replaying costs time proportional to prior attempts
  /// and restores state before an error or normal termination as well as ordinary steps.
  void previous();
  /// Return the current instruction's stable row ID, or zero for an empty program.
  RowId current_row() const;

private:
  hrm::LevelDefinition level_;
  Editor editor_;
  CompiledProgram compiled_;
  std::unique_ptr<hrm::Machine> machine_;
};

} // namespace hrm_app
