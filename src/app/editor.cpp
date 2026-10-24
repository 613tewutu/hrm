#include "hrm_app/editor.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace hrm_app {

const char *opcode_name(hrm::Opcode opcode) {
  const char *names[] =
    {"inbox", "outbox", "add", "sub", "copyto", "copyfrom", "jump", "jumpifzero", "jumpifnegative"};
  return names[static_cast<std::size_t>(opcode)];
}

bool is_jump(hrm::Opcode opcode) {
  return opcode == hrm::Opcode::Jump || opcode == hrm::Opcode::JumpIfZero ||
    opcode == hrm::Opcode::JumpIfNegative;
}

bool uses_floor(hrm::Opcode opcode) {
  return opcode == hrm::Opcode::Add || opcode == hrm::Opcode::Sub ||
    opcode == hrm::Opcode::CopyTo || opcode == hrm::Opcode::CopyFrom;
}

Editor::Editor(const std::vector<std::string> &source) {
  std::vector<RowId> original;
  std::vector<hrm::Value> targets;
  for (const auto &line : source) {
    const auto decoded = hrm::decode_line(line);
    if (decoded.error != hrm::ErrorCode::None) {
      throw std::invalid_argument("invalid editor command: " + line);
    }
    CommandRow row;
    row.id = next_id_++;
    row.landing = false;
    row.opcode = decoded.instruction.opcode;
    row.operand = std::to_string(decoded.instruction.operand);
    rows_.push_back(row);
    original.push_back(row.id);
    targets.push_back(decoded.instruction.operand);
  }
  CommandRow last;
  last.id = next_id_++;
  rows_.push_back(last);
  for (std::size_t i = 0; i < original.size(); ++i) {
    if (is_jump(find(original[i])->opcode) && targets[i] >= 1 &&
      static_cast<std::uintmax_t>(targets[i]) <= original.size()) {
      set_target(original[i], original[static_cast<std::size_t>(targets[i] - 1)]);
    }
  }
}

const std::vector<CommandRow> &Editor::rows() const {
  return rows_;
}

const CommandRow *Editor::find(RowId id) const {
  const auto it = std::find_if(rows_.begin(), rows_.end(), [id](const auto &row) {
    return row.id == id;
  });
  return it == rows_.end() ? nullptr : &*it;
}

RowId Editor::insert_command(RowId after, hrm::Opcode opcode, std::string operand) {
  const auto position = std::find_if(rows_.begin(), rows_.end(), [after](const auto &row) {
    return row.id == after;
  });
  const auto index = position == rows_.end()
    ? rows_.size() - 1
    : static_cast<std::size_t>(position - rows_.begin()) + 1;
  return insert_at(index, opcode, std::move(operand));
}

RowId Editor::insert_before(RowId before, hrm::Opcode opcode, std::string operand) {
  if (before == 0) {
    return insert_command(rows_.back().id, opcode, std::move(operand));
  }
  const auto position = std::find_if(rows_.begin(), rows_.end(), [before](const auto &row) {
    return row.id == before;
  });
  if (position == rows_.end()) {
    return 0;
  }
  return insert_at(static_cast<std::size_t>(position - rows_.begin()), opcode, std::move(operand));
}

RowId Editor::insert_at(std::size_t index, hrm::Opcode opcode, std::string operand) {
  CommandRow row;
  row.id = next_id_++;
  row.landing = false;
  row.opcode = opcode;
  row.operand = std::move(operand);
  const auto id = row.id;
  rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(index), row);
  tidy_landings();
  return id;
}

bool Editor::move_before(RowId source, RowId before) {
  const auto original = std::find_if(rows_.begin(), rows_.end(), [source](const auto &row) {
    return row.id == source;
  });
  if (original == rows_.end() || source == before) {
    return false;
  }
  if (original->landing && std::none_of(rows_.begin(), rows_.end(), [source](const auto &row) {
        return !row.landing && row.target == source;
      })) {
    return false;
  }
  const bool terminal_linked = std::any_of(rows_.begin(), rows_.end(), [&](const auto &row) {
    return !row.landing && is_jump(row.opcode) && row.target == rows_.back().id;
  });
  const auto destination = before == 0
    ? (terminal_linked ? rows_.end() : rows_.end() - 1)
    : std::find_if(rows_.begin(), rows_.end(), [before](const auto &row) {
        return row.id == before;
      });
  if (before != 0 && destination == rows_.end()) {
    return false;
  }
  auto index = static_cast<std::size_t>(destination - rows_.begin());
  const auto source_index = static_cast<std::size_t>(original - rows_.begin());
  if (source_index < index) {
    --index;
  }
  if (source_index == index) {
    return false;
  }
  const auto row = *original;
  rows_.erase(original);
  rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(index), row);
  tidy_landings();
  return true;
}

void Editor::tidy_landings() {
  if (!rows_.back().landing) {
    CommandRow last;
    last.id = next_id_++;
    rows_.push_back(last);
  }
  const auto last = rows_.back().id;
  std::set<RowId> referenced;
  for (const auto &row : rows_) {
    if (!row.landing && is_jump(row.opcode) && row.target) {
      referenced.insert(row.target);
    }
  }
  rows_.erase(
    std::remove_if(
      rows_.begin(),
      rows_.end(),
      [&](const auto &row) {
        return row.landing && row.id != last && !referenced.count(row.id);
      }
    ),
    rows_.end()
  );
}

RowId Editor::insert_landing(RowId before) {
  auto position = std::find_if(rows_.begin(), rows_.end(), [before](const auto &row) {
    return row.id == before;
  });
  CommandRow row;
  row.id = next_id_++;
  const auto id = row.id;
  rows_.insert(position, row);
  return id;
}

void Editor::erase(RowId id) {
  const auto position = std::find_if(rows_.begin(), rows_.end(), [id](const auto &row) {
    return row.id == id;
  });
  if (position == rows_.end() || (position + 1 == rows_.end() && position->landing)) {
    return;
  }
  rows_.erase(position);
  for (auto &row : rows_) {
    if (row.target == id) {
      row.target = 0;
    }
  }
  tidy_landings();
}

void Editor::set_operand(RowId id, std::string operand) {
  for (auto &row : rows_) {
    if (row.id == id && !row.landing && uses_floor(row.opcode)) {
      row.operand = std::move(operand);
      return;
    }
  }
}

RowId Editor::set_target(RowId jump, RowId destination) {
  const auto *source = find(jump);
  if (!source || source->landing || !is_jump(source->opcode) || !find(destination)) {
    return 0;
  }
  const RowId previous = source->target;
  if (previous == destination) {
    return previous;
  }
  const RowId landing = previous && find(previous) ? previous : insert_landing(destination);
  if (landing == previous) {
    move_before(landing, destination);
  }
  for (auto &row : rows_) {
    if (row.id == jump) {
      row.target = landing;
    }
  }
  tidy_landings();
  return landing;
}

CompiledProgram Editor::compile() const {
  std::map<RowId, std::size_t> destinations;
  std::size_t number = 0;
  for (const auto &row : rows_) {
    if (!row.landing) {
      destinations[row.id] = ++number;
    }
  }
  std::size_t next = 0;
  for (auto it = rows_.rbegin(); it != rows_.rend(); ++it) {
    if (it->landing) {
      destinations[it->id] = next;
    } else {
      next = destinations[it->id];
    }
  }
  CompiledProgram result;
  for (const auto &row : rows_) {
    if (row.landing) {
      continue;
    }
    hrm::SourceInstruction instruction;
    instruction.text = opcode_name(row.opcode);
    if (is_jump(row.opcode)) {
      const auto destination = destinations.find(row.target);
      instruction.text +=
        " " + std::to_string(destination == destinations.end() ? 0 : destination->second);
    } else if (uses_floor(row.opcode)) {
      instruction.text += " " + row.operand;
    }
    result.program.instructions.push_back(std::move(instruction));
    result.instruction_rows.push_back(row.id);
  }
  return result;
}

Session::Session(hrm::LevelDefinition level, Editor editor)
  : level_(std::move(level)),
    editor_(std::move(editor)) {
  reset();
}

const hrm::Machine &Session::machine() const {
  return *machine_;
}
const Editor &Session::editor() const {
  return editor_;
}
Editor &Session::edit() {
  return editor_;
}

void Session::reset() {
  compiled_ = editor_.compile();
  machine_ = std::make_unique<hrm::Machine>(level_, compiled_.program);
}

hrm::StepResult Session::next() {
  return machine_->step();
}

void Session::previous() {
  const auto attempted = machine_->state().attempted;
  if (attempted == 0) {
    return;
  }
  machine_->reset();
  for (std::uint64_t i = 0; i < attempted - 1; ++i) {
    machine_->step();
  }
}

RowId Session::current_row() const {
  const auto &state = machine_->state();
  if (compiled_.instruction_rows.empty()) {
    return 0;
  }
  const auto index =
    state.pc < compiled_.instruction_rows.size() ? state.pc : compiled_.instruction_rows.size() - 1;
  return compiled_.instruction_rows[index];
}

} // namespace hrm_app
