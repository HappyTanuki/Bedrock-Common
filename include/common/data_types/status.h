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
  kSnowflakeMissingMachineId = 0x101,
  kSnowflakeInvalidMachineId = 0x102,
  kSnowflakeInvalidStatePath = 0x103,
  kSnowflakeEnvironmentReadFailed = 0x104,
  kSnowflakeConfigurationReadFailed = 0x105,
  kSnowflakeRegistryIoFailed = 0x106,
  kSnowflakeRegistryCorrupt = 0x107,
  kSnowflakeRegistryIncompatible = 0x108,
  kSnowflakeMachineIdMismatch = 0x109,
  kSnowflakeLockFailed = 0x10A,
  kSnowflakeSlotsExhausted = 0x10B,
  kSnowflakeClockBeforeEpoch = 0x10C,
  kSnowflakeClockOutOfRange = 0x10D,
  kSnowflakeClockRollback = 0x10E,
  kSnowflakeSequenceExhausted = 0x10F,
  kSnowflakeUnsupportedPlatform = 0x110,
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
      case ErrorCode::kSnowflakeMissingMachineId:
        return "snowflake machine id is missing";
      case ErrorCode::kSnowflakeInvalidMachineId:
        return "snowflake machine id is invalid";
      case ErrorCode::kSnowflakeInvalidStatePath:
        return "snowflake state path is invalid";
      case ErrorCode::kSnowflakeEnvironmentReadFailed:
        return "failed to read snowflake environment";
      case ErrorCode::kSnowflakeConfigurationReadFailed:
        return "failed to read snowflake configuration";
      case ErrorCode::kSnowflakeRegistryIoFailed:
        return "snowflake registry I/O failed";
      case ErrorCode::kSnowflakeRegistryCorrupt:
        return "snowflake registry is corrupt";
      case ErrorCode::kSnowflakeRegistryIncompatible:
        return "snowflake registry is incompatible";
      case ErrorCode::kSnowflakeMachineIdMismatch:
        return "snowflake machine id mismatch";
      case ErrorCode::kSnowflakeLockFailed:
        return "snowflake registry lock failed";
      case ErrorCode::kSnowflakeSlotsExhausted:
        return "snowflake machine id slots exhausted";
      case ErrorCode::kSnowflakeClockBeforeEpoch:
        return "snowflake clock is before epoch";
      case ErrorCode::kSnowflakeClockOutOfRange:
        return "snowflake clock is out of range";
      case ErrorCode::kSnowflakeClockRollback:
        return "snowflake clock moved backwards";
      case ErrorCode::kSnowflakeSequenceExhausted:
        return "snowflake sequence exhausted";
      case ErrorCode::kSnowflakeUnsupportedPlatform:
        return "snowflake platform is unsupported";
      case ErrorCode::kError:
        return "error";
    }
    return "error";
  }
};

}  // namespace bedrock
