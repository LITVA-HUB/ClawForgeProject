#include "tools/BuiltinTools.hpp"

#include <algorithm>
#include <filesystem>

#include "util/FileUtil.hpp"
#include "util/Shell.hpp"

namespace clawforge::tools {

namespace {

std::filesystem::path resolvePath(const std::filesystem::path& root, const std::string& rawPath) {
  std::filesystem::path p(rawPath);
  if (p.is_relative()) {
    p = root / p;
  }
  return p.lexically_normal();
}

std::string trimOutput(const std::string& text, std::size_t maxChars = 50000) {
  if (text.size() <= maxChars) return text;
  return text.substr(0, maxChars) + "\n...[truncated]";
}

}  // namespace

void registerBuiltinTools(ToolRegistry& registry, const std::filesystem::path& workspaceRoot) {
  registry.registerTool("read", [workspaceRoot](const nlohmann::json& args) {
    const std::string rawPath = args.value("path", "");
    if (rawPath.empty()) {
      return nlohmann::json{{"ok", false}, {"error", "path is required"}};
    }

    const auto path = resolvePath(workspaceRoot, rawPath);
    auto content = util::FileUtil::readText(path);
    if (!content.has_value()) {
      return nlohmann::json{{"ok", false}, {"error", "Cannot read file"}, {"path", path.string()}};
    }

    return nlohmann::json{{"ok", true}, {"path", path.string()}, {"content", trimOutput(*content)}};
  });

  registry.registerTool("write", [workspaceRoot](const nlohmann::json& args) {
    const std::string rawPath = args.value("path", "");
    const std::string content = args.value("content", "");

    if (rawPath.empty()) {
      return nlohmann::json{{"ok", false}, {"error", "path is required"}};
    }

    const auto path = resolvePath(workspaceRoot, rawPath);
    const bool ok = util::FileUtil::writeText(path, content);
    return nlohmann::json{{"ok", ok},
                          {"path", path.string()},
                          {"bytes", static_cast<int64_t>(content.size())},
                          {"error", ok ? "" : "Cannot write file"}};
  });

  registry.registerTool("edit", [workspaceRoot](const nlohmann::json& args) {
    const std::string rawPath = args.value("path", "");
    const std::string oldText = args.value("oldText", "");
    const std::string newText = args.value("newText", "");

    if (rawPath.empty()) {
      return nlohmann::json{{"ok", false}, {"error", "path is required"}};
    }

    const auto path = resolvePath(workspaceRoot, rawPath);
    const bool ok = util::FileUtil::replaceExact(path, oldText, newText);
    return nlohmann::json{{"ok", ok},
                          {"path", path.string()},
                          {"error", ok ? "" : "Exact match not found or file not readable"}};
  });

  registry.registerTool("exec", [workspaceRoot](const nlohmann::json& args) {
    const std::string cmd = args.value("command", "");
    const std::string workdir = args.value("workdir", workspaceRoot.string());

    if (cmd.empty()) {
      return nlohmann::json{{"ok", false}, {"error", "command is required"}};
    }

    const std::string finalCmd = "cd " + util::Shell::quote(workdir) + " && " + cmd;
    const auto res = util::Shell::run(finalCmd);

    return nlohmann::json{{"ok", res.exitCode == 0},
                          {"exitCode", res.exitCode},
                          {"output", trimOutput(res.output)}};
  });
}

}  // namespace clawforge::tools
