#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Config.hpp"

namespace clawforge::browser {

class BrowserRelay {
 public:
  explicit BrowserRelay(core::BrowserConfig config);

  nlohmann::json status() const;
  nlohmann::json open(const std::string& url) const;
  nlohmann::json snapshot(const std::string& urlHint = "", const std::string& targetId = "") const;

  nlohmann::json navigate(const std::string& url, const std::string& targetId = "") const;
  nlohmann::json click(const std::string& ref, const std::string& targetId = "", bool doubleClick = false) const;
  nlohmann::json type(const std::string& ref, const std::string& text,
                      const std::string& targetId = "", bool submit = false,
                      bool slowly = false) const;
  nlohmann::json screenshot(const std::string& targetId = "", bool fullPage = false,
                            const std::string& type = "png") const;

 private:
  core::BrowserConfig config_;

  bool useOpenClawCli() const;
  nlohmann::json runOpenClawBrowser(const std::vector<std::string>& args) const;
};

}  // namespace clawforge::browser
