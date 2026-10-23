#include "hrm/core.hpp"

#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace {

bool read_number(std::istream &input, hrm::Value &value) {
  std::string token;
  if (!(input >> token)) {
    return false;
  }
  const hrm::IntegerResult result = hrm::parse_integer(token);
  value = result.value;
  return result.error == hrm::ErrorCode::None;
}

int invalid_input() {
  // The PDF defines no OJ verdict for a malformed input envelope.
  std::cerr << "Invalid OJ input\n";
  return 1;
}

} // namespace

int main() {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  hrm::Value level_id = 0;
  hrm::Value count = 0;
  if (!read_number(std::cin, level_id) || level_id < 1 || level_id > 3 ||
      !read_number(std::cin, count) || count < 0 ||
      static_cast<std::uintmax_t>(count) > std::numeric_limits<std::size_t>::max()) {
    return invalid_input();
  }
  std::string remainder;
  std::getline(std::cin, remainder);
  if (remainder.find_first_not_of(" \t\r") != std::string::npos) {
    return invalid_input();
  }

  hrm::Program program;
  for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
    hrm::SourceInstruction instruction;
    if (!std::getline(std::cin, instruction.text)) {
      return invalid_input();
    }
    // OJ level/count whitespace is flexible, so no physical line is claimed here.
    program.instructions.push_back(std::move(instruction));
  }
  hrm::LevelDefinition level;
  hrm::load_builtin_level(static_cast<int>(level_id), level);
  hrm::Machine machine(std::move(level), std::move(program));
  while (machine.state().status == hrm::ExecutionStatus::Running) {
    machine.step();
  }
  switch (machine.state().verdict) {
  case hrm::Verdict::Success:
    std::cout << "Success\n";
    break;
  case hrm::Verdict::Fail:
    std::cout << "Fail\n";
    break;
  case hrm::Verdict::Error:
    std::cout << "Error on instruction " << machine.state().diagnostic.instruction << '\n';
    break;
  case hrm::Verdict::Pending:
    return 1;
  }
}
