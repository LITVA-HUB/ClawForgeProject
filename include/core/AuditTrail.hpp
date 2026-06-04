#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace clawforge::core {

class AuditTrail {
 public:
  AuditTrail(bool enabled, std::filesystem::path filePath);
  void write(const std::string& event, const nlohmann::json& data);

  // Flushes pending writes to disk. Called on clean shutdown.
  void flush();

 private:
  bool enabled_{false};
  std::filesystem::path filePath_;
  std::ofstream out_;
  std::mutex mu_;
};

}  // namespace clawforge::core
