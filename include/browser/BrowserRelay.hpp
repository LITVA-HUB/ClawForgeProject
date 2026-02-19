#pragma once

#include <map>
#include <mutex>
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

 public:
  struct NativeRef {
    std::string role;
    std::string name;
    std::string text;
    std::string tag;
    std::string href;
    std::string signature;
    std::string formId;
    std::string inputName;
    std::string inputType;
    bool submitControl{false};
  };

  struct NativeForm {
    std::string action;
    std::string method;
  };

  struct NativeRuntime {
    std::string source;
    nlohmann::json warning;
  };

  struct NativeTarget {
    std::string targetId;
    std::string url;
    std::string title;
    std::string html;
    NativeRuntime runtime;
    std::map<std::string, NativeRef> refs;
    std::map<std::string, NativeForm> forms;
    std::map<std::string, std::string> typedValues;
  };

 private:
  core::BrowserConfig config_;
  mutable std::mutex nativeMu_;
  mutable std::map<std::string, NativeTarget> nativeTargets_;
  mutable int nativeCounter_{0};
  mutable std::string nativeLastTargetId_;

  bool useOpenClawCli() const;
  bool useNativeBackend() const;
  nlohmann::json runOpenClawBrowser(const std::vector<std::string>& args) const;

  void nativeLoadStateLocked() const;
  void nativeSaveStateLocked() const;

  nlohmann::json nativeStatus() const;
  nlohmann::json nativeOpen(const std::string& url) const;
  nlohmann::json nativeNavigate(const std::string& url, const std::string& targetId) const;
  nlohmann::json nativeSnapshot(const std::string& urlHint, const std::string& targetId) const;
  nlohmann::json nativeClick(const std::string& ref, const std::string& targetId, bool doubleClick) const;
  nlohmann::json nativeType(const std::string& ref, const std::string& text,
                            const std::string& targetId, bool submit, bool slowly) const;
  nlohmann::json nativeScreenshot(const std::string& targetId, bool fullPage,
                                  const std::string& type) const;
  nlohmann::json nativeSubmitFormLocked(NativeTarget& t, const std::string& formId,
                                        const std::string& triggerRef, const std::string& targetId,
                                        const std::string& trigger) const;
};

}  // namespace clawforge::browser
