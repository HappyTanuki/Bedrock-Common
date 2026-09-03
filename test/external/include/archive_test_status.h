#pragma once
#include <string>
#include <string_view>

#include "common/data_types/status.h"

namespace test_support {

struct OwnedStatus {
  bedrock::ErrorCode code = bedrock::ErrorCode::kSuccess;
  std::size_t offset = 0;
  std::string message;

  [[nodiscard]] bool Ok() const noexcept {
    return code == bedrock::ErrorCode::kSuccess;
  }
  [[nodiscard]] bool Failed() const noexcept { return !Ok(); }
  [[nodiscard]] std::string_view Message() const noexcept { return message; }
};

inline OwnedStatus CopyStatus(const bedrock::Status& status) {
  return OwnedStatus{status.code, status.offset, std::string(status.Message())};
}

}  // namespace test_support
