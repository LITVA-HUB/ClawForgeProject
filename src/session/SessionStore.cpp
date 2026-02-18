#include "session/SessionStore.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

#include "util/FileUtil.hpp"
#include "util/TimeUtil.hpp"

namespace clawforge::session {

using json = nlohmann::json;

SessionStore::SessionStore(std::filesystem::path stateDir)
    : stateDir_(std::move(stateDir)),
      sessionsDir_(stateDir_ / "sessions"),
      sessionsIndexPath_(sessionsDir_ / "sessions.json") {}

bool SessionStore::init() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!util::FileUtil::ensureDir(sessionsDir_)) {
    return false;
  }

  loadIndexUnsafe();
  return true;
}

SessionInfo SessionStore::ensureSession(const std::string& sessionKey) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (auto* found = findByKeyUnsafe(sessionKey); found != nullptr) {
    return *found;
  }

  SessionInfo info;
  info.key = sessionKey;
  info.sessionId = newSessionId();
  info.updatedAt = util::TimeUtil::nowMillis();
  index_.push_back(info);
  saveIndexUnsafe();
  return info;
}

void SessionStore::appendMessage(const std::string& sessionKey, const std::string& role,
                                 const std::string& content) {
  std::lock_guard<std::mutex> lock(mutex_);

  SessionInfo* info = findByKeyUnsafe(sessionKey);
  if (!info) {
    SessionInfo created;
    created.key = sessionKey;
    created.sessionId = newSessionId();
    created.updatedAt = util::TimeUtil::nowMillis();
    index_.push_back(created);
    info = &index_.back();
  }

  info->updatedAt = util::TimeUtil::nowMillis();

  json line = {
      {"role", role},
      {"content", content},
      {"timestamp", info->updatedAt},
  };

  const auto transcriptPath = sessionsDir_ / (info->sessionId + ".jsonl");
  std::ofstream out(transcriptPath, std::ios::out | std::ios::app);
  out << line.dump() << '\n';

  saveIndexUnsafe();
}

std::vector<SessionMessage> SessionStore::loadMessages(const std::string& sessionKey,
                                                       std::size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);

  const SessionInfo* info = nullptr;
  for (const auto& entry : index_) {
    if (entry.key == sessionKey) {
      info = &entry;
      break;
    }
  }
  if (!info) return {};

  const auto transcriptPath = sessionsDir_ / (info->sessionId + ".jsonl");
  std::ifstream in(transcriptPath);
  if (!in) return {};

  std::vector<SessionMessage> messages;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    auto parsed = json::parse(line, nullptr, false);
    if (parsed.is_discarded()) continue;

    SessionMessage msg;
    msg.role = parsed.value("role", "assistant");
    msg.content = parsed.value("content", "");
    msg.timestamp = parsed.value("timestamp", 0LL);
    messages.push_back(std::move(msg));
  }

  if (messages.size() > limit) {
    return std::vector<SessionMessage>(messages.end() - static_cast<long>(limit), messages.end());
  }

  return messages;
}

std::vector<SessionInfo> SessionStore::listSessions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto copy = index_;
  std::sort(copy.begin(), copy.end(), [](const SessionInfo& a, const SessionInfo& b) {
    return a.updatedAt > b.updatedAt;
  });
  return copy;
}

void SessionStore::loadIndexUnsafe() {
  index_.clear();
  auto content = util::FileUtil::readText(sessionsIndexPath_);
  if (!content.has_value() || content->empty()) {
    return;
  }

  auto parsed = json::parse(*content, nullptr, false);
  if (!parsed.is_array()) {
    return;
  }

  for (const auto& item : parsed) {
    SessionInfo info;
    info.key = item.value("key", "");
    info.sessionId = item.value("sessionId", "");
    info.updatedAt = item.value("updatedAt", 0LL);
    if (!info.key.empty() && !info.sessionId.empty()) {
      index_.push_back(std::move(info));
    }
  }
}

void SessionStore::saveIndexUnsafe() const {
  json arr = json::array();
  for (const auto& s : index_) {
    arr.push_back({
        {"key", s.key},
        {"sessionId", s.sessionId},
        {"updatedAt", s.updatedAt},
    });
  }
  util::FileUtil::writeText(sessionsIndexPath_, arr.dump(2));
}

SessionInfo* SessionStore::findByKeyUnsafe(const std::string& sessionKey) {
  for (auto& s : index_) {
    if (s.key == sessionKey) {
      return &s;
    }
  }
  return nullptr;
}

std::string SessionStore::newSessionId() {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  std::uniform_int_distribution<uint64_t> dist;

  const uint64_t a = dist(rng);
  const uint64_t b = dist(rng);
  std::ostringstream ss;
  ss << std::hex << a << b;
  return ss.str();
}

}  // namespace clawforge::session
