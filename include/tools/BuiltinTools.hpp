#pragma once

#include <filesystem>

#include "tools/ToolRegistry.hpp"

namespace clawforge::tools {

void registerBuiltinTools(ToolRegistry& registry, const std::filesystem::path& workspaceRoot);

}  // namespace clawforge::tools
