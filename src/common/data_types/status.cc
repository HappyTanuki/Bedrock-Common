#include "common/data_types/status.h"

namespace bedrock {
namespace {

class ErrorCategoryImpl final : public std::error_category {
 public:
  const char* name() const noexcept override { return "bedrock"; }

  std::string message(int value) const override {
    switch (static_cast<ErrorCode>(value)) {
      case ErrorCode::kSuccess:
        return "Success.";
      case ErrorCode::kNoENT:
        return "No Entry.";
      case ErrorCode::kError:
        return "Unhandled Error.";
    }
    return "Unknown Error.";
  }
};

}  // namespace

const std::error_category& ErrorCategory() noexcept {
  static const ErrorCategoryImpl instance;  // 스레드 안전 초기화
  return instance;
}

std::error_code make_error_code(ErrorCode e) noexcept {
  return {static_cast<int>(e), ErrorCategory()};
}

}  // namespace bedrock
