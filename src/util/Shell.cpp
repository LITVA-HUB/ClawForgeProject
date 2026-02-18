#include "util/Shell.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <sys/wait.h>

namespace clawforge::util {

CommandResult Shell::run(const std::string& command) {
  CommandResult result;
  std::array<char, 4096> buffer{};

  const std::string finalCommand = command + " 2>&1";
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(finalCommand.c_str(), "r"), pclose);

  if (!pipe) {
    result.exitCode = -1;
    result.output = "Failed to execute command";
    return result;
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
    result.output += buffer.data();
  }

  const int status = pclose(pipe.release());
  if (WIFEXITED(status)) {
    result.exitCode = WEXITSTATUS(status);
  } else {
    result.exitCode = -1;
  }

  return result;
}

std::string Shell::quote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out += "'";
  return out;
}

}  // namespace clawforge::util
