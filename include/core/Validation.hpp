#pragma once

#include <string>

namespace clawforge::core {

// Returns true if s is a non-empty string that does not contain
// shell-unsafe characters. Used to validate config string fields
// before passing to subprocess or log sinks.
inline bool isSafeIdentifier(const std::string& s) {
  if (s.empty() || s.size() > 128) return false;
  for (char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) &&
        c != '-' && c != '_' && c != '.' && c != '/') {
      return false;
    }
  }
  return true;
}

// Returns true if port is in the valid range [1, 65535].
inline bool isValidPort(int port) {
  return port >= 1 && port <= 65535;
}

// Returns true if the string looks like a safe env var name
// (uppercase letters, digits, underscore, not starting with digit).
inline bool isSafeEnvVarName(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  if (std::isdigit(static_cast<unsigned char>(s[0]))) return false;
  for (char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

}  // namespace clawforge::core
