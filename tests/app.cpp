#include "hrm_app/editor.hpp"
#include "hrm_app/level_config.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

/// Fail a regression immediately when its stated invariant does not hold.
void check(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

/// Verify stable landing IDs and ordinal rebasing across command and link edits.
void editor_tests() {
  hrm_app::Editor editor({"inbox", "outbox", "jump 1"});
  auto compiled = editor.compile();
  check(compiled.program.instructions.size() == 3, "landing cards must not become instructions");
  check(compiled.program.instructions[2].text == "jump 1", "imported jump target changed");
  const auto jump = compiled.instruction_rows[2];
  const auto landing = editor.find(jump)->target;
  check(editor.find(landing)->landing, "jump must point to an empty landing card");
  const auto first = compiled.instruction_rows[0];
  editor.insert_command(landing, hrm::Opcode::CopyTo, "2");
  compiled = editor.compile();
  check(
    compiled.program.instructions[3].text == "jump 1",
    "inserting at a landing card must keep it targeted"
  );
  check(
    compiled.program.instructions[0].text == "copyto 2",
    "command must be inserted below landing card"
  );
  editor.erase(first);
  compiled = editor.compile();
  check(
    compiled.program.instructions[2].text == "jump 1",
    "deleting a command must preserve its preceding landing"
  );
  editor.erase(landing);
  check(
    editor.compile().program.instructions.back().text == "jump 0",
    "deleted landing must leave a detectable unbound jump"
  );
  const auto terminal = editor.rows().back().id;
  editor.set_target(jump, terminal);
  check(
    editor.compile().program.instructions.back().text == "jump 0",
    "trailing empty target must not silently jump past the end"
  );
  editor.insert_command(terminal, hrm::Opcode::Inbox);
  check(
    editor.compile().program.instructions[2].text == "jump 4",
    "adding below an empty target must resolve the link"
  );

  hrm_app::Editor forward({"jump 3", "inbox", "outbox", "jumpifzero 2"});
  check(
    forward.compile().program.instructions[0].text == "jump 3",
    "forward target must retain its ordinal"
  );
  check(
    forward.compile().program.instructions[3].text == "jumpifzero 2",
    "multiple anchors must not alter source numbering"
  );
  const auto second = forward.compile().instruction_rows[1];
  forward.insert_command(forward.rows().front().id, hrm::Opcode::Inbox);
  check(
    forward.compile().program.instructions[0].text == "jump 4",
    "insertion before a target must rebase numeric jumps"
  );
  forward.set_target(forward.compile().instruction_rows[0], second);
  check(
    forward.find(forward.find(forward.compile().instruction_rows[0])->target)->landing,
    "retargeting to a command must create or reuse a landing card"
  );
}

/// Verify insertion, reordering, stale destinations, and orphan-landing cleanup.
void drop_tests() {
  hrm_app::Editor editor;
  const auto inbox = editor.insert_before(0, hrm::Opcode::Inbox);
  const auto outbox = editor.insert_before(0, hrm::Opcode::Outbox);
  const auto copy = editor.insert_before(outbox, hrm::Opcode::CopyTo, "2");
  check(
    editor.rows().size() == 4 && !editor.rows().front().landing,
    "normal insertion must not leave redundant empty cards"
  );
  auto compiled = editor.compile();
  check(
    compiled.program.instructions[1].text == "copyto 2",
    "middle drop must insert before its destination"
  );
  check(editor.move_before(outbox, inbox), "existing cards must move to the start");
  check(
    editor.compile().instruction_rows == std::vector<hrm_app::RowId>{outbox, inbox, copy},
    "moving must preserve row IDs and order"
  );
  check(editor.move_before(outbox, 0), "existing cards must move to the end");
  check(
    editor.compile().instruction_rows == std::vector<hrm_app::RowId>{inbox, copy, outbox},
    "append move must preserve all commands"
  );
  check(
    !editor.move_before(outbox, 0) && !editor.move_before(inbox, inbox),
    "drops in the same position must be no-ops"
  );
  check(editor.find(copy)->operand == "2", "reordering must preserve operands");
  check(
    editor.insert_before(999999, hrm::Opcode::Inbox) == 0 && !editor.move_before(inbox, 999999),
    "stale drop destinations must not modify the program"
  );
  check(editor.rows().size() == 4, "stale destinations changed the editor");

  hrm_app::Editor linked({"inbox", "outbox", "jump 1"});
  const auto ids = linked.compile().instruction_rows;
  const auto jump = ids[2];
  const auto landing = linked.find(jump)->target;
  const auto inserted = linked.insert_before(landing, hrm::Opcode::CopyFrom, "0");
  check(
    linked.compile().program.instructions.back().text == "jump 2",
    "drop before a landing must rebase the destination"
  );
  check(linked.move_before(jump, inserted), "a linked jump must be reorderable");
  check(
    linked.find(jump)->target == landing &&
      linked.compile().program.instructions[0].text == "jump 3",
    "moving a jump must preserve its landing and recompute the ordinal"
  );
  check(!linked.move_before(landing, inserted), "landing cards must not become draggable commands");
  linked.erase(jump);
  check(linked.find(landing) == nullptr, "unreferenced landing cards must be cleaned up");
}

/// Verify forward execution, reset, and replay before ordinary and terminal steps.
void playback_tests() {
  hrm::LevelDefinition level;
  check(hrm::load_builtin_level(1, level), "built-in level missing");
  hrm_app::Session session(level, hrm_app::Editor({"inbox", "outbox", "inbox", "outbox"}));
  session.previous();
  check(session.machine().state().attempted == 0, "previous at start must be a no-op");
  session.next();
  session.next();
  check(
    session.machine().state().output == std::vector<hrm::Value>{1},
    "next must execute kernel instructions"
  );
  session.previous();
  check(
    session.machine().state().hand.occupied && session.machine().state().hand.value == 1,
    "previous must restore the hand"
  );
  check(session.machine().state().output.empty(), "previous must remove output");
  session.next();
  session.next();
  session.next();
  check(
    session.machine().state().verdict == hrm::Verdict::Success,
    "completed program must succeed"
  );
  session.previous();
  check(
    session.machine().state().status == hrm::ExecutionStatus::Running,
    "previous must leave terminal state"
  );
  check(
    session.machine().state().hand.value == 2,
    "previous after success must restore final hand"
  );
  session.reset();
  check(
    session.machine().state().output.empty() && session.machine().state().input_cursor == 0,
    "reset must restore conveyors"
  );

  hrm_app::Session faulty(level, hrm_app::Editor({"outbox"}));
  faulty.next();
  check(faulty.machine().state().verdict == hrm::Verdict::Error, "fault must propagate");
  faulty.previous();
  check(
    faulty.machine().state().status == hrm::ExecutionStatus::Running,
    "previous must undo a fault"
  );
}

/// Compare YAML levels with OJ data and reject malformed game configurations.
void config_tests() {
  const auto game = hrm_app::load_config("assets/game.yaml");
  check(game.levels.size() == 3, "YAML levels missing");
  for (std::size_t i = 0; i < game.levels.size(); ++i) {
    hrm::LevelDefinition builtin;
    hrm::load_builtin_level(static_cast<int>(i + 1), builtin);
    const auto &yaml = game.levels[i].definition;
    check(
      yaml.input == builtin.input && yaml.target == builtin.target &&
        yaml.floor_size == builtin.floor_size && yaml.allowed_opcodes == builtin.allowed_opcodes,
      "YAML and OJ levels disagree"
    );
  }
  const auto test_path = std::filesystem::path("build/tests/invalid-game.yaml");
  std::filesystem::create_directories(test_path.parent_path());
  for (const auto &body : {std::string("version: 2\ntitle: invalid\nlevels: []\n"),
         std::string("version: 1\ntitle: invalid\nlevels: []\n"),
         std::string(
           "version: 1\ntitle: invalid\nlevels:\n - id: 1\n   title: bad\n   description: bad\n   "
           "input: [1.5]\n   target: []\n   floor_size: 0\n   instructions: [inbox]\n"
         )}) {
    std::ofstream(test_path) << body;
    bool rejected = false;
    try {
      hrm_app::load_config(test_path);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "invalid YAML configuration must be rejected");
  }
  std::filesystem::remove(test_path);
}

} // namespace

/// Run application regressions and report the first failing invariant.
int main() {
  try {
    editor_tests();
    drop_tests();
    playback_tests();
    config_tests();
    std::cout << "Editor, playback, and YAML tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
