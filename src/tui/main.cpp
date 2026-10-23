#include "hrm_app/editor.hpp"
#include "hrm_app/level_config.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <array>
#include <chrono>
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

Element card(Element content, Color edge = outline) {
  return content | borderRounded | color(edge) | bgcolor(surface);
}

Component action(ConstStringRef label, std::function<void()> handler) {
  auto options = ButtonOption::Simple();
  options.transform = [](const EntryState &state) {
    return card(text(" " + state.label + " ") | color(state.focused ? teal : ink),
                state.focused ? teal : outline);
  };
  return Button(label, std::move(handler), options);
}

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

class CommandStack : public ComponentBase {
public:
  std::function<Element(bool, std::vector<Box> &)> draw;
  std::function<void(std::size_t)> pick;
  std::function<void(int)> move;
  std::function<void()> confirm;
  std::function<void()> remove;

  const std::vector<Box> &boxes() const { return boxes_; }
  const Box &viewport() const { return viewport_; }

  bool Focusable() const override { return true; }
  Element OnRender() override { return draw(Focused(), boxes_) | reflect(viewport_); }
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

class Application {
public:
  explicit Application(hrm_app::GameConfig config, int initial_level) : config_(std::move(config)) {
    for (std::size_t i = 0; i < config_.levels.size(); ++i) {
      const auto &level = config_.levels[i];
      level_names_.push_back(std::to_string(level.definition.id) + " / " + level.title);
      sessions_.push_back(std::make_unique<hrm_app::Session>(level.definition, hrm_app::Editor()));
      if (level.definition.id == initial_level) {
        level_index_ = static_cast<int>(i);
      }
    }
    shown_level_ = level_index_;
    select(session().editor().rows().front().id);
    build_components();
  }

  Component component() const { return root_; }
  void set_preview_size(int width, int height) {
    preview_width_ = width;
    preview_height_ = height;
  }
  std::function<void()> quit;

  void tick(std::chrono::steady_clock::time_point now) {
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
    if (!playing_) {
      next_tick_ = now;
      return;
    }
    if (now >= next_tick_) {
      session().next();
      if (session().machine().state().status != hrm::ExecutionStatus::Running) {
        playing_ = false;
      }
      next_tick_ = now + std::chrono::milliseconds(1000 / speed_);
      redraw_ = true;
    }
  }
  bool needs_redraw() { return std::exchange(redraw_, false); }

private:
  struct DragState {
    hrm::Opcode opcode;
    RowId source = 0;
    std::string operand;
    std::string label;
    int card_width = 0, grab_x = 0, grab_y = 0;
    int start_x = 0, start_y = 0, x = 0, y = 0;
    bool moved = false;
    std::optional<RowId> before;
  };

  hrm_app::Session &session() { return *sessions_[static_cast<std::size_t>(level_index_)]; }

  std::optional<RowId> drop_target(int x, int y) {
    if (!stack_->viewport().Contain(x, y)) {
      return std::nullopt;
    }
    const auto &boxes = stack_->boxes();
    const auto &rows = session().editor().rows();
    for (std::size_t i = 0; i < std::min(boxes.size(), rows.size()); ++i) {
      if (y <= (boxes[i].y_min + boxes[i].y_max) / 2) {
        return i + 1 == rows.size() && rows[i].landing ? 0 : rows[i].id;
      }
    }
    return RowId(0);
  }

  void begin_drag(hrm::Opcode opcode, RowId source, const Mouse &mouse, const Box &source_box) {
    playing_ = false;
    DragState state;
    state.opcode = opcode;
    state.source = source;
    state.operand = source ? session().editor().find(source)->operand : operand_text_;
    state.card_width = source_box.x_max - source_box.x_min + 1;
    state.grab_x = mouse.x - source_box.x_min;
    state.grab_y = mouse.y - source_box.y_min;
    state.label = " " + std::string(hrm_app::opcode_name(opcode)) + " ";
    if (source) {
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
        state.label += session().editor().find(source)->target ? "  → landing" : "  → ? [T]";
      }
    }
    state.start_x = state.x = mouse.x;
    state.start_y = state.y = mouse.y;
    drag_ = std::move(state);
    stack_->TakeFocus();
    if (source) {
      select(source);
    }
    message_ = "Drag to an insertion line; release outside the stack or press Esc to cancel.";
  }

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
          } else if (palette_boxes_[static_cast<std::size_t>(drag.opcode)].Contain(mouse.x,
                                                                                   mouse.y)) {
            add(drag.opcode);
          }
        } else if (drag.before) {
          RowId id = drag.source;
          const bool changed = id ? session().edit().move_before(id, *drag.before)
                                  : (id = session().edit().insert_before(*drag.before, drag.opcode,
                                                                         drag.operand)) != 0;
          if (changed) {
            select(id);
            edited();
            stack_->TakeFocus();
            message_ = hrm_app::is_jump(drag.opcode) && !drag.source
                           ? "Jump inserted. Press T to choose its landing card."
                           : "Command dropped. Edits reset the simulation.";
          } else {
            message_ = "Command kept in its current position.";
          }
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
      if (session().machine().level().allowed_opcodes.test(i) &&
          palette_boxes_[i].Contain(mouse.x, mouse.y)) {
        palette_buttons_[i]->TakeFocus();
        begin_drag(static_cast<hrm::Opcode>(i), 0, mouse, palette_boxes_[i]);
        return true;
      }
    }
    if (stack_->viewport().Contain(mouse.x, mouse.y)) {
      const auto &rows = session().editor().rows();
      const auto &boxes = stack_->boxes();
      for (std::size_t i = 0; i < std::min(rows.size(), boxes.size()); ++i) {
        if (!rows[i].landing && boxes[i].Contain(mouse.x, mouse.y)) {
          stack_->TakeFocus();
          begin_drag(rows[i].opcode, rows[i].id, mouse, boxes[i]);
          return true;
        }
      }
    }
    return false;
  }
  void select(RowId id) {
    selected_ = id;
    const auto *row = session().editor().find(id);
    operand_text_ = row && !row->landing && hrm_app::uses_floor(row->opcode) ? row->operand : "0";
  }
  void edited() {
    playing_ = false;
    linking_ = 0;
    session().reset();
  }
  void add(hrm::Opcode opcode) {
    select(session().edit().insert_command(selected_, opcode, operand_text_));
    edited();
    message_ = hrm_app::is_jump(opcode)
                   ? "Press T, select a destination, then Enter to link this jump."
                   : "Command inserted. Edits reset the simulation.";
    stack_->TakeFocus();
  }
  void remove() {
    const auto rows = session().editor().rows();
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [&](const auto &row) { return row.id == selected_; });
    const auto index = static_cast<std::size_t>(it - rows.begin());
    session().edit().erase(selected_);
    const auto &remaining = session().editor().rows();
    select(remaining[std::min(index, remaining.size() - 1)].id);
    linking_ = 0;
    edited();
    message_ = "Card removed. Jumps to a deleted landing card need a new link.";
  }
  void move(int delta) {
    const auto &rows = session().editor().rows();
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [&](const auto &row) { return row.id == selected_; });
    const auto index = std::clamp(static_cast<int>(it - rows.begin()) + delta, 0,
                                  static_cast<int>(rows.size()) - 1);
    select(rows[static_cast<std::size_t>(index)].id);
  }
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
  void link() {
    const auto *row = session().editor().find(selected_);
    if (!row || row->landing || !hrm_app::is_jump(row->opcode)) {
      message_ = "Select a jump or jumpifzero command first.";
      return;
    }
    playing_ = false;
    linking_ = selected_;
    message_ = "LINK TARGET: Up/Down + Enter, or click a card. Esc cancels.";
    stack_->TakeFocus();
  }
  void next() {
    playing_ = false;
    session().next();
  }
  void previous() {
    playing_ = false;
    session().previous();
  }
  void reset() {
    playing_ = false;
    session().reset();
    message_ = "Reset: input, output, hand, floor, and counters are clear.";
  }
  void toggle() {
    if (!playing_ && session().machine().state().status != hrm::ExecutionStatus::Running) {
      session().reset();
    }
    playing_ = !playing_ && session().machine().state().status == hrm::ExecutionStatus::Running;
  }
  void apply_operand() {
    const auto *row = session().editor().find(selected_);
    if (row && !row->landing && hrm_app::uses_floor(row->opcode)) {
      const auto parsed = hrm::parse_integer(operand_text_);
      if (parsed.error == hrm::ErrorCode::None) {
        operand_text_ = std::to_string(parsed.value);
      }
      session().edit().set_operand(selected_, operand_text_);
      edited();
      message_ = "Floor operand updated.";
      stack_->TakeFocus();
    } else {
      message_ = "Select add, sub, copyto, or copyfrom to set its floor index.";
    }
  }

  void build_components() {
    level_menu_ = Dropdown(&level_names_, &level_index_);
    stack_ = Make<CommandStack>();
    stack_->draw = [&](bool focused, std::vector<Box> &boxes) {
      return draw_stack(focused, boxes);
    };
    stack_->move = [&](int delta) { move(delta); };
    stack_->pick = [&](std::size_t index) { choose(session().editor().rows()[index].id); };
    stack_->confirm = [&] {
      if (linking_) {
        choose(selected_);
      }
    };
    stack_->remove = [&] { remove(); };
    Components first, second;
    for (std::size_t i = 0; i < 8; ++i) {
      const auto opcode = static_cast<hrm::Opcode>(i);
      auto button = action(hrm_app::opcode_name(opcode), [&, opcode] { add(opcode); });
      palette_buttons_[i] = button;
      button = button | reflect(palette_boxes_[i]);
      button =
          Maybe(button, [&, i] { return session().machine().level().allowed_opcodes.test(i); });
      (i < 4 ? first : second).push_back(button);
    }
    palette_first_ = Container::Horizontal(std::move(first));
    palette_second_ = Container::Horizontal(std::move(second));
    auto input_options = InputOption::Default();
    input_options.multiline = false;
    input_options.on_enter = [&] { apply_operand(); };
    operand_input_ = Input(&operand_text_, "floor index", input_options);
    apply_ = action("Set floor", [&] { apply_operand(); });
    link_ = action("Link [T]", [&] { link(); });
    erase_ = action("Delete", [&] { remove(); });
    edit_controls_ = Container::Horizontal({operand_input_, apply_, link_, erase_});
    auto clear = action("Reset [R]", [&] { reset(); });
    auto back = action("Previous", [&] { previous(); });
    auto play = action(&play_label_, [&] { toggle(); });
    auto forward = action("Next", [&] { next(); });
    SliderOption<int> slider_options;
    slider_options.value = &speed_;
    slider_options.min = 1;
    slider_options.max = 12;
    slider_options.increment = 1;
    slider_options.color_active = teal;
    slider_options.color_inactive = outline;
    speed_slider_ = Slider(slider_options);
    transport_ = Container::Horizontal({clear, back, play, forward, speed_slider_});
    auto hierarchy = Container::Vertical(
        {level_menu_, palette_first_, palette_second_, stack_, edit_controls_, transport_});
    root_ = Renderer(hierarchy, [&] { return draw(); });
    root_ = CatchEvent(root_, [&](Event event) {
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
      if (event == Event::Escape && linking_) {
        linking_ = 0;
        message_ = "Jump linking cancelled.";
        return true;
      }
      if (event == Event::Escape ||
          (!operand_input_->Focused() && event == Event::Character('q'))) {
        if (quit) {
          quit();
        }
        return true;
      }
      if (operand_input_->Focused()) {
        return false;
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
    stack_->TakeFocus();
  }

  Element draw_stack(bool focused, std::vector<Box> &boxes) {
    const auto &rows = session().editor().rows();
    boxes.resize(rows.size());
    Elements cards;
    Canvas arrows(16, static_cast<int>(rows.size()) * 12);
    std::size_t number = 0;
    const auto active = session().current_row();
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto &row = rows[i];
      const bool selected = selected_ == row.id;
      const bool current = !row.landing && active == row.id;
      const bool target = std::any_of(rows.begin(), rows.end(),
                                      [&](const auto &source) { return source.target == row.id; });
      std::string label;
      if (row.landing) {
        label = target ? "  LANDING  ·  insert below" : "Drop commands here";
      } else {
        label = (current ? "▶ " : "  ") + std::to_string(++number) + "  " +
                hrm_app::opcode_name(row.opcode);
        if (hrm_app::is_jump(row.opcode)) {
          label += row.target ? "  → landing" : "  → ? [T]";
        } else if (hrm_app::uses_floor(row.opcode)) {
          label += "  " + row.operand;
        }
      }
      const auto edge = current ? amber : selected ? blue : target ? teal : outline;
      const bool append_zone = row.landing && !target;
      auto element = append_zone ? text(label) | color(selected ? blue : muted) | center
                                 : card(text(label) | color(row.landing ? muted : ink), edge);
      const bool insert_here =
          drag_ && drag_->moved && drag_->before &&
          (*drag_->before == row.id || (*drag_->before == 0 && i + 1 == rows.size()));
      if (insert_here) {
        auto line =
            text("── ▼ INSERT " + std::string(hrm_app::opcode_name(drag_->opcode)) + " ──") |
            color(teal) | bold;
        auto marker =
            !append_zone && *drag_->before == 0 ? vbox({filler(), line}) : vbox({line, filler()});
        element = dbox({element, marker});
      }
      element = element | size(HEIGHT, EQUAL, 3) | reflect(boxes[i]);
      if ((playing_ && current) || (!playing_ && focused && selected)) {
        element = element | focus;
      }
      cards.push_back(element);
      if (!row.landing && hrm_app::is_jump(row.opcode) && row.target) {
        const auto destination = std::find_if(
            rows.begin(), rows.end(), [&](const auto &other) { return other.id == row.target; });
        if (destination != rows.end()) {
          const int from = static_cast<int>(i) * 12 + 6;
          const int to = static_cast<int>(destination - rows.begin()) * 12 + 6;
          const int lane = 1 + static_cast<int>(i % 4) * 3;
          const auto tint = selected || current ? amber : teal;
          arrows.DrawBlockLine(15, from, lane, from, tint);
          arrows.DrawBlockLine(lane, from, lane, to, tint);
          arrows.DrawBlockLine(lane, to, 15, to, tint);
          for (int x = 10; x <= 15; ++x) {
            const int radius = 15 - x;
            arrows.DrawBlockLine(x, to - radius, x, to + radius, tint);
          }
        }
      }
    }
    return hbox({canvas(std::move(arrows)), vbox(std::move(cards)) | flex}) | vscroll_indicator |
           yframe | flex;
  }

  Element conveyor(bool output) {
    const auto &state = session().machine().state();
    const auto &values = output ? state.output : session().machine().level().input;
    const auto begin = output ? std::size_t(0) : state.input_cursor;
    Elements blocks;
    for (std::size_t i = begin; i < values.size(); ++i) {
      auto block =
          card(text(std::to_string(values[i])) | color(ink) | center, output       ? blue
                                                                      : i == begin ? teal
                                                                                   : outline);
      if (output && i + 1 == values.size()) {
        block = block | focus;
      }
      blocks.push_back(block);
    }
    if (blocks.empty()) {
      blocks.push_back(text("empty") | color(muted) | center);
    }
    return vbox({text(output ? "OUTBOX" : "INBOX") | bold | color(output ? blue : teal) | center,
                 text(std::to_string(values.size() - begin) + (output ? " sent" : " remaining")) |
                     color(muted) | center,
                 text("▼  ▼  ▼") | color(outline) | center,
                 vbox(std::move(blocks)) | yframe | flex}) |
           size(WIDTH, EQUAL, 13);
  }

  Element room() {
    const auto &state = session().machine().state();
    Elements floor;
    for (std::size_t i = 0; i < state.floor.size(); i += 3) {
      Elements line;
      for (std::size_t j = i; j < std::min(i + 3, state.floor.size()); ++j) {
        line.push_back(
            vbox({card(text(state.floor[j].occupied ? std::to_string(state.floor[j].value) : "·") |
                       color(ink) | center),
                  text("[" + std::to_string(j) + "]") | color(muted) | center}) |
            size(WIDTH, EQUAL, 10));
      }
      floor.push_back(hbox(std::move(line)) | center);
    }
    if (floor.empty()) {
      floor.push_back(text("No floor slots") | color(muted) | center);
    }
    return vbox({text("ROBOT") | color(muted) | center,
                 card(text(state.hand.occupied ? std::to_string(state.hand.value) : "empty hand") |
                          color(amber) | center,
                      amber) |
                     hcenter,
                 text("  ▄▄▄▄▄  ") | color(teal) | center, text(" ▐ ● ● ▌ ") | color(teal) | center,
                 text("  ▀┬─┬▀  ") | color(teal) | center, text("  ╱│ │╲  ") | color(teal) | center,
                 text("   ╱ ╲   ") | color(teal) | center, filler(),
                 text("FLOOR") | color(muted) | center, vbox(std::move(floor)) | yframe | flex,
                 filler()}) |
           flex;
  }

  Element with_drag_preview(Element view, int width, int height) {
    if (!drag_ || !drag_->moved) {
      return view;
    }
    const int card_width = std::clamp(drag_->card_width, 3, width);
    const int x = std::clamp(drag_->x - drag_->grab_x, 0, width - card_width);
    const int y = std::clamp(drag_->y - drag_->grab_y, 0, height - 3);
    auto preview = card(text(drag_->label) | color(ink) | bold, drag_->before ? teal : amber) |
                   size(WIDTH, EQUAL, card_width) | size(HEIGHT, EQUAL, 3) | clear_under;
    auto overlay = vbox({filler() | size(HEIGHT, EQUAL, y),
                         hbox({filler() | size(WIDTH, EQUAL, x), preview, filler()}), filler()});
    return dbox({view, overlay});
  }

  Element draw() {
    if (shown_level_ != level_index_) {
      shown_level_ = level_index_;
      playing_ = false;
      linking_ = 0;
      drag_.reset();
      select(session().editor().rows().front().id);
      message_ = "Level loaded from YAML. Edits and playback are kept per level.";
    }
    play_label_ = playing_ ? "Pause [Space]" : "Play [Space]";
    const auto dimensions = Terminal::Size();
    const int width = preview_width_ ? preview_width_ : dimensions.dimx;
    const int height = preview_height_ ? preview_height_ : dimensions.dimy;
    if (width < 112 || height < 32) {
      drag_.reset();
      return vbox({text("HUMAN RESOURCE MACHINE") | bold | color(teal),
                   text("Resize the terminal to at least 112 columns × 32 rows."),
                   text("Current: " + std::to_string(width) + " × " + std::to_string(height)),
                   text("Q / Esc to quit") | color(muted)}) |
             center | bgcolor(background);
    }
    const auto &level = config_.levels[static_cast<std::size_t>(level_index_)];
    const auto &state = session().machine().state();
    std::string status = playing_ ? "RUNNING" : "PAUSED";
    Color status_color = playing_ ? teal : amber;
    if (state.verdict == hrm::Verdict::Success) {
      status = "SUCCESS";
      status_color = teal;
    }
    if (state.verdict == hrm::Verdict::Fail) {
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
    auto board =
        vbox({hbox({text(" WORKROOM ") | bold | color(teal), filler(),
                    text("steps " + std::to_string(state.completed) + " ") | color(muted)}),
              separator() | color(outline),
              hbox({conveyor(false), separator() | color(outline), room(),
                    separator() | color(outline), conveyor(true)}) |
                  flex}) |
        flex;
    auto description =
        card(vbox({text(level.title) | bold | color(teal),
                   paragraph(level.definition.description) | color(ink),
                   paragraph("Goal: " + sequence(level.definition.target)) | color(amber)})) |
        size(HEIGHT, EQUAL, 8);
    auto buttons = hbox({transport_->ChildAt(0)->Render(), transport_->ChildAt(1)->Render(),
                         transport_->ChildAt(2)->Render(), transport_->ChildAt(3)->Render()});
    auto speed = hbox({text("  Speed ") | vcenter,
                       speed_slider_->Render() | vcenter | size(WIDTH, EQUAL, 22), filler()});
    const int workroom_width = width - 53;
    auto playback =
        workroom_width >= 80 ? hbox({buttons, speed | vcenter}) : vbox({buttons, speed});
    auto workroom =
        vbox({board | flex, separator() | color(outline),
              hbox({text(" " + status) | color(status_color) | bold, filler(),
                    text(std::to_string(speed_) + " steps/s ") | color(muted)}),
              playback, text(" Space play/pause · ←/→ step · R reset") | color(muted)}) |
        size(WIDTH, EQUAL, workroom_width);
    auto commands =
        vbox({description,
              hbox({text(" COMMANDS ") | bold | color(blue), filler(),
                    text(drag_ && drag_->moved && drag_->before
                             ? "INSERT " + std::string(hrm_app::opcode_name(drag_->opcode))
                         : drag_    ? "DRAGGING"
                         : linking_ ? "PICK TARGET"
                                    : "drag to insert") |
                        color(linking_ ? amber : muted)}),
              palette_first_->Render(), palette_second_->Render(), stack_->Render() | flex,
              hbox({text("Floor ") | color(muted) | vcenter,
                    card(operand_input_->Render() | color(ink), blue) | size(WIDTH, EQUAL, 7),
                    apply_->Render(), link_->Render(), erase_->Render()}),
              paragraph(" " + message_) | color(linking_ ? amber : muted),
              text(" ↑/↓ select · T link · Del remove · Tab · Q quit") | color(muted)}) |
        size(WIDTH, EQUAL, 52);
    auto view = vbox({hbox({text("  " + config_.title) | bold | color(ink), filler(),
                            level_menu_->Render() | size(WIDTH, EQUAL, 32)}),
                      separator() | color(outline),
                      hbox({workroom, separator() | color(outline), commands}) | flex}) |
                color(ink) | bgcolor(background);
    return with_drag_preview(view, width, height);
  }

  hrm_app::GameConfig config_;
  std::vector<std::unique_ptr<hrm_app::Session>> sessions_;
  std::vector<std::string> level_names_;
  int level_index_ = 0, shown_level_ = 0, speed_ = 4, preview_width_ = 0, preview_height_ = 0;
  RowId selected_ = 0, linking_ = 0;
  bool playing_ = false, redraw_ = false;
  std::string operand_text_ = "0", play_label_ = "Play [Space]";
  std::string message_ =
      "Build a program with the palette. Click a card to select its insertion point.";
  std::chrono::steady_clock::time_point next_tick_{};
  std::chrono::steady_clock::time_point next_scroll_{};
  std::optional<DragState> drag_;
  std::array<Box, 8> palette_boxes_;
  std::array<Component, 8> palette_buttons_;
  Component root_, level_menu_, palette_first_, palette_second_, operand_input_, apply_, link_,
      erase_, edit_controls_, transport_, speed_slider_;
  std::shared_ptr<CommandStack> stack_;
};

std::filesystem::path default_config(const char *executable) {
  if (std::filesystem::exists("assets/game.yaml")) {
    return "assets/game.yaml";
  }
  std::error_code error;
  auto path = std::filesystem::read_symlink("/proc/self/exe", error);
  if (error) {
    path = std::filesystem::absolute(executable);
  }
  return path.parent_path() / "assets/game.yaml";
}

} // namespace

int main(int argc, char **argv) {
  try {
    auto config = default_config(argv[0]);
    int level = 1, width = 140, height = 44;
    bool preview = false;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--help") {
        std::cout << "Usage: hrm-tui [--config game.yaml] [--level ID] [--preview] [--width N] "
                     "[--height N]\n";
        return 0;
      }
      if (argument == "--preview") {
        preview = true;
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
    auto game = hrm_app::load_config(config);
    if (std::none_of(game.levels.begin(), game.levels.end(),
                     [&](const auto &item) { return item.definition.id == level; })) {
      throw std::runtime_error("level not found in configuration: " + std::to_string(level));
    }
    Application application(std::move(game), level);
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
