#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "core/Config.hpp"

namespace clawforge::browser {

class BrowserRelay {
 public:
  explicit BrowserRelay(core::BrowserConfig config);

  nlohmann::json status() const;
  nlohmann::json open(const std::string& url) const;
  nlohmann::json snapshot(const std::string& urlHint = "") const;

 private:
  core::BrowserConfig config_;
};

}  // namespace clawforge::browser
