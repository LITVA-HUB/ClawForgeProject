#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace clawforge::util {

class FileUtil {
 public:
  static bool ensureDir(const std::filesystem::path& path);
  static std::optional<std::string> readText(const std::filesystem::path& path);
  static bool writeText(const std::filesystem::path& path, const std::string& content);
  static bool replaceExact(const std::filesystem::path& path, const std::string& oldText,
                           const std::string& newText);
};

}  // namespace clawforge::util
