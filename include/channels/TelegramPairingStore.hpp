#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace clawforge::channels {

struct PairingRequest {
  std::string code;
  long long userId{0};
  long long chatId{0};
  std::string username;
  std::string firstName;
  int64_t createdAt{0};
  bool approved{false};
  int64_t approvedAt{0};
};

class TelegramPairingStore {
 public:
  explicit TelegramPairingStore(std::filesystem::path stateDir);

  bool init();
  std::vector<PairingRequest> listRequests() const;
  std::vector<PairingRequest> listApproved() const;
  bool isApproved(long long userId) const;
  PairingRequest ensurePending(long long userId, long long chatId, const std::string& username,
                               const std::string& firstName);
  std::optional<PairingRequest> approveByCode(const std::string& code);

 private:
  std::filesystem::path filePath_;
  mutable std::mutex mutex_;
  std::vector<PairingRequest> requests_;

  void loadUnsafe();
  bool saveUnsafe() const;
  static std::string newCode();
};

}  // namespace clawforge::channels
