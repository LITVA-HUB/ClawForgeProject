#include "core/AuditTrail.hpp"

#include <fstream>

#include "util/FileUtil.hpp"
#include "util/TimeUtil.hpp"

namespace clawforge::core {

AuditTrail::AuditTrail(bool enabled, std::filesystem::path filePath)
    : enabled_(enabled), filePath_(std::move(filePath)) {}

void AuditTrail::write(const std::string& event, const nlohmann::json& data) {
  if (!enabled_) return;

  std::lock_guard<std::mutex> lock(mu_);
  if (filePath_.has_parent_path()) {
    util::FileUtil::ensureDir(filePath_.parent_path());
  }

  nlohmann::json row = {{"ts", util::TimeUtil::nowIso8601()}, {"event", event}, {"data", data}};

  std::ofstream out(filePath_, std::ios::out | std::ios::app);
  if (!out) return;
  out << row.dump() << "\n";
}

}  // namespace clawforge::core
