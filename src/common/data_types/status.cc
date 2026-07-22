/**
 * @file status.cc
 * @brief ErrorCode 전용 std::error_category 구현.
 */
#include "common/data_types/status.h"

namespace bedrock {
namespace {

class ErrorCategoryImpl final : public std::error_category {
 public:
  /** @brief 카테고리 이름을 반환한다. */
  const char* name() const noexcept override { return "bedrock"; }

  /**
   * @brief 오류 코드 값에 대응하는 고정 메시지를 반환한다.
   * @param value ErrorCode로 캐스팅될 정수 값.
   * @return 코드에 대응하는 고정 메시지.
   */
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
