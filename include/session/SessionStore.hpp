#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace clawforge::session {

struct SessionMessage {
  std::string role;
  std::string content;
  int64_t timestamp{0};
};

struct SessionInfo {
  std::string key;
  std::string sessionId;
  int64_t updatedAt{0};
};

class SessionStore {
 public:
  explicit SessionStore(std::filesystem::path stateDir);

  bool init();

  SessionInfo ensureSession(const std::string& sessionKey);
  void appendMessage(const std::string& sessionKey, const std::string& role, const std::string& content);
  std::vector<SessionMessage> loadMessages(const std::string& sessionKey, std::size_t limit = 50) const;
  std::vector<SessionInfo> listSessions() const;

 private:
  std::filesystem::path stateDir_;
  std::filesystem::path sessionsDir_;
  std::filesystem::path sessionsIndexPath_;

  mutable std::mutex mutex_;
  std::vector<SessionInfo> index_;

  void loadIndexUnsafe();
  void saveIndexUnsafe() const;
  SessionInfo* findByKeyUnsafe(const std::string& sessionKey);
  static std::string newSessionId();
};

}  // namespace clawforge::session
