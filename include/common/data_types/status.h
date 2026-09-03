/**
 * @file status.h
 * @brief ABI-stable status value with a borrowed diagnostic message.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace bedrock {

enum class ErrorCode : std::uint32_t {
  kSuccess = 0,
  kNoENT = 1,
  kNullInput = 2,
  kCorrupted = 3,
  kNotReady = 4,
  kAlreadyConsumed = 5,
  kError = 0xFFFFFFFF,
};

struct Status {
  ErrorCode code = ErrorCode::kSuccess;
  std::size_t offset = 0;
  std::string_view message;

  constexpr Status() noexcept = default;
  constexpr explicit(false) Status(ErrorCode error) noexcept
      : code(error), message(DefaultMessage(error)) {}
  constexpr Status(ErrorCode error, std::string_view detail,
                   std::size_t error_offset = 0) noexcept
      : code(error), offset(error_offset), message(detail) {}
  constexpr explicit(false) Status(int error) noexcept
      : Status(static_cast<ErrorCode>(static_cast<std::uint32_t>(error))) {}

  [[nodiscard]] constexpr bool Ok() const noexcept {
    return code == ErrorCode::kSuccess;
  }
  [[nodiscard]] constexpr bool Failed() const noexcept { return !Ok(); }
  [[nodiscard]] constexpr std::string_view Message() const noexcept {
    return message;
  }

 private:
  [[nodiscard]] static constexpr std::string_view DefaultMessage(
      ErrorCode error) noexcept {
    switch (error) {
      case ErrorCode::kSuccess:
        return "success";
      case ErrorCode::kNoENT:
        return "no such entry";
      case ErrorCode::kNullInput:
        return "null input";
      case ErrorCode::kCorrupted:
        return "corrupted data";
      case ErrorCode::kNotReady:
        return "archive deserializer is not initialized";
      case ErrorCode::kAlreadyConsumed:
        return "archive operation is already consumed";
      case ErrorCode::kError:
        return "error";
    }
    return "error";
  }
};

}  // namespace bedrock
