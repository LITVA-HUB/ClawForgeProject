#pragma once

#include <filesystem>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace clawforge::core {

class AuditTrail {
 public:
  AuditTrail(bool enabled, std::filesystem::path filePath);
  void write(const std::string& event, const nlohmann::json& data);

 private:
  bool enabled_{false};
  std::filesystem::path filePath_;
  std::mutex mu_;
};

}  // namespace clawforge::core
