#include "util/FileUtil.hpp"

#include <fstream>
#include <sstream>

namespace clawforge::util {

bool FileUtil::ensureDir(const std::filesystem::path& path) {
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    return std::filesystem::is_directory(path, ec);
  }
  return std::filesystem::create_directories(path, ec);
}

std::optional<std::string> FileUtil::readText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool FileUtil::writeText(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  if (path.has_parent_path() && !std::filesystem::exists(path.parent_path(), ec)) {
    if (!std::filesystem::create_directories(path.parent_path(), ec) && ec) {
      return false;
    }
  }

  std::ofstream out(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << content;
  return out.good();
}

bool FileUtil::replaceExact(const std::filesystem::path& path, const std::string& oldText,
                            const std::string& newText) {
  auto content = readText(path);
  if (!content.has_value()) return false;

  const std::size_t pos = content->find(oldText);
  if (pos == std::string::npos) {
    return false;
  }

  content->replace(pos, oldText.size(), newText);
  return writeText(path, *content);
}

}  // namespace clawforge::util
