#pragma once

#include <string>

namespace clawforge::util {

struct CommandResult {
  int exitCode{0};
  std::string output;
};

class Shell {
 public:
  static CommandResult run(const std::string& command);
  static std::string quote(const std::string& value);
};

}  // namespace clawforge::util
