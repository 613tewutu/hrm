#include "hrm_app/editor.hpp"
#include "hrm_app/level_config.hpp"
#include "hrm_app/robot_animation.hpp"
#include "hrm_app/solutions.hpp"

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
  hrm_app::Editor negative({"inbox", "jumpifnegative 4", "inbox", "outbox"});
  const auto negative_jump = negative.compile().instruction_rows[1];
  check(
    negative.find(negative.find(negative_jump)->target)->landing,
    "negative jumps must import explicit landing cards"
  );
  negative.insert_command(negative.rows().front().id, hrm::Opcode::Inbox);
  check(
    negative.compile().program.instructions[2].text == "jumpifnegative 5",
    "negative jump destinations must rebase with command insertion"
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
  check(linked.move_before(landing, inserted), "linked landing cards must be movable");
  check(
    linked.find(jump)->target == landing &&
      linked.compile().program.instructions[0].text == "jump 2",
    "moving a landing must preserve its ID and update the compiled destination"
  );
  check(linked.move_before(landing, 0), "a landing must be movable to the end");
  check(
    linked.find(jump)->target == landing &&
      linked.compile().program.instructions[0].text == "jump 0",
    "an end landing must remain linked while unresolved"
  );
  check(
    linked.move_before(landing, ids[1]),
    "an end landing must be movable back before a command"
  );
  check(
    linked.compile().program.instructions[0].text == "jump 4",
    "moving an end landing back must restore a numeric target"
  );
  check(
    !linked.move_before(linked.rows().back().id, inserted),
    "the unbound drop prompt cannot move"
  );
  linked.erase(jump);
  check(linked.find(landing) == nullptr, "unreferenced landing cards must be cleaned up");
  hrm_app::Editor shared({"jump 3", "jumpifzero 3", "inbox", "outbox"});
  const auto shared_ids = shared.compile().instruction_rows;
  const auto first_landing = shared.find(shared_ids[0])->target;
  const auto second_landing = shared.find(shared_ids[1])->target;
  check(first_landing != second_landing, "jumps to the same instruction need distinct landings");
  check(
    shared.compile().program.instructions[0].text == "jump 3" &&
      shared.compile().program.instructions[1].text == "jumpifzero 3",
    "distinct landing cards may resolve to the same executable position"
  );
  check(shared.move_before(first_landing, shared_ids[3]), "an owned landing must move");
  check(
    shared.find(shared_ids[0])->target == first_landing &&
      shared.find(shared_ids[1])->target == second_landing &&
      shared.compile().program.instructions[0].text == "jump 4" &&
      shared.compile().program.instructions[1].text == "jumpifzero 3",
    "moving one landing must not move another jump's destination"
  );
  check(
    shared.set_target(shared_ids[0], second_landing) == first_landing &&
      shared.find(shared_ids[1])->target == second_landing,
    "relinking onto another landing must reposition the source's own card"
  );
  check(
    shared.compile().program.instructions[0].text == "jump 3" &&
      shared.compile().program.instructions[1].text == "jumpifzero 3",
    "separate landings at the same location must still compile to the same target"
  );
  const auto round_trip = shared.compile();
  std::vector<std::string> source;
  for (const auto &instruction : round_trip.program.instructions) {
    source.push_back(instruction.text);
  }
  hrm_app::Editor restored(source);
  const auto restored_ids = restored.compile().instruction_rows;
  check(
    restored.find(restored_ids[0])->target != restored.find(restored_ids[1])->target,
    "saved/imported programs must restore one distinct landing per jump"
  );
  shared.erase(first_landing);
  check(
    shared.find(shared_ids[0])->target == 0 &&
      shared.find(shared_ids[1])->target == second_landing && shared.find(second_landing),
    "deleting one landing must not disconnect another jump"
  );
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

/// Verify physical destinations, transfer timing, pause, replay, and nonmoving control flow.
void animation_tests() {
  using hrm_app::RobotEffect;
  using hrm_app::RobotPlace;
  hrm::LevelDefinition level;
  check(hrm::load_builtin_level(2, level), "animation level missing");
  hrm_app::Session session(
    level,
    hrm_app::Editor({"inbox", "copyto 0", "inbox", "add 0", "outbox", "jump 1"})
  );
  hrm_app::RobotAnimation robot;
  auto start = [&] {
    const auto before = session.machine().state();
    const auto row = session.current_row();
    const auto instruction =
      hrm::decode_line(session.machine().program().instructions[before.pc].text);
    const auto result = session.next();
    check(
      robot.start(instruction.instruction, result, before, session.machine().state(), row),
      "step must animate"
    );
  };
  start();
  check(
    robot.frame().effect == RobotEffect::Pickup && robot.frame().from.place == RobotPlace::Home &&
      robot.frame().to.place == RobotPlace::Inbox,
    "inbox must walk from home to the input belt"
  );
  check(
    robot.display_state(session.machine().state()).input_cursor == 0,
    "input must remain visible before pickup"
  );
  robot.advance(0.2, 1, false);
  check(
    robot.frame().walking && robot.frame().travel > 0 && robot.frame().travel < 1,
    "walk must interpolate"
  );
  const auto frozen = robot.frame();
  check(
    !robot.advance(3, 12, true) && robot.frame().phase == frozen.phase,
    "pause must freeze the pose"
  );
  robot.advance(0.51, 1, false);
  check(
    robot.frame().transferred && robot.display_state(session.machine().state()).hand.value == 3,
    "pickup must expose the new hand at the transfer point"
  );
  check(
    robot.frame().row != 0 && robot.active(),
    "the executing card must remain active until settling"
  );
  robot.finish();
  start();
  check(
    robot.frame().from.place == RobotPlace::Inbox && robot.frame().to.place == RobotPlace::Floor &&
      robot.frame().to.floor == 0 && robot.frame().effect == RobotEffect::Store,
    "copyto must approach its addressed floor cell"
  );
  check(
    !robot.display_state(session.machine().state()).floor[0].occupied,
    "floor update must wait for transfer"
  );
  robot.advance(0.18, 4, false);
  check(
    robot.display_state(session.machine().state()).floor[0].value == 3,
    "store must reveal copied floor value"
  );
  robot.finish();
  start();
  robot.finish();
  start();
  check(
    robot.frame().effect == RobotEffect::Add && robot.frame().to.floor == 0,
    "add must show an arithmetic interaction at its floor cell"
  );
  check(
    robot.before().hand.value == 9 && robot.after().hand.value == 12,
    "arithmetic animation must capture both values"
  );
  robot.finish();
  start();
  check(
    robot.frame().effect == RobotEffect::Deliver && robot.frame().to.place == RobotPlace::Outbox,
    "outbox must move to the output belt"
  );
  check(
    robot.display_state(session.machine().state()).output.empty(),
    "output must wait for placement"
  );
  robot.finish();
  start();
  check(
    robot.frame().effect == RobotEffect::Jump && robot.frame().from.place == RobotPlace::Outbox &&
      robot.frame().to.place == RobotPlace::Outbox,
    "jump must turn without teleporting the robot"
  );
  robot.finish();
  robot.seek(level, session.machine().program(), 2);
  check(
    robot.frame().to.place == RobotPlace::Floor && robot.frame().to.floor == 0,
    "rewind must reconstruct the previous physical location"
  );
  robot.seek(level, session.machine().program(), 1);
  check(
    robot.frame().to.place == RobotPlace::Inbox,
    "rewind before a floor command must restore inbox position"
  );
  robot.reset();
  check(
    !robot.active() && robot.frame().to.place == RobotPlace::Home,
    "reset must restore home pose"
  );

  hrm_app::Session fault(level, hrm_app::Editor({"outbox"}));
  const auto before_fault = fault.machine().state();
  const auto failure = fault.next();
  robot.start(
    hrm::decode_line("outbox").instruction,
    failure,
    before_fault,
    fault.machine().state(),
    1
  );
  check(
    robot.frame().effect == RobotEffect::Fault && robot.frame().to.place == RobotPlace::Home,
    "an invalid instruction must shake in place without reaching a destination"
  );
  robot.finish();
  check(
    !robot.start(
      hrm::DecodedInstruction(),
      fault.next(),
      fault.machine().state(),
      fault.machine().state(),
      0
    ),
    "terminal no-op calls must not restart animations"
  );

  level.input.clear();
  level.target.clear();
  hrm_app::Session empty(level, hrm_app::Editor({"inbox"}));
  const auto before_empty = empty.machine().state();
  const auto ended = empty.next();
  robot.reset();
  robot
    .start(hrm::decode_line("inbox").instruction, ended, before_empty, empty.machine().state(), 1);
  check(
    robot.frame().effect == RobotEffect::EmptyInbox && robot.frame().to.place == RobotPlace::Inbox,
    "an empty inbox attempt must reach the belt and end without a pickup"
  );
  robot.advance(1, 12, false);
  check(
    !robot.active() &&
      robot.display_state(empty.machine().state()).verdict == hrm::Verdict::Success,
    "normal empty-inbox termination must settle to the actual verdict"
  );

  level.input = {3};
  hrm_app::Session arithmetic(
    level,
    hrm_app::Editor({"inbox", "copyto 0", "copyfrom 0", "sub 0", "jumpifzero 1"})
  );
  robot.reset();
  const hrm_app::RobotEffect expected[] = {RobotEffect::Pickup,
    RobotEffect::Store,
    RobotEffect::Copy,
    RobotEffect::Subtract,
    RobotEffect::Jump};
  for (const auto effect : expected) {
    const auto before = arithmetic.machine().state();
    const auto command =
      hrm::decode_line(arithmetic.machine().program().instructions[before.pc].text);
    const auto result = arithmetic.next();
    robot.start(command.instruction, result, before, arithmetic.machine().state(), 1);
    check(
      robot.frame().effect == effect,
      "copy, subtraction, and zero-branch effects must match their commands"
    );
    robot.finish();
  }
  hrm_app::Session nonzero(level, hrm_app::Editor({"inbox", "jumpifzero 1"}));
  nonzero.next();
  const auto before_branch = nonzero.machine().state();
  const auto branch = nonzero.next();
  robot.start(
    hrm::decode_line("jumpifzero 1").instruction,
    branch,
    before_branch,
    nonzero.machine().state(),
    1
  );
  check(
    robot.frame().effect == RobotEffect::Continue,
    "nonzero conditional jumps must use a continue gesture"
  );
  level.allowed_opcodes.set(static_cast<std::size_t>(hrm::Opcode::JumpIfNegative));
  for (const auto value : {-1, 0, 1}) {
    level.input = {value};
    hrm_app::Session negative(level, hrm_app::Editor({"inbox", "jumpifnegative 1"}));
    negative.next();
    const auto before = negative.machine().state();
    const auto result = negative.next();
    robot.start(
      hrm::decode_line("jumpifnegative 1").instruction,
      result,
      before,
      negative.machine().state(),
      1
    );
    check(
      robot.frame().effect == (value < 0 ? RobotEffect::Jump : RobotEffect::Continue),
      "negative jump animation must follow the sign of the held value"
    );
  }
}

/// Compare YAML levels with OJ data and reject malformed game configurations.
void config_tests() {
  const auto game = hrm_app::load_config("assets/game.yaml");
  check(game.levels.size() == 5, "YAML levels missing");
  for (std::size_t i = 0; i < 3; ++i) {
    hrm::LevelDefinition builtin;
    hrm::load_builtin_level(static_cast<int>(i + 1), builtin);
    const auto &yaml = game.levels[i].definition;
    check(
      yaml.input == builtin.input && yaml.target == builtin.target &&
        yaml.floor_size == builtin.floor_size && yaml.allowed_opcodes == builtin.allowed_opcodes,
      "YAML and OJ levels disagree"
    );
  }
  const auto &triple_level = game.levels[3].definition;
  check(
    triple_level.id == 4 && triple_level.floor_size == 1 &&
      !triple_level.allowed_opcodes.test(static_cast<std::size_t>(hrm::Opcode::JumpIfNegative)),
    "level 4 must triple inputs using the course instruction set"
  );
  hrm_app::Session triple(
    triple_level,
    hrm_app::Editor({"inbox", "copyto 0", "add 0", "add 0", "outbox", "jump 1"})
  );
  for (int i = 0; i < 100 && triple.machine().state().status == hrm::ExecutionStatus::Running;
    ++i) {
    triple.next();
  }
  check(
    triple.machine().state().verdict == hrm::Verdict::Success,
    "tripling level must be solvable"
  );
  const auto &sign_level = game.levels[4].definition;
  check(
    sign_level.id == 5 &&
      sign_level.allowed_opcodes.test(static_cast<std::size_t>(hrm::Opcode::JumpIfNegative)),
    "level 5 must introduce negative conditional jumps"
  );
  check(sign_level.floor_size == 6, "sign comparison needs six floor slots");
  hrm_app::Session signs(
    sign_level,
    hrm_app::Editor(
      {"inbox",
        "copyto 0",
        "copyto 1",
        "copyfrom 0",
        "sub 5",
        "copyto 0",
        "jumpifzero 13",
        "copyfrom 1",
        "add 5",
        "copyto 1",
        "jumpifzero 15",
        "jump 4",
        "copyfrom 4",
        "jump 16",
        "copyfrom 5",
        "copyto 2",
        "inbox",
        "copyto 0",
        "copyto 1",
        "copyfrom 0",
        "sub 5",
        "copyto 0",
        "jumpifzero 29",
        "copyfrom 1",
        "add 5",
        "copyto 1",
        "jumpifzero 31",
        "jump 20",
        "copyfrom 4",
        "jump 32",
        "copyfrom 5",
        "sub 2",
        "jumpifzero 36",
        "copyfrom 5",
        "jump 37",
        "copyfrom 4",
        "outbox",
        "jump 1"}
    )
  );
  check(
    signs.machine().state().floor[4].occupied && signs.machine().state().floor[4].value == 0 &&
      signs.machine().state().floor[5].occupied && signs.machine().state().floor[5].value == 1,
    "initial constants must be occupied, including zero"
  );
  for (std::size_t i = 0; i < 4; ++i) {
    check(!signs.machine().state().floor[i].occupied, "working floor cells must start empty");
  }
  for (int i = 0; i < 5000 && signs.machine().state().status == hrm::ExecutionStatus::Running;
    ++i) {
    signs.next();
  }
  check(signs.machine().state().verdict == hrm::Verdict::Success, "sign level must be solvable");
  hrm_app::Session negative_signs(
    sign_level,
    hrm_app::Editor(
      {"inbox",
        "jumpifnegative 6",
        "copyfrom 4",
        "copyto 0",
        "jump 9",
        "copyfrom 5",
        "copyto 0",
        "jump 9",
        "inbox",
        "jumpifnegative 14",
        "copyfrom 4",
        "sub 0",
        "jump 16",
        "copyfrom 5",
        "sub 0",
        "jumpifzero 19",
        "copyfrom 5",
        "jump 20",
        "copyfrom 4",
        "outbox",
        "jump 1"}
    )
  );
  for (int i = 0;
    i < 200 && negative_signs.machine().state().status == hrm::ExecutionStatus::Running;
    ++i) {
    negative_signs.next();
  }
  check(
    negative_signs.machine().state().verdict == hrm::Verdict::Success,
    "negative branches must solve every sign combination"
  );
  signs.reset();
  signs.edit() = hrm_app::Editor({"copyfrom 5", "copyto 4"});
  signs.reset();
  signs.next();
  signs.next();
  check(signs.machine().state().floor[4].value == 1, "preset slots must remain writable");
  signs.previous();
  check(signs.machine().state().floor[4].value == 0, "rewind must replay from preset cells");
  signs.reset();
  check(
    signs.machine().state().floor[4].value == 0 && signs.machine().state().floor[5].value == 1,
    "reset must restore the original constants"
  );
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
  for (const auto &initial : {"{6: 0}", "{-1: 0}", "{4: 1.5}", "{4: 0, 04: 1}", "[0, 1]"}) {
    std::ofstream(
      test_path
    ) << "version: 1\ntitle: test\nlevels:\n - id: 1\n   title: test\n   description: test\n"
         "   input: [1]\n   target: [1]\n   floor_size: 6\n   instructions: [inbox]\n"
         "   initial_floor: "
      << initial << '\n';
    bool rejected = false;
    try {
      hrm_app::load_config(test_path);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "invalid preset floor data must be rejected");
  }
  std::filesystem::remove(test_path);
}

/// Verify successful-only persistence, jump round trips, stale levels, and damaged saves.
void solution_tests() {
  const auto directory = std::filesystem::path("build/tests/solutions");
  std::filesystem::remove_all(directory);
  hrm_app::SolutionStore store(directory);
  hrm::LevelDefinition level;
  hrm::load_builtin_level(1, level);
  level.allowed_opcodes.set(static_cast<std::size_t>(hrm::Opcode::Jump));
  level.floor_size = 1;
  level.initial_floor = {hrm::Cell(0)};
  check(!store.load(level), "a missing save must not mark a level completed");
  hrm_app::Session session(level, hrm_app::Editor({"inbox", "outbox", "jump 1"}));
  bool rejected = false;
  try {
    store.save(session.machine());
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected && !std::filesystem::exists(directory), "unsolved drafts must not be saved");
  for (int i = 0; i < 10 && session.machine().state().status == hrm::ExecutionStatus::Running;
    ++i) {
    session.next();
  }
  store.save(session.machine());
  const auto loaded = store.load(level);
  check(
    loaded && *loaded == std::vector<std::string>{"inbox", "outbox", "jump 1"},
    "saved instructions and numeric jump destinations must survive restart"
  );
  hrm_app::Session restored(level, hrm_app::Editor(*loaded));
  check(restored.machine().state().attempted == 0, "loaded solutions must start at step zero");
  auto changed = level;
  changed.input.push_back(42);
  check(!store.load(changed), "changed level data must invalidate old completion");
  changed = level;
  changed.initial_floor[0] = hrm::Cell(1);
  check(!store.load(changed), "changed preset constants must invalidate old completion");
  const auto path = directory / "level-1.yaml";
  std::ifstream input(path);
  std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  input.close();
  body.replace(body.find("outbox"), 6, "jump 1");
  std::ofstream(path) << body;
  rejected = false;
  try {
    store.load(level);
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "a tampered nonterminating solution must fail bounded replay");
  store.save(session.machine());
  check(store.load(level).has_value(), "a new successful run must replace damaged progress");
  std::ofstream(path) << "version: [broken";
  rejected = false;
  try {
    store.load(level);
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected, "corrupt YAML must report a recoverable load failure");
  std::filesystem::remove_all(directory);
}

} // namespace

/// Run application regressions and report the first failing invariant.
int main() {
  try {
    editor_tests();
    drop_tests();
    playback_tests();
    animation_tests();
    config_tests();
    solution_tests();
    std::cout << "Editor, playback, YAML, robot animation, and saved solution tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
