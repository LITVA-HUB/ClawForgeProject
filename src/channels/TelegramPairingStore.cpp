#include "channels/TelegramPairingStore.hpp"

#include <algorithm>
#include <random>

#include <nlohmann/json.hpp>

#include "util/FileUtil.hpp"
#include "util/TimeUtil.hpp"

namespace clawforge::channels {

using json = nlohmann::json;

TelegramPairingStore::TelegramPairingStore(std::filesystem::path stateDir)
    : filePath_(std::move(stateDir) / "telegram" / "pairing.json") {}

bool TelegramPairingStore::init() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!util::FileUtil::ensureDir(filePath_.parent_path())) {
    return false;
  }
  loadUnsafe();
  return saveUnsafe();
}

std::vector<PairingRequest> TelegramPairingStore::listRequests() const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto copy = requests_;
  std::sort(copy.begin(), copy.end(), [](const PairingRequest& a, const PairingRequest& b) {
    return a.createdAt > b.createdAt;
  });
  return copy;
}

std::vector<PairingRequest> TelegramPairingStore::listApproved() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PairingRequest> out;
  for (const auto& req : requests_) {
    if (req.approved) out.push_back(req);
  }
  std::sort(out.begin(), out.end(), [](const PairingRequest& a, const PairingRequest& b) {
    return a.approvedAt > b.approvedAt;
  });
  return out;
}

bool TelegramPairingStore::isApproved(long long userId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& req : requests_) {
    if (req.userId == userId && req.approved) return true;
  }
  return false;
}

PairingRequest TelegramPairingStore::ensurePending(long long userId, long long chatId,
                                                   const std::string& username,
                                                   const std::string& firstName) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& req : requests_) {
    if (req.userId == userId) {
      req.chatId = chatId;
      req.username = username;
      req.firstName = firstName;
      if (!req.approved) {
        saveUnsafe();
        return req;
      }
    }
  }

  PairingRequest created;
  created.code = newCode();
  created.userId = userId;
  created.chatId = chatId;
  created.username = username;
  created.firstName = firstName;
  created.createdAt = util::TimeUtil::nowMillis();
  requests_.push_back(created);
  saveUnsafe();
  return created;
}

std::optional<PairingRequest> TelegramPairingStore::approveByCode(const std::string& code) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& req : requests_) {
    if (req.code == code) {
      req.approved = true;
      req.approvedAt = util::TimeUtil::nowMillis();
      saveUnsafe();
      return req;
    }
  }
  return std::nullopt;
}

void TelegramPairingStore::loadUnsafe() {
  requests_.clear();
  const auto content = util::FileUtil::readText(filePath_);
  if (!content.has_value() || content->empty()) return;

  const auto parsed = json::parse(*content, nullptr, false);
  if (!parsed.is_object()) return;

  const auto arr = parsed.value("requests", json::array());
  if (!arr.is_array()) return;

  for (const auto& item : arr) {
    PairingRequest req;
    req.code = item.value("code", "");
    req.userId = item.value("userId", 0LL);
    req.chatId = item.value("chatId", 0LL);
    req.username = item.value("username", "");
    req.firstName = item.value("firstName", "");
    req.createdAt = item.value("createdAt", 0LL);
    req.approved = item.value("approved", false);
    req.approvedAt = item.value("approvedAt", 0LL);
    if (!req.code.empty() && req.userId != 0) {
      requests_.push_back(std::move(req));
    }
  }
}

bool TelegramPairingStore::saveUnsafe() const {
  json arr = json::array();
  for (const auto& req : requests_) {
    arr.push_back({
        {"code", req.code},
        {"userId", req.userId},
        {"chatId", req.chatId},
        {"username", req.username},
        {"firstName", req.firstName},
        {"createdAt", req.createdAt},
        {"approved", req.approved},
        {"approvedAt", req.approvedAt},
    });
  }
  return util::FileUtil::writeText(filePath_, json{{"requests", arr}}.dump(2));
}

std::string TelegramPairingStore::newCode() {
  static constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(kAlphabet) - 2));
  std::string out;
  out.reserve(6);
  for (int i = 0; i < 6; ++i) {
    out.push_back(kAlphabet[dist(rng)]);
  }
  return out;
}

}  // namespace clawforge::channels
