#include "hrm_app/editor.hpp"
#include "hrm_app/level_config.hpp"
#include "hrm_app/robot_animation.hpp"
#include "hrm_app/solutions.hpp"
#include "hrm_audio/audio.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace ftxui;
using hrm_app::RowId;

// Inherit neutral colors; resolve accents through the terminal's own ANSI palette.
const Color ink = Color::Default;
const Color muted = Color::Default;
const Color outline = Color::Default;
const Color background = Color::Default;
const Color surface = Color::Default;
const Color teal = Color::Cyan;
const Color amber = Color::Yellow;
const Color blue = Color::Blue;
const Color red = Color::Red;

/// Draw a sampled half-ellipse with horizontal end tangents and a compact pixel arrowhead.
/// @param drawing Right-hand route canvas in subcell pixels.
/// @param from Source height in canvas pixels.
/// @param to Destination height in canvas pixels.
/// @param lane Maximum horizontal excursion of the curve.
/// @param tint Terminal palette color for both curve and arrowhead.
void curved_arrow(Canvas &drawing, int from, int to, int lane, Color tint) {
  const int samples = std::max(48, std::abs(to - from) * 3 + lane * 6);
  int previous_x = 0;
  int previous_y = from;
  for (int i = 1; i <= samples; ++i) {
    const double t = static_cast<double>(i) / samples;
    const double angle = t * 3.141592653589793;
    const int x = static_cast<int>(std::lround(lane * std::sin(angle)));
    const int y = static_cast<int>(std::lround(from + (to - from) * (1.0 - std::cos(angle)) / 2.0));
    drawing.DrawPointLine(previous_x, previous_y, x, y, tint);
    drawing.DrawPointLine(previous_x + 1, previous_y, x + 1, y, tint);
    previous_x = x;
    previous_y = y;
  }
  for (int x = 0; x <= 3; ++x) {
    drawing.DrawPointLine(x, to - x, x, to + x, tint);
  }
}

/// Wrap content in a rounded card using the terminal's palette.
///
/// @param content Already-styled card contents.
/// @param edge Border accent, inherited by otherwise unstyled children.
/// @return A card element using the terminal-default background.
Element card(Element content, Color edge = outline) {
  return content | borderRounded | color(edge) | bgcolor(surface);
}

/// Create a focusable rounded action button with a semantic click handler.
///
/// @param label Static text or a live label reference owned by the application.
/// @param handler Action to invoke for mouse or keyboard activation.
/// @return A button with the same border and focus treatment as command cards.
Component action(ConstStringRef label, std::function<void()> handler) {
  auto options = ButtonOption::Simple();
  options.transform = [](const EntryState &state) {
    auto element = card(
      text(" " + state.label + " ") | color(state.focused ? teal : ink),
      state.focused ? teal : outline
    );
    return state.focused ? element | focus : element;
  };
  return Button(label, std::move(handler), options);
}

/// Format a value sequence for the level's target description.
///
/// @param values Ordered target values.
/// @return Comma-separated numbers, or an explicit empty-sequence label.
std::string sequence(const std::vector<hrm::Value> &values) {
  std::string result;
  for (const auto value : values) {
    if (!result.empty()) {
      result += ", ";
    }
    result += std::to_string(value);
  }
  return result.empty() ? "(empty)" : result;
}

/// Return a player-facing explanation for a kernel error code.
std::string error_text(hrm::ErrorCode code) {
  switch (code) {
  case hrm::ErrorCode::EmptyHand:
    return "The robot's hand is empty.";
  case hrm::ErrorCode::EmptyFloorCell:
    return "That floor slot is empty.";
  case hrm::ErrorCode::InvalidFloorIndex:
    return "That floor slot does not exist.";
  case hrm::ErrorCode::InvalidJumpTarget:
    return "Link the jump to a landing card with a command below it.";
  case hrm::ErrorCode::ArithmeticOverflow:
    return "The result exceeds the signed 64-bit range.";
  case hrm::ErrorCode::OpcodeNotAllowed:
    return "This instruction is unavailable in this level.";
  case hrm::ErrorCode::WrongOperandCount:
    return "Wrong number of operands.";
  case hrm::ErrorCode::InvalidInteger:
    return "The operand must be an integer.";
  case hrm::ErrorCode::OperandOutOfRange:
    return "The operand exceeds the signed 64-bit range.";
  default:
    return "Unknown instruction.";
  }
}

/// Route focus, selection, and keyboard events through the scrollable command stack.
class CommandStack : public ComponentBase {
public:
  std::function<Element(bool, std::vector<Box> &)> draw;
  std::function<void(std::size_t)> pick;
  std::function<void(int)> move;
  std::function<void()> confirm;
  std::function<void()> remove;

  /// Return the most recently laid-out card bounds for hit testing.
  const std::vector<Box> &boxes() const {
    return boxes_;
  }
  /// Return the visible stack bounds for drop and scroll hit testing.
  const Box &viewport() const {
    return viewport_;
  }

  /// Keep the command stack reachable through keyboard focus traversal.
  bool Focusable() const override {
    return true;
  }
  /// Render the stack and retain its visible bounds for subsequent events.
  Element OnRender() override {
    return draw(Focused(), boxes_) | reflect(viewport_);
  }
  /// Dispatch selection, scrolling, confirmation, and deletion events.
  ///
  /// @param event Mouse or keyboard event from the component hierarchy.
  /// @return True when the stack consumes the event; false lets sibling controls handle it.
  bool OnEvent(Event event) override {
    if (event.is_mouse()) {
      const auto &mouse = event.mouse();
      if (!viewport_.Contain(mouse.x, mouse.y)) {
        return false;
      }
      if (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown) {
        TakeFocus();
        move(mouse.button == Mouse::WheelUp ? -1 : 1);
        return true;
      }
      if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
        for (std::size_t i = 0; i < boxes_.size(); ++i) {
          if (boxes_[i].Contain(mouse.x, mouse.y)) {
            TakeFocus();
            pick(i);
            return true;
          }
        }
      }
      return false;
    }
    if (!Focused()) {
      return false;
    }
    if (event == Event::ArrowUp || event == Event::ArrowDown) {
      move(event == Event::ArrowUp ? -1 : 1);
      return true;
    }
    if (event == Event::Return) {
      confirm();
      return true;
    }
    if (event == Event::Delete || event == Event::Backspace) {
      remove();
      return true;
    }
    return false;
  }

private:
  std::vector<Box> boxes_;
  Box viewport_;
};

/// Own per-level editors and coordinate the terminal workroom on one UI thread.
class Application {
public:
  /// Create empty editor sessions and assemble their shared terminal controls.
  ///
  /// @param config Validated level definitions and display metadata to own.
  /// @param initial_level Level ID selected at startup, validated by the caller.
  explicit Application(hrm_app::GameConfig config, int initial_level, bool audio_enabled = true)
    : config_(std::move(config)),
      audio_(audio_enabled) {
    for (std::size_t i = 0; i < config_.levels.size(); ++i) {
      const auto &level = config_.levels[i];
      level_names_.push_back(std::to_string(level.definition.id) + " / " + level.title);
      sessions_.push_back(std::make_unique<hrm_app::Session>(level.definition, hrm_app::Editor()));
      try {
        solutions_.push_back(solution_store_.load(level.definition));
      } catch (const std::exception &error) {
        solutions_.push_back(std::nullopt);
        picker_message_ = error.what();
      }
      if (level.definition.id == initial_level) {
        level_index_ = static_cast<int>(i);
      }
    }
    shown_level_ = level_index_;
    picker_index_ = level_index_;
    refresh_levels();
    select(session().editor().rows().front().id);
    build_components();
  }

  /// Return the root interactive component.
  Component component() const {
    return root_;
  }
  /// Set explicit terminal dimensions for a noninteractive preview.
  void set_preview_size(int width, int height) {
    screen_index_ = 0;
    level_entered_ = true;
    preview_width_ = width;
    preview_height_ = height;
  }
  std::function<void()> quit;

  /// Advance continuous playback or drag auto-scrolling when its timer is due.
  ///
  /// @param now Monotonic UI-loop time used for playback and scroll deadlines.
  void tick(std::chrono::steady_clock::time_point now) {
    audio_.update();
    const double elapsed = last_tick_.time_since_epoch().count() == 0
      ? 0.0
      : std::chrono::duration<double>(now - last_tick_).count();
    last_tick_ = now;
    if (screen_index_ == 0) {
      hint_time_ += elapsed;
      const auto offset = static_cast<std::size_t>(std::max(0.0, hint_time_ - 2.0) / 0.14);
      if (offset != hint_offset_) {
        hint_offset_ = offset;
        redraw_ = true;
      }
    }
    if (screen_index_ == 1) {
      return;
    }
    if (drag_) {
      const auto target = drop_target(drag_->x, drag_->y);
      if (target != drag_->before) {
        drag_->before = target;
        redraw_ = true;
      }
      const auto &viewport = stack_->viewport();
      if (drag_->moved && viewport.Contain(drag_->x, drag_->y) && now >= next_scroll_) {
        if (drag_->y <= viewport.y_min + 1) {
          move(-1);
          redraw_ = true;
        } else if (drag_->y >= viewport.y_max - 1) {
          move(1);
          redraw_ = true;
        }
        next_scroll_ = now + std::chrono::milliseconds(100);
      }
      return;
    }
    if (linking_ && link_pointer_ && stack_->viewport().Contain(mouse_x_, mouse_y_) &&
      now >= next_scroll_) {
      const auto &viewport = stack_->viewport();
      if (mouse_y_ <= viewport.y_min + 1) {
        move(-1);
      } else if (mouse_y_ >= viewport.y_max - 1) {
        move(1);
      }
      next_scroll_ = now + std::chrono::milliseconds(100);
    }
    if (!animation_paused_) {
      mascot_time_ = std::fmod(mascot_time_ + elapsed, 4.0);
      if (!robot_.active() && now >= next_frame_) {
        redraw_ = true;
        next_frame_ = now + std::chrono::milliseconds(mascot_interacted_ ? 100 : 33);
      }
    }
    const bool was_active = robot_.active();
    const bool was_transferred = robot_.frame().transferred;
    if (robot_.advance(elapsed, speed_, animation_paused_) &&
      (now >= next_frame_ || !robot_.active())) {
      redraw_ = true;
      next_frame_ = now + std::chrono::milliseconds(16);
    }
    if (was_active && !was_transferred && robot_.frame().transferred) {
      transfer_sound();
    }
    if (was_active && !robot_.active() &&
      session().machine().state().status != hrm::ExecutionStatus::Running) {
      playing_ = false;
      settled_sound();
    }
    if (playing_ && !robot_.active()) {
      if (!start_step()) {
        playing_ = false;
      }
      redraw_ = true;
    }
  }
  /// Consume the pending timed-redraw flag.
  bool needs_redraw() {
    return std::exchange(redraw_, false);
  }

private:
  struct DragState {
    hrm::Opcode opcode;
    RowId source = 0;
    bool landing = false;
    std::string operand;
    std::string label;
    int card_width = 0, grab_x = 0, grab_y = 0;
    int start_x = 0, start_y = 0, x = 0, y = 0;
    bool moved = false;
    std::optional<RowId> before;
  };

  /// Return the currently selected level's session.
  hrm_app::Session &session() {
    return *sessions_[static_cast<std::size_t>(level_index_)];
  }

  /// Return physical state at the animation's transfer point rather than exposing an early update.
  const hrm::MachineState &view_state() {
    return robot_.display_state(session().machine().state());
  }

  /// Play one instruction cue when its visual data-transfer phase is crossed.
  void transfer_sound() {
    using hrm_app::RobotEffect;
    using hrm_audio::Cue;
    Cue cue = Cue::Count;
    switch (robot_.frame().effect) {
    case RobotEffect::Pickup:
      cue = Cue::Pickup;
      break;
    case RobotEffect::Deliver:
      cue = Cue::Deliver;
      break;
    case RobotEffect::Store:
      cue = Cue::Store;
      break;
    case RobotEffect::Copy:
      cue = Cue::Copy;
      break;
    case RobotEffect::Add:
      cue = Cue::Add;
      break;
    case RobotEffect::Subtract:
      cue = Cue::Subtract;
      break;
    case RobotEffect::Jump:
      cue = Cue::Jump;
      break;
    case RobotEffect::EmptyInbox:
      cue = Cue::EmptyInbox;
      break;
    default:
      break;
    }
    const auto place = robot_.frame().to.place;
    const float pan = place == hrm_app::RobotPlace::Inbox ? -0.6f
      : place == hrm_app::RobotPlace::Outbox              ? 0.6f
                                                          : 0.0f;
    audio_.play(cue, pan);
  }

  /// Sound a final verdict once its animation has settled, including manual fast-forward.
  void settled_sound() {
    const auto verdict = session().machine().state().verdict;
    if (verdict == hrm::Verdict::Success || verdict == hrm::Verdict::Fail) {
      audio_.play(
        verdict == hrm::Verdict::Success ? hrm_audio::Cue::Success : hrm_audio::Cue::Error
      );
    }
  }

  /// Commit one kernel attempt and capture its before/after states for visual playback.
  bool start_step() {
    if (floor_pick_ || linking_) {
      return false;
    }
    if (session().machine().state().status != hrm::ExecutionStatus::Running) {
      return false;
    }
    const auto before = session().machine().state();
    const auto row = session().current_row();
    const auto instruction =
      hrm::decode_line(session().machine().program().instructions[before.pc].text);
    const auto result = session().next();
    if (result.outcome == hrm::StepOutcome::Faulted) {
      audio_.play(hrm_audio::Cue::Error);
    }
    if (session().machine().state().verdict == hrm::Verdict::Success) {
      try {
        solution_store_.save(session().machine());
        std::vector<std::string> lines;
        for (const auto &instruction : session().machine().program().instructions) {
          lines.push_back(instruction.text);
        }
        solutions_[static_cast<std::size_t>(level_index_)] = std::move(lines);
        refresh_levels();
        message_ = "Level completed. Solution saved.";
      } catch (const std::exception &error) {
        message_ = "Completed, but saving failed: " + std::string(error.what());
      }
    }
    animation_paused_ = false;
    return robot_.start(instruction.instruction, result, before, session().machine().state(), row);
  }

  /// Resolve a pointer position to an insertion boundary in the visible stack.
  ///
  /// @param x Horizontal terminal-cell coordinate.
  /// @param y Vertical terminal-cell coordinate.
  /// @return A stable row ID, zero for append, or no value outside the stack.
  std::optional<RowId> drop_target(int x, int y) {
    if (!stack_->viewport().Contain(x, y)) {
      return std::nullopt;
    }
    const auto &boxes = stack_->boxes();
    const auto &rows = session().editor().rows();
    for (std::size_t i = 0; i < std::min(boxes.size(), rows.size()); ++i) {
      const int middle = (boxes[i].y_min + boxes[i].y_max) / 2;
      if (y <= middle) {
        if (rows[i].landing && y == middle) {
          return i + 1 == rows.size() ? 0 : rows[i + 1].id;
        }
        return rows[i].id;
      }
    }
    return RowId(0);
  }

  /// Capture a command and its exact grab point without modifying the program.
  ///
  /// @param opcode Available palette opcode or existing command opcode.
  /// @param source Existing command ID, or zero for a new palette command.
  /// @param mouse Initial press position in terminal cells.
  /// @param source_box Laid-out source bounds defining the preserved size and grab offset.
  void begin_drag(hrm::Opcode opcode, RowId source, const Mouse &mouse, const Box &source_box) {
    playing_ = false;
    animation_paused_ = true;
    DragState state;
    state.opcode = opcode;
    state.source = source;
    state.landing = source && session().editor().find(source)->landing;
    state.operand = source ? session().editor().find(source)->operand : "?";
    state.card_width = source_box.x_max - source_box.x_min + 1;
    state.grab_x = mouse.x - source_box.x_min;
    state.grab_y = mouse.y - source_box.y_min;
    state.label = " " + std::string(hrm_app::opcode_name(opcode)) + " ";
    if (state.landing) {
      state.label = landing_label(source);
    } else if (source) {
      const auto compiled = session().editor().compile();
      const auto position =
        std::find(compiled.instruction_rows.begin(), compiled.instruction_rows.end(), source);
      const auto number =
        static_cast<std::size_t>(position - compiled.instruction_rows.begin()) + 1;
      state.label = (session().current_row() == source ? "▶ " : "  ") + std::to_string(number) +
        "  " + hrm_app::opcode_name(opcode);
      if (hrm_app::uses_floor(opcode)) {
        state.label += "  " + state.operand;
      } else if (hrm_app::is_jump(opcode)) {
        state.label += session().editor().find(source)->target ? "  →" : "  → ?";
      }
    }
    state.start_x = state.x = mouse.x;
    state.start_y = state.y = mouse.y;
    drag_ = std::move(state);
    stack_->TakeFocus();
    if (source) {
      select(source);
    }
    message_ = source ? "Drop in PROGRAM to move; outside the command panel to delete. Esc cancels."
                      : "Drop in PROGRAM to insert. Esc cancels.";
  }

  /// Coordinate drag start, movement, cancellation, and atomic release-time edits.
  ///
  /// @param mouse Current mouse event and cell coordinates.
  /// @return True while a drag owns the event, including the final release.
  bool drag_mouse(const Mouse &mouse) {
    if (drag_) {
      drag_->x = mouse.x;
      drag_->y = mouse.y;
      drag_->moved = drag_->moved || mouse.x != drag_->start_x || mouse.y != drag_->start_y;
      drag_->before = drop_target(mouse.x, mouse.y);
      if (mouse.motion == Mouse::Released) {
        const auto drag = *drag_;
        drag_.reset();
        if (!drag.moved) {
          if (drag.source) {
            choose(drag.source);
            message_ = "Card selected. Drag to reorder, or click a palette instruction.";
          } else if (palette_boxes_[static_cast<std::size_t>(drag.opcode)]
                       .Contain(mouse.x, mouse.y)) {
            add(drag.opcode);
          }
        } else if (drag.before) {
          RowId id = drag.source;
          const bool changed = id
            ? session().edit().move_before(id, *drag.before)
            : (id = session().edit().insert_before(*drag.before, drag.opcode, drag.operand)) != 0;
          if (changed) {
            select(id);
            edited();
            stack_->TakeFocus();
            message_ = drag.landing ? "Landing moved. Its jump follows the new position."
              : hrm_app::is_jump(drag.opcode) && !drag.source
              ? "Jump inserted. Press T to choose its landing card."
              : "Command dropped. Edits reset the simulation.";
            if (!drag.source) {
              parameter_mode(id, true);
            }
          } else {
            message_ = "Command kept in its current position.";
          }
        } else if (drag.source && !commands_box_.Contain(mouse.x, mouse.y)) {
          select(drag.source);
          remove();
        } else {
          message_ = "Drag cancelled. Program unchanged.";
        }
      }
      return true;
    }
    if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed || linking_) {
      return false;
    }
    for (std::size_t i = 0; i < palette_boxes_.size(); ++i) {
      if (palette_box_.Contain(mouse.x, mouse.y) &&
        session().machine().level().allowed_opcodes.test(i) &&
        palette_boxes_[i].Contain(mouse.x, mouse.y)) {
        palette_buttons_[i]->TakeFocus();
        begin_drag(static_cast<hrm::Opcode>(i), 0, mouse, palette_boxes_[i]);
        palette_index_ = i;
        return true;
      }
    }
    if (stack_->viewport().Contain(mouse.x, mouse.y)) {
      const auto &rows = session().editor().rows();
      const auto &boxes = stack_->boxes();
      for (std::size_t i = 0; i < std::min(rows.size(), boxes.size()); ++i) {
        const bool linked = std::any_of(rows.begin(), rows.end(), [&](const auto &row) {
          return !row.landing && row.target == rows[i].id;
        });
        if ((!rows[i].landing || linked) && boxes[i].Contain(mouse.x, mouse.y)) {
          stack_->TakeFocus();
          begin_drag(rows[i].opcode, rows[i].id, mouse, boxes[i]);
          return true;
        }
      }
    }
    return false;
  }
  /// Select a stable editor row.
  void select(RowId id) {
    selected_ = id;
  }
  /// Pause playback, leave link mode, and rebuild the machine after an editor mutation.
  void edited(hrm_audio::Cue cue = hrm_audio::Cue::Insert) {
    audio_.stop_effects();
    audio_.play(cue);
    playing_ = false;
    linking_ = 0;
    floor_pick_ = 0;
    pending_new_ = false;
    hover_floor_.reset();
    link_pointer_ = false;
    session().reset();
    robot_.reset();
    animation_paused_ = false;
  }
  /// Insert an allowed palette command after the selected row and restart simulation.
  ///
  /// @param opcode Palette instruction to add using the current floor-operand text.
  void add(hrm::Opcode opcode) {
    select(session().edit().insert_command(selected_, opcode, "?"));
    edited();
    message_ = hrm_app::is_jump(opcode)
      ? "Press T, select a destination, then Enter to link this jump."
      : "Command inserted. Edits reset the simulation.";
    stack_->TakeFocus();
    parameter_mode(selected_, true);
  }

  /// Request a real floor cell or jump destination before continuing an inserted command.
  /// @param id Existing command to configure.
  /// @param inserted Whether cancelling should remove this newly inserted command.
  void parameter_mode(RowId id, bool inserted) {
    const auto *row = session().editor().find(id);
    if (!row || row->landing) {
      return;
    }
    if (hrm_app::uses_floor(row->opcode)) {
      playing_ = false;
      animation_paused_ = true;
      floor_pick_ = id;
      floor_page_ = 0;
      hover_floor_.reset();
      pending_new_ = inserted;
      message_ = "PICK FLOOR: click a floor cell in the workroom. Wheel pages; Esc cancels.";
    } else if (hrm_app::is_jump(row->opcode)) {
      select(id);
      link();
      pending_new_ = inserted;
    }
  }

  /// Cancel parameter picking, discarding a new command while preserving an existing command.
  void cancel_parameter() {
    const auto id = floor_pick_ ? floor_pick_ : linking_;
    const bool discard = pending_new_;
    floor_pick_ = linking_ = 0;
    pending_new_ = false;
    hover_floor_.reset();
    link_pointer_ = false;
    if (discard) {
      select(id);
      remove();
    }
    message_ = discard ? "Insertion cancelled." : "Parameter selection cancelled.";
  }

  /// Hit-test floor cells and commit an operand only on a click inside a visible real slot.
  bool floor_mouse(const Mouse &mouse) {
    if (!floor_pick_) {
      return false;
    }
    if (room_box_.Contain(mouse.x, mouse.y) &&
      (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown)) {
      const auto count = session().machine().level().floor_size;
      const auto pages = (count + floor_capacity_ - 1) / floor_capacity_;
      if (mouse.button == Mouse::WheelUp && floor_page_ > 0) {
        --floor_page_;
      } else if (mouse.button == Mouse::WheelDown && floor_page_ + 1 < pages) {
        ++floor_page_;
      }
      hover_floor_.reset();
      return true;
    }
    hover_floor_.reset();
    for (const auto &entry : floor_boxes_) {
      if (entry.second.Contain(mouse.x, mouse.y)) {
        hover_floor_ = entry.first;
        break;
      }
    }
    if (hover_floor_ && mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
      const auto slot = *hover_floor_;
      select(floor_pick_);
      session().edit().set_operand(floor_pick_, std::to_string(slot));
      edited();
      stack_->TakeFocus();
      message_ = "Floor [" + std::to_string(slot) + "] selected.";
    }
    return true;
  }
  /// Delete the selected card and repair selection after landing cleanup.
  void remove() {
    const auto rows = session().editor().rows();
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
      return row.id == selected_;
    });
    const auto index = static_cast<std::size_t>(it - rows.begin());
    session().edit().erase(selected_);
    const auto &remaining = session().editor().rows();
    select(remaining[std::min(index, remaining.size() - 1)].id);
    linking_ = 0;
    edited(hrm_audio::Cue::Remove);
    message_ = "Card removed. Jumps to a deleted landing card need a new link.";
  }
  /// Move editor selection by a bounded row offset.
  void move(int delta) {
    const auto &rows = session().editor().rows();
    const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
      return row.id == selected_;
    });
    const auto index =
      std::clamp(static_cast<int>(it - rows.begin()) + delta, 0, static_cast<int>(rows.size()) - 1);
    select(rows[static_cast<std::size_t>(index)].id);
  }
  /// Select a row or complete an in-progress jump link to it.
  ///
  /// @param id Existing destination or selection row ID.
  void choose(RowId id) {
    if (linking_) {
      const auto source = linking_;
      session().edit().set_target(source, id);
      linking_ = 0;
      select(source);
      edited();
      message_ = "Jump linked. Its pixel arrow points to the empty landing card.";
    } else {
      select(id);
    }
  }
  /// Enter target selection for a selected jump, or show why linking is unavailable.
  void link() {
    const auto *row = session().editor().find(selected_);
    if (!row || row->landing || !hrm_app::is_jump(row->opcode)) {
      message_ = "Select an unconditional or conditional jump command first.";
      return;
    }
    playing_ = false;
    linking_ = selected_;
    pending_new_ = false;
    link_pointer_ = false;
    animation_paused_ = true;
    message_ = "LINK TARGET: Up/Down + Enter, or click a card. Esc cancels.";
    stack_->TakeFocus();
  }
  /// Pause and attempt the next kernel instruction.
  void next() {
    playing_ = false;
    const bool active = robot_.active();
    const bool transferred = robot_.frame().transferred;
    robot_.finish();
    if (active) {
      if (!transferred) {
        transfer_sound();
      }
      settled_sound();
    }
    start_step();
  }
  /// Pause and replay to the state preceding the last instruction attempt.
  void previous() {
    audio_.stop_effects();
    playing_ = false;
    session().previous();
    robot_.seek(
      session().machine().level(),
      session().machine().program(),
      session().machine().state().attempted
    );
    animation_paused_ = false;
  }
  /// Pause and clear execution state while retaining the player's edited program.
  void reset() {
    audio_.stop_effects();
    audio_.play(hrm_audio::Cue::Reset);
    playing_ = false;
    session().reset();
    robot_.reset();
    animation_paused_ = false;
    message_ = "Reset: input, output, hand, floor, and counters are clear.";
  }
  /// Toggle playback, restarting a terminal program before playing again.
  void toggle() {
    if (playing_) {
      audio_.stop_effects();
      playing_ = false;
      animation_paused_ = true;
      return;
    }
    if (!robot_.active() && session().machine().state().status != hrm::ExecutionStatus::Running) {
      session().reset();
      robot_.reset();
    }
    animation_paused_ = false;
    playing_ =
      robot_.active() || session().machine().state().status == hrm::ExecutionStatus::Running;
  }
  /// Refresh completion badges without changing the picker selection.
  void refresh_levels() {
    picker_names_.clear();
    for (std::size_t i = 0; i < level_names_.size(); ++i) {
      picker_names_.push_back((solutions_[i] ? "[completed] " : "[unsolved]  ") + level_names_[i]);
    }
  }

  /// Enter a level, optionally replacing its draft with the verified saved solution.
  void enter_level(bool load_saved) {
    const auto index = static_cast<std::size_t>(picker_index_);
    if (load_saved && !solutions_[index]) {
      picker_message_ = "This level has no saved solution.";
      return;
    }
    audio_.stop_effects();
    audio_.play(hrm_audio::Cue::Select);
    if (load_saved) {
      sessions_[index] = std::make_unique<hrm_app::Session>(
        config_.levels[index].definition,
        hrm_app::Editor(*solutions_[index])
      );
    }
    level_index_ = picker_index_;
    shown_level_ = level_index_;
    screen_index_ = 0;
    level_entered_ = true;
    playing_ = false;
    linking_ = 0;
    drag_.reset();
    robot_.seek(
      session().machine().level(),
      session().machine().program(),
      session().machine().state().attempted
    );
    animation_paused_ = false;
    select(session().editor().rows().front().id);
    message_ = load_saved ? "Saved solution loaded. Press Play to run it."
                          : "Build your program. Press / to choose another level.";
    stack_->TakeFocus();
  }

  /// Suspend playback while choosing a level, preserving drafts and physical state.
  void open_levels() {
    if (floor_pick_ || linking_) {
      cancel_parameter();
    }
    audio_.stop_effects();
    playing_ = false;
    animation_paused_ = true;
    drag_.reset();
    linking_ = 0;
    picker_index_ = level_index_;
    picker_message_.clear();
    screen_index_ = 1;
    level_menu_->TakeFocus();
  }

  /// Assemble the component tree and bind shared keyboard, mouse, and transport behavior.
  void build_components() {
    auto menu_options = MenuOption::Vertical();
    menu_options.on_enter = [&] {
      enter_level(false);
    };
    level_menu_ = Menu(&picker_names_, &picker_index_, menu_options);
    open_level_ = action("Open [Enter]", [&] {
      enter_level(false);
    });
    load_solution_ = action("Load solution [L]", [&] {
      enter_level(true);
    });
    picker_controls_ =
      Container::Vertical({level_menu_, Container::Horizontal({open_level_, load_solution_})});
    stack_ = Make<CommandStack>();
    stack_->draw = [&](bool focused, std::vector<Box> &boxes) {
      return draw_stack(focused, boxes);
    };
    stack_->move = [&](int delta) {
      move(delta);
    };
    stack_->pick = [&](std::size_t index) {
      choose(session().editor().rows()[index].id);
    };
    stack_->confirm = [&] {
      if (linking_) {
        choose(selected_);
      }
    };
    stack_->remove = [&] {
      remove();
    };
    Components palette;
    for (std::size_t i = 0; i < hrm::opcode_count; ++i) {
      const auto opcode = static_cast<hrm::Opcode>(i);
      auto button = action(hrm_app::opcode_name(opcode), [&, opcode] {
        add(opcode);
      });
      palette_buttons_[i] = button;
      button = button | reflect(palette_boxes_[i]);
      button = Maybe(button, [&, i] {
        return session().machine().level().allowed_opcodes.test(i);
      });
      palette.push_back(button);
    }
    palette_ = Container::Vertical(std::move(palette));
    auto clear = action("Reset [R]", [&] {
      reset();
    });
    auto back = action("Previous", [&] {
      previous();
    });
    auto play = action(&play_label_, [&] {
      toggle();
    });
    auto forward = action("Next", [&] {
      next();
    });
    SliderOption<int> slider_options;
    slider_options.value = &speed_;
    slider_options.min = 1;
    slider_options.max = 12;
    slider_options.increment = 1;
    slider_options.color_active = teal;
    slider_options.color_inactive = outline;
    speed_slider_ = Slider(slider_options);
    transport_ = Container::Horizontal({clear, back, play, forward, speed_slider_});
    auto hierarchy = Container::Vertical({palette_, stack_, transport_});
    auto screens = Container::Tab({hierarchy, picker_controls_}, &screen_index_);
    root_ = Renderer(screens, [&] {
      return draw();
    });
    root_ = CatchEvent(root_, [&](Event event) {
      const auto &input = event.input();
      const bool keypress = event.is_character() ||
        (!input.empty() &&
          (input.size() == 1 ||
            std::string("ABCDHF~PQRS").find(input.back()) != std::string::npos));
      const bool interaction = event.is_mouse()
        ? event.mouse().motion == Mouse::Pressed && event.mouse().button != Mouse::None
        : keypress && event != Event::Custom && !event.is_cursor_position() &&
          !event.is_cursor_shape();
      if (interaction && screen_index_ == 0) {
        mascot_interacted_ = true;
      }
      if (event == Event::Character('m') || event == Event::Character('n')) {
        if (event == Event::Character('m')) {
          audio_.set_music(!audio_.music_enabled());
        } else {
          audio_.set_effects(!audio_.effects_enabled());
        }
        return true;
      }
      if (event == Event::Character('/')) {
        open_levels();
        return true;
      }
      if (screen_index_ == 1) {
        if (event == Event::Character('l') || event == Event::Character('L')) {
          enter_level(true);
          return true;
        }
        if (event == Event::Escape && level_entered_) {
          screen_index_ = 0;
          stack_->TakeFocus();
          return true;
        }
        if (event == Event::Escape || event == Event::Character('q')) {
          if (quit) {
            quit();
          }
          return true;
        }
        return false;
      }
      if ((floor_pick_ || linking_) &&
        (event == Event::Escape || event == Event::Delete || event == Event::Backspace)) {
        cancel_parameter();
        return true;
      }
      if (event.is_mouse()) {
        const auto &mouse = event.mouse();
        mouse_x_ = mouse.x;
        mouse_y_ = mouse.y;
        if (floor_mouse(mouse)) {
          return true;
        }
        if (linking_) {
          link_pointer_ = true;
          if (stack_->viewport().Contain(mouse.x, mouse.y)) {
            const auto &rows = session().editor().rows();
            const auto &boxes = stack_->boxes();
            for (std::size_t i = 0; i < std::min(rows.size(), boxes.size()); ++i) {
              if (boxes[i].Contain(mouse.x, mouse.y)) {
                select(rows[i].id);
                stack_->TakeFocus();
                if (mouse.button == Mouse::Left && mouse.motion == Mouse::Pressed) {
                  choose(rows[i].id);
                }
                break;
              }
            }
          }
          return true;
        }
        if (!drag_ && palette_box_.Contain(mouse.x, mouse.y) &&
          (mouse.button == Mouse::WheelUp || mouse.button == Mouse::WheelDown)) {
          const int direction = mouse.button == Mouse::WheelUp ? -1 : 1;
          int next = static_cast<int>(palette_index_) + direction;
          while (next >= 0 && next < static_cast<int>(hrm::opcode_count)) {
            if (session().machine().level().allowed_opcodes.test(static_cast<std::size_t>(next))) {
              palette_index_ = static_cast<std::size_t>(next);
              palette_buttons_[palette_index_]->TakeFocus();
              break;
            }
            next += direction;
          }
          return true;
        }
      }
      if (drag_ && event == Event::Escape) {
        drag_.reset();
        message_ = "Drag cancelled. Program unchanged.";
        return true;
      }
      if (event.is_mouse() && drag_mouse(event.mouse())) {
        return true;
      }
      if (drag_) {
        return true;
      }
      if (event == Event::Escape || event == Event::Character('q')) {
        if (quit) {
          quit();
        }
        return true;
      }
      if (floor_pick_) {
        return true;
      }
      if (event == Event::ArrowUp || event == Event::ArrowDown) {
        link_pointer_ = false;
        stack_->TakeFocus();
        move(event == Event::ArrowUp ? -1 : 1);
        return true;
      }
      if (event == Event::Character('f')) {
        parameter_mode(selected_, false);
        return true;
      }
      if (event == Event::Character(' ')) {
        toggle();
        return true;
      }
      if (event == Event::Character('r')) {
        reset();
        return true;
      }
      if (event == Event::Character('t')) {
        link();
        return true;
      }
      if (!speed_slider_->Focused() && event == Event::ArrowLeft) {
        previous();
        return true;
      }
      if (!speed_slider_->Focused() && event == Event::ArrowRight) {
        next();
        return true;
      }
      return false;
    });
    level_menu_->TakeFocus();
  }

  /// Render command cards, jump routes, selection, and the current drop boundary.
  ///
  /// @param focused Whether the stack owns keyboard focus and should follow selection.
  /// @param boxes Receives card bounds during layout for subsequent mouse hit tests.
  /// @return A clipped, scrollable stack whose landing rows do not consume instruction IDs.
  Element draw_stack(bool focused, std::vector<Box> &boxes) {
    const auto &rows = session().editor().rows();
    boxes.resize(rows.size());
    Elements cards;
    std::size_t number = 0;
    const auto active = robot_.active() ? robot_.frame().row : session().current_row();
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto &row = rows[i];
      const bool selected = selected_ == row.id;
      const bool current = !row.landing && active == row.id;
      const bool target = std::any_of(rows.begin(), rows.end(), [&](const auto &source) {
        return source.target == row.id;
      });
      std::string label;
      if (row.landing) {
        label = target ? landing_label(row.id) : "Drop commands here";
      } else {
        label = (current ? "▶ " : "  ") + std::to_string(++number) + "  " +
          hrm_app::opcode_name(row.opcode);
        if (hrm_app::is_jump(row.opcode)) {
          label += row.target ? "  →" : "  → ?";
        } else if (hrm_app::uses_floor(row.opcode)) {
          label += "  " + row.operand;
        }
      }
      const auto edge = row.id == floor_pick_ ? teal
        : current                             ? amber
        : selected                            ? blue
        : target                              ? teal
                                              : outline;
      const bool append_zone = row.landing && !target;
      auto element = append_zone ? text(label) | color(selected ? blue : muted) | center
                                 : card(text(label) | color(row.landing ? muted : ink), edge);
      const bool insert_here = drag_ && drag_->moved && drag_->before &&
        (*drag_->before == row.id || (*drag_->before == 0 && i + 1 == rows.size()));
      if (insert_here) {
        auto line = text("── ▼ INSERT " + drag_name() + " ──") | color(teal) | bold;
        auto marker =
          !append_zone && *drag_->before == 0 ? vbox({filler(), line}) : vbox({line, filler()});
        element = dbox({element, marker});
      }
      if (linking_ && selected) {
        element = dbox({element, vbox({text("── ▼ LAND HERE ──") | color(teal), filler()})});
      }
      element = element | size(HEIGHT, EQUAL, 3) | reflect(boxes[i]);
      if ((playing_ && current) || (!playing_ && focused && selected)) {
        element = element | focus;
      }
      cards.push_back(element);
    }
    auto routes = canvas([this](Canvas &arrows) {
      const auto &rows = session().editor().rows();
      for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (row.landing || !hrm_app::is_jump(row.opcode) || (!row.target && row.id != linking_)) {
          continue;
        }
        const bool preview = row.id == linking_;
        const auto target = preview ? selected_ : row.target;
        const auto destination = std::find_if(rows.begin(), rows.end(), [&](const auto &other) {
          return other.id == target;
        });
        if (destination == rows.end()) {
          continue;
        }
        const int from = static_cast<int>(i) * 12 + 6;
        int to = static_cast<int>(destination - rows.begin()) * 12 + (preview ? 0 : 6);
        if (preview && link_pointer_ && !stack_->boxes().empty()) {
          to = (mouse_y_ - stack_->boxes().front().y_min) * 4 + 2;
        }
        to = std::clamp(to, 0, std::max(0, arrows.height() - 1));
        const int lane = preview ? 14 : 10 + static_cast<int>(i % 3) * 2;
        const auto tint = preview || row.id == selected_ ? amber : teal;
        curved_arrow(arrows, from, to, lane, tint);
      }
    }) |
      size(WIDTH, EQUAL, 8) | size(HEIGHT, EQUAL, static_cast<int>(rows.size()) * 3);
    return hbox({vbox(std::move(cards)) | flex, routes}) | vscroll_indicator | yframe | flex;
  }

  /// Render a conveyor from the kernel's current state.
  ///
  /// @param output Select completed output instead of remaining input.
  /// @return A fixed-width column of value cards, following the latest output when needed.
  Element conveyor(bool output) {
    const auto &state = view_state();
    const auto &values = output ? state.output : session().machine().level().input;
    const auto begin = output ? std::size_t(0) : state.input_cursor;
    Elements blocks;
    for (std::size_t offset = begin; offset < values.size(); ++offset) {
      const std::size_t i = output ? values.size() - 1 - offset : offset;
      auto block = card(
        text(std::to_string(values[i])) | color(ink) | center,
        output         ? blue
          : i == begin ? teal
                       : outline
      );
      if (output && i + 1 == values.size()) {
        block = block | focus;
      }
      blocks.push_back(block);
    }
    if (blocks.empty()) {
      blocks.push_back(text("empty") | color(muted) | center);
    }
    return vbox(
             {text(output ? "OUTBOX" : "INBOX") | bold | color(output ? blue : teal) | center,
               text(std::to_string(values.size() - begin) + (output ? " sent" : " left")) |
                 color(muted) | center,
               text("▼  ▼  ▼") | color(outline) | center,
               vbox(std::move(blocks)) | yframe | flex}
           ) |
      size(WIDTH, EQUAL, 11);
  }

  /// Draw command-driven walking, reaching, transfers, and floor interactions on a cell canvas.
  Element room() {
    return canvas([this](Canvas &drawing) {
      using hrm_app::RobotEffect;
      using hrm_app::RobotPlace;
      const int width = drawing.width() / 2;
      const int height = drawing.height() / 4;
      floor_boxes_.clear();
      if (width <= 0 || height <= 0) {
        return;
      }
      if (width < 13 || height < 16) {
        drawing.DrawText(0, 0, "ROBOT", teal);
        return;
      }
      const auto &state = view_state();
      const auto frame = robot_.frame();
      const bool active = robot_.active();
      auto write = [&](int x, int y, const std::string &value, Color tint) {
        if (y >= 0 && y < height) {
          drawing.DrawText(x * 2, y * 4, value, tint);
        }
      };
      auto box = [&](int center, int top, std::string value, Color tint) {
        if (value.size() > 7) {
          value = value.substr(0, 6) + "~";
        }
        const int x = std::clamp(center - 4, 0, width - 9);
        value = std::string((7 - value.size()) / 2, ' ') + value;
        value.resize(7, ' ');
        write(x, top, "╭───────╮", tint);
        write(x, top + 1, "│" + value + "│", tint);
        write(x, top + 2, "╰───────╯", tint);
      };
      const int floor_top = std::clamp(height * 3 / 5, 11, height - 5);
      const int columns = std::min(3, std::max(1, (width + 1) / 10));
      const int visible_rows = std::clamp((height - floor_top - 2) / 4, 1, 3);
      const auto capacity = static_cast<std::size_t>(columns * visible_rows);
      floor_capacity_ = capacity;
      const auto focus_slot = frame.to.place == RobotPlace::Floor ? frame.to.floor
        : frame.from.place == RobotPlace::Floor                   ? frame.from.floor
                                                                  : std::size_t(0);
      const auto first_slot =
        floor_pick_ ? floor_page_ * capacity : focus_slot / capacity * capacity;
      auto floor_point = [&](std::size_t slot) {
        if (slot < first_slot || slot >= first_slot + capacity) {
          return std::pair<double, double>{width / 2.0, static_cast<double>(floor_top)};
        }
        const int local = static_cast<int>(slot - first_slot);
        return std::pair<double, double>{(width - (columns - 1) * 10) / 2.0 +
            (local % columns) * 10,
          static_cast<double>(floor_top + (local / columns) * 4)};
      };
      const int home_feet = std::clamp(height / 3 + 3, 9, floor_top - 1);
      auto point = [&](hrm_app::RobotLocation location) {
        switch (location.place) {
        case RobotPlace::Inbox:
          return std::pair<double, double>{5, home_feet};
        case RobotPlace::Outbox:
          return std::pair<double, double>{width - 6, home_feet};
        case RobotPlace::Floor: {
          auto value = floor_point(location.floor);
          value.second -= 1;
          return value;
        }
        case RobotPlace::Home:
          return std::pair<double, double>{width / 2.0, home_feet};
        }
        return std::pair<double, double>{width / 2.0, home_feet};
      };

      std::string caption = "ROBOT";
      if (active) {
        switch (frame.effect) {
        case RobotEffect::Pickup:
          caption = "INBOX · pick up";
          break;
        case RobotEffect::Deliver:
          caption = "OUTBOX · deliver";
          break;
        case RobotEffect::Store:
          caption = "COPYTO · place copy";
          break;
        case RobotEffect::Copy:
          caption = "COPYFROM · lift copy";
          break;
        case RobotEffect::Add:
          caption = "ADD · calculate";
          break;
        case RobotEffect::Subtract:
          caption = "SUB · calculate";
          break;
        case RobotEffect::Jump:
          caption = "JUMP · turn";
          break;
        case RobotEffect::Continue:
          caption = "NONZERO · continue";
          break;
        case RobotEffect::EmptyInbox:
          caption = "INBOX EMPTY";
          break;
        case RobotEffect::Fault:
          caption = "BLOCKED · error";
          break;
        case RobotEffect::Idle:
          break;
        }
      }
      const auto accent = active && frame.effect == RobotEffect::Fault ? red : teal;
      write(std::max(0, (width - string_width(caption)) / 2), 0, caption, accent);
      write(std::max(0, width / 2 - 2), floor_top - 1, "FLOOR", muted);
      if (state.floor.empty()) {
        write(std::max(0, width / 2 - 7), floor_top + 1, "No floor slots", muted);
      }
      for (std::size_t slot = first_slot;
        slot < std::min(first_slot + capacity, state.floor.size());
        ++slot) {
        const auto position = floor_point(slot);
        const int x = static_cast<int>(position.first);
        const int y = static_cast<int>(position.second);
        const int left = std::clamp(x - 4, 0, width - 9);
        Box hit;
        hit.x_min = room_box_.x_min + left;
        hit.x_max = hit.x_min + 8;
        hit.y_min = room_box_.y_min + y;
        hit.y_max = hit.y_min + 3;
        floor_boxes_.push_back({slot, hit});
        const auto tint =
          active && frame.to.place == RobotPlace::Floor && frame.to.floor == slot ? amber : outline;
        box(x, y, state.floor[slot].occupied ? std::to_string(state.floor[slot].value) : "-", tint);
        if (floor_pick_ && hover_floor_ == slot) {
          write(left, y, "┏", teal);
          write(left + 8, y, "┓", teal);
          write(left, y + 2, "┗", teal);
          write(left + 8, y + 2, "┛", teal);
        }
        write(x - 2, y + 3, "[" + std::to_string(slot) + "]", tint);
      }
      if (state.floor.size() > capacity) {
        write(
          0,
          height - 2,
          "floor " + std::to_string(first_slot) + ".." +
            std::to_string(std::min(first_slot + capacity, state.floor.size()) - 1),
          muted
        );
      }

      const std::string hand_readout =
        state.hand.occupied ? "HAND: " + std::to_string(state.hand.value) : "HAND: empty";
      write(std::max(0, (width - string_width(hand_readout)) / 2), height - 1, hand_readout, amber);

      const auto from = point(frame.from);
      const auto to = point(frame.to);
      int x = static_cast<int>(std::lround(from.first + (to.first - from.first) * frame.travel));
      int feet =
        static_cast<int>(std::lround(from.second + (to.second - from.second) * frame.travel));
      if (frame.walking && frame.gait) {
        --feet;
      }
      if (active && frame.effect == RobotEffect::Jump && frame.phase > 0.25 && frame.phase < 0.75) {
        --feet;
      }
      x = std::clamp(x, 4, width - 5);
      const bool interacting = active && frame.phase >= 0.55 && frame.phase < 0.85;
      const int lean = interacting && std::sin(frame.interaction * 3.141592653589793) > 0.6 ? 1 : 0;
      const bool distressed = (active && frame.effect == RobotEffect::Fault) ||
        (!active && state.attempted > 0 &&
          (state.status == hrm::ExecutionStatus::Faulted || state.verdict == hrm::Verdict::Fail));
      const auto mascot_color = distressed ? red : amber;
      std::string head = "▐▛███▛█";
      std::string body = "▝▜██████▀";
      std::string legs = " ▝▝   ▝▝";
      int dip = 0;
      if (!active && !distressed) {
        if (mascot_time_ > 3.7 && mascot_time_ < 3.9) {
          head = "▐██████";
        }
        if (mascot_time_ > 1.8 && mascot_time_ < 2.2) {
          legs = " ▘▘   ▘▘";
        }
      } else if (!distressed && frame.walking) {
        legs = frame.gait ? "▝▝     ▝▝" : "  ▝▝ ▝▝";
      } else if (!distressed && frame.effect == RobotEffect::Jump) {
        legs = frame.phase > 0.25 && frame.phase < 0.75 ? " ▀▀   ▀▀" : legs;
      } else if (!distressed && frame.effect == RobotEffect::Continue) {
        legs = frame.gait ? " ▝▝   ▘▘" : legs;
      } else if (!distressed && interacting) {
        switch (frame.effect) {
        case RobotEffect::Pickup:
        case RobotEffect::Copy:
        case RobotEffect::Store:
        case RobotEffect::Deliver:
          dip = lean;
          body = "▀▜██████▀";
          legs = lean ? " ▀▀   ▀▀" : legs;
          break;
        case RobotEffect::Add:
        case RobotEffect::Subtract:
          legs = frame.gait ? " ▝▝   ▘▘" : " ▘▘   ▝▝";
          break;
        case RobotEffect::EmptyInbox:
          write(std::min(x + 5, width - 1), feet - 3, "?", amber);
          break;
        default:
          break;
        }
      }
      auto sprite_row = [&](int left, int top, const std::string &glyphs) {
        if (mascot_interacted_) {
          write(left, top, glyphs, mascot_color);
          return;
        }
        int column = 0;
        for (const auto &glyph : Utf8ToGlyphs(glyphs)) {
          const double hue = std::fmod(
            mascot_time_ / 4.0 + (left + column - x + 4 + (top - feet + 2) * 0.5) / 10.0,
            1.0
          );
          write(left + column, top, glyph, Color::HSV(static_cast<uint8_t>(hue * 255), 220, 255));
          ++column;
        }
      };
      sprite_row(x - 3, feet - 2 + dip, head);
      sprite_row(x - 4, feet - 1 + dip, body);
      sprite_row(x - 4, feet + dip, legs);

      const bool transfer = interacting &&
        (frame.effect == RobotEffect::Pickup || frame.effect == RobotEffect::Deliver ||
          frame.effect == RobotEffect::Store || frame.effect == RobotEffect::Copy);
      if (state.hand.occupied && (!transfer || frame.effect == RobotEffect::Store)) {
        box(x, feet - 6 + lean, std::to_string(state.hand.value), amber);
      } else if (!state.hand.occupied && !transfer) {
        write(x - 5, feet - 5 + lean, "empty hand", muted);
      }
      if (transfer) {
        std::pair<double, double> start{x, feet - 5 + lean};
        auto end = start;
        hrm::Value value = 0;
        if (frame.effect == RobotEffect::Pickup || frame.effect == RobotEffect::Copy) {
          value = robot_.after().hand.value;
          start = frame.effect == RobotEffect::Pickup ? std::pair<double, double>{4, feet - 4}
                                                      : floor_point(frame.to.floor);
          if (frame.effect == RobotEffect::Copy) {
            start.second += 1;
          }
        } else {
          value = robot_.before().hand.value;
          end = frame.effect == RobotEffect::Deliver
            ? std::pair<double, double>{width - 5, feet - 4}
            : floor_point(frame.to.floor);
          if (frame.effect == RobotEffect::Store) {
            end.second += 1;
          }
        }
        const double t = frame.interaction * frame.interaction * (3.0 - 2.0 * frame.interaction);
        box(
          static_cast<int>(std::lround(start.first + (end.first - start.first) * t)),
          static_cast<int>(std::lround(start.second + (end.second - start.second) * t)) - 1,
          std::to_string(value),
          amber
        );
      }
      if (interacting &&
        (frame.effect == RobotEffect::Add || frame.effect == RobotEffect::Subtract)) {
        const auto value = robot_.before().floor[frame.to.floor].value;
        const std::string calculation = frame.transferred
          ? "= " + std::to_string(state.hand.value)
          : (frame.effect == RobotEffect::Add ? "+ " : "- ") + std::to_string(value);
        write(
          std::clamp(x + 5, 0, std::max(0, width - static_cast<int>(calculation.size()))),
          feet - 4,
          calculation,
          amber
        );
      }
    }) |
      flex | reflect(room_box_);
  }

  /// Overlay a dragged card at its original grab offset without changing underlying layout.
  ///
  /// @param view Root workroom view to preserve underneath the overlay.
  /// @param width Current terminal width in cells.
  /// @param height Current terminal height in cells.
  /// @return The original view or a bounded floating-card overlay during an active drag.
  Element with_drag_preview(Element view, int width, int height) {
    if (!drag_ || !drag_->moved) {
      return view;
    }
    const int card_width = std::clamp(drag_->card_width, 3, width);
    const int x = std::clamp(drag_->x - drag_->grab_x, 0, width - card_width);
    const int y = std::clamp(drag_->y - drag_->grab_y, 0, height - 3);
    const bool deleting = drag_->source && !commands_box_.Contain(drag_->x, drag_->y);
    auto preview = card(
                     text(drag_->label) | color(ink) | bold,
                     deleting          ? red
                       : drag_->before ? teal
                                       : amber
                   ) |
      size(WIDTH, EQUAL, card_width) | size(HEIGHT, EQUAL, 3) | clear_under;
    auto overlay = vbox(
      {filler() | size(HEIGHT, EQUAL, y),
        hbox({filler() | size(WIDTH, EQUAL, x), preview, filler()}),
        filler()}
    );
    return dbox({view, overlay});
  }

  /// Render the responsive workroom and command panel, synchronizing level selection first.
  Element draw() {
    if (shown_level_ != level_index_) {
      shown_level_ = level_index_;
      playing_ = false;
      linking_ = 0;
      drag_.reset();
      robot_.seek(
        session().machine().level(),
        session().machine().program(),
        session().machine().state().attempted
      );
      animation_paused_ = false;
      select(session().editor().rows().front().id);
      message_ = "Level loaded from YAML. Edits and playback are kept per level.";
    }
    play_label_ = playing_ ? "Pause [Space]" : "Play [Space]";
    const auto dimensions = Terminal::Size();
    const int width = preview_width_ ? preview_width_ : dimensions.dimx;
    const int height = preview_height_ ? preview_height_ : dimensions.dimy;
    if (screen_index_ == 1) {
      const auto &level = config_.levels[static_cast<std::size_t>(picker_index_)];
      return vbox(
               {text("CHOOSE A LEVEL") | bold | color(teal),
                 separator(),
                 level_menu_->Render() | vscroll_indicator | frame |
                   size(HEIGHT, LESS_THAN, std::max(3, height / 3)),
                 separator(),
                 text(level.title) | bold,
                 paragraph(level.definition.description),
                 text(
                   solutions_[static_cast<std::size_t>(picker_index_)]
                     ? "Completed · saved solution available"
                     : "No saved solution yet"
                 ) |
                   color(amber),
                 hbox({open_level_->Render(), text(" "), load_solution_->Render()}),
                 paragraph(picker_message_) | color(red),
                 text("↑/↓ choose · Enter open/resume · L load solution · Esc back · Q quit"),
                 text(audio_status()) | color(muted)}
             ) |
        size(WIDTH, EQUAL, std::max(1, std::min(84, width - 4))) | borderRounded | center |
        color(ink) | bgcolor(background);
    }
    if (width < 112 || height < 32) {
      drag_.reset();
      return vbox(
               {text("HUMAN RESOURCE MACHINE") | bold | color(teal),
                 text("Resize the terminal to at least 112 columns × 32 rows."),
                 text("Current: " + std::to_string(width) + " × " + std::to_string(height)),
                 text("Q / Esc to quit") | color(muted)}
             ) |
        center | bgcolor(background);
    }
    const auto &level = config_.levels[static_cast<std::size_t>(level_index_)];
    const auto &state = view_state();
    std::string status = playing_             ? "RUNNING"
      : robot_.active() && !animation_paused_ ? "STEPPING"
                                              : "PAUSED";
    Color status_color = playing_ ? teal : amber;
    if (!robot_.active() && state.verdict == hrm::Verdict::Success) {
      status = "SUCCESS";
      status_color = teal;
    }
    if (!robot_.active() && state.verdict == hrm::Verdict::Fail) {
      status = "FAIL · output differs from target";
      status_color = red;
    }
    if (state.verdict == hrm::Verdict::Error) {
      status = "ERROR #" + std::to_string(state.diagnostic.instruction) + " · " +
        error_text(state.diagnostic.code);
      status_color = red;
    }
    if (session().machine().program().instructions.empty()) {
      status = "READY · add commands from the palette";
      status_color = teal;
    }
    auto board = vbox(
                   {hbox(
                      {text(" WORKROOM ") | bold | color(teal),
                        filler(),
                        text("steps " + std::to_string(state.completed) + " ") | color(muted)}
                    ),
                     separator() | color(outline),
                     hbox(
                       {conveyor(false),
                         separator() | color(outline),
                         room(),
                         separator() | color(outline),
                         conveyor(true)}
                     ) |
                       flex}
                 ) |
      flex;
    auto description = card(vbox(
                         {text(level.title) | bold | color(teal),
                           paragraph(level.definition.description) | color(ink),
                           paragraph("Goal: " + sequence(level.definition.target)) | color(amber)}
                       )) |
      size(HEIGHT, EQUAL, 10);
    auto buttons = hbox(
      {transport_->ChildAt(0)->Render(),
        transport_->ChildAt(1)->Render(),
        transport_->ChildAt(2)->Render(),
        transport_->ChildAt(3)->Render()}
    );
    auto speed = hbox(
      {text("  Speed ") | vcenter,
        speed_slider_->Render() | vcenter | size(WIDTH, EQUAL, 22),
        filler()}
    );
    const int command_width = std::clamp(width / 2, 56, 58);
    const int workroom_width = width - command_width - 1;
    auto playback =
      workroom_width >= 80 ? hbox({buttons, speed | vcenter}) : vbox({buttons, speed});
    auto workroom =
      vbox(
        {board | flex,
          separator() | color(outline),
          hbox(
            {text(" " + status) | color(status_color) | bold,
              filler(),
              text(std::to_string(speed_) + " steps/s ") | color(muted)}
          ),
          playback,
          text(hint_line(workroom_width)) | size(WIDTH, EQUAL, workroom_width) | color(muted)}
      ) |
      size(WIDTH, EQUAL, workroom_width);
    auto commands =
      vbox(
        {description,
          hbox(
            {vbox(
               {text(" COMMANDS") | color(blue) | bold,
                 palette_->Render() | vscroll_indicator | yframe | flex | reflect(palette_box_)}
             ) | size(WIDTH, EQUAL, 20),
              separator() | color(outline),
              vbox(
                {hbox(
                   {text(" PROGRAM") | color(blue) | bold,
                     filler(),
                     text(
                       drag_ && drag_->source && drag_->moved &&
                           !commands_box_.Contain(drag_->x, drag_->y)
                         ? "DELETE"
                         : drag_ && drag_->before ? "INSERT " + drag_name()
                         : linking_               ? "PICK JUMP"
                         : floor_pick_            ? "PICK FLOOR"
                                                  : ""
                     ) |
                       color(amber)}
                 ),
                  stack_->Render() | flex}
              ) |
                flex}
          ) |
            flex,
          paragraph(" " + message_) | size(HEIGHT, EQUAL, 2) |
            color(linking_ || floor_pick_ ? amber : muted)}
      ) |
      size(WIDTH, EQUAL, command_width) | reflect(commands_box_);
    auto view = hbox({workroom, separator() | color(outline), commands}) | flex | color(ink) |
      bgcolor(background);
    return with_drag_preview(view, width, height);
  }

  /// Identify a landing's owning jump by its current executable instruction number.
  std::string landing_label(RowId id) const {
    const auto &editor = sessions_[static_cast<std::size_t>(level_index_)]->editor();
    std::size_t number = 0;
    for (const auto &row : editor.rows()) {
      if (!row.landing) {
        ++number;
        if (row.target == id) {
          return " LANDING · jump #" + std::to_string(number);
        }
      }
    }
    return " LANDING";
  }

  /// Name the dragged object without treating landing cards as executable opcodes.
  std::string drag_name() const {
    return drag_ && drag_->landing ? "landing" : drag_ ? hrm_app::opcode_name(drag_->opcode) : "";
  }

  /// Keep all shortcuts on one workroom footer row, scrolling right-to-left only on overflow.
  std::string hint_line(int width) const {
    std::string line = "/ levels · Q quit · Space play/pause · ←/→ step · R reset · "
                       "↑/↓ select · F floor · T link · Del delete · " +
      audio_status();
    if (string_width(line) <= width) {
      return line;
    }
    const auto glyphs = Utf8ToGlyphs(line + "     ");
    std::string result;
    int cells = 0;
    for (std::size_t i = 0; cells < width && i < glyphs.size(); ++i) {
      const auto &glyph = glyphs[(hint_offset_ + i) % glyphs.size()];
      const int size = string_width(glyph);
      if (cells + size > width) {
        break;
      }
      result += glyph;
      cells += size;
    }
    return result;
  }

  /// Summarize shared music/effect controls without claiming unavailable device playback.
  std::string audio_status() const {
    if (!audio_.ready()) {
      return "Audio unavailable / disabled";
    }
    return std::string("M music: ") + (audio_.music_enabled() ? "on" : "off") +
      " · N SFX: " + (audio_.effects_enabled() ? "on" : "off");
  }

  hrm_app::GameConfig config_;
  hrm_audio::Audio audio_;
  hrm_app::SolutionStore solution_store_{hrm_app::solution_directory()};
  std::vector<std::optional<std::vector<std::string>>> solutions_;
  std::vector<std::unique_ptr<hrm_app::Session>> sessions_;
  std::vector<std::string> level_names_;
  std::vector<std::string> picker_names_;
  std::string picker_message_;
  int picker_index_ = 0, screen_index_ = 1;
  bool level_entered_ = false;
  int level_index_ = 0, shown_level_ = 0, speed_ = 4, preview_width_ = 0, preview_height_ = 0;
  RowId selected_ = 0, linking_ = 0, floor_pick_ = 0;
  bool pending_new_ = false, link_pointer_ = false;
  int mouse_x_ = 0, mouse_y_ = 0;
  Box room_box_, commands_box_, palette_box_;
  std::vector<std::pair<std::size_t, Box>> floor_boxes_;
  std::optional<std::size_t> hover_floor_;
  std::size_t floor_page_ = 0, floor_capacity_ = 1, hint_offset_ = 0, palette_index_ = 0;
  double hint_time_ = 0.0;
  bool playing_ = false, redraw_ = false;
  std::string play_label_ = "Play [Space]";
  std::string message_ =
    "Build a program with the palette. Click a card to select its insertion point.";
  std::chrono::steady_clock::time_point last_tick_{};
  std::chrono::steady_clock::time_point next_frame_{};
  std::chrono::steady_clock::time_point next_scroll_{};
  std::optional<DragState> drag_;
  hrm_app::RobotAnimation robot_;
  double mascot_time_ = 0.0;
  bool mascot_interacted_ = false;
  bool animation_paused_ = false;
  std::array<Box, hrm::opcode_count> palette_boxes_;
  std::array<Component, hrm::opcode_count> palette_buttons_;
  Component picker_controls_, open_level_, load_solution_;
  Component root_, level_menu_, palette_, transport_, speed_slider_;
  std::shared_ptr<CommandStack> stack_;
};

} // namespace

/// Parse frontend options and run the interactive workroom or a single preview frame.
///
/// @param argc Number of process arguments.
/// @param argv Executable path and optional configuration, level, or preview options.
/// @return Zero after normal exit/help, or one after a configuration or argument failure.
int main(int argc, char **argv) {
  try {
    std::optional<std::filesystem::path> config;
    int level = 1, width = 140, height = 44;
    bool preview = false, audio_enabled = true;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--help") {
        std::cout << "Usage: hrm-tui [--config game.yaml] [--level ID] [--preview] [--width N] "
                     "[--height N] [--no-audio]\n";
        return 0;
      }
      if (argument == "--preview") {
        preview = true;
        continue;
      }
      if (argument == "--no-audio") {
        audio_enabled = false;
        continue;
      }
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + argument);
      }
      const std::string value = argv[++i];
      if (argument == "--config") {
        config = value;
        continue;
      }
      const auto parsed = hrm::parse_integer(value);
      if (parsed.error != hrm::ErrorCode::None || parsed.value < 1 || parsed.value > 10000) {
        throw std::runtime_error("invalid value for " + argument);
      }
      if (argument == "--level") {
        level = static_cast<int>(parsed.value);
      } else if (argument == "--width") {
        width = static_cast<int>(parsed.value);
      } else if (argument == "--height") {
        height = static_cast<int>(parsed.value);
      } else {
        throw std::runtime_error("unknown option: " + argument);
      }
    }
    auto game = config ? hrm_app::load_config(*config) : hrm_app::load_bundled_config();
    if (std::none_of(game.levels.begin(), game.levels.end(), [&](const auto &item) {
          return item.definition.id == level;
        })) {
      throw std::runtime_error("level not found in configuration: " + std::to_string(level));
    }
    Application application(std::move(game), level, audio_enabled && !preview);
    if (preview) {
      application.set_preview_size(width, height);
      auto screen = Screen::Create(Dimension::Fixed(width), Dimension::Fixed(height));
      Render(screen, application.component()->Render());
      std::cout << screen.ToString() << '\n';
      return 0;
    }
    auto screen = ScreenInteractive::Fullscreen();
    application.quit = screen.ExitLoopClosure();
    Loop loop(&screen, application.component());
    while (!loop.HasQuitted()) {
      loop.RunOnce();
      application.tick(std::chrono::steady_clock::now());
      if (application.needs_redraw()) {
        screen.PostEvent(Event::Custom);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
  } catch (const std::exception &error) {
    std::cerr << "hrm-tui: " << error.what() << '\n';
    return 1;
  }
}
