/**
 * @file snowflake.h
 * @brief Snowflake ID data type and generation API.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace bedrock {

enum class SnowflakeError : std::int64_t {
  kMissingMachineId = -1, kInvalidMachineId = -2, kInvalidStatePath = -3,
  kEnvironmentReadFailed = -4, kConfigurationReadFailed = -5,
  kRegistryIoFailed = -6, kRegistryCorrupt = -7,
  kRegistryIncompatible = -8, kMachineIdMismatch = -9,
  kLockFailed = -10, kSlotsExhausted = -11,
  kClockBeforeEpoch = -12, kClockOutOfRange = -13,
  kClockRollback = -14, kSequenceExhausted = -15,
  kUnsupportedPlatform = -16,
};

struct Snowflake {
  std::uint64_t sequence : 12;
  /** Server-wide live issuer slot, not an OS thread number. */
  std::uint64_t thread_id : 7;
  std::uint64_t machine_id : 3;
  std::uint64_t timestamp : 41;
  std::uint64_t sign : 1 = 0;
  explicit Snowflake(std::uint64_t value)
      : sequence(value & 0xFFF), thread_id((value >> 12) & 0x7F),
        machine_id((value >> 19) & 0x7), timestamp((value >> 22) & 0x1FFFFFFFFFF) {
    if ((value & (std::uint64_t{1} << 63)) != 0) sign = 1;
  }
  explicit operator std::uint64_t() const {
    return static_cast<std::uint64_t>(sequence) |
           (static_cast<std::uint64_t>(thread_id) << 12) |
           (static_cast<std::uint64_t>(machine_id) << 19) |
           (static_cast<std::uint64_t>(timestamp) << 22) |
           (static_cast<std::uint64_t>(sign) << 63);
  }
  bool operator==(const Snowflake& other) const {
    return static_cast<std::uint64_t>(*this) == static_cast<std::uint64_t>(other);
  }
  bool operator<(const Snowflake& other) const {
    return static_cast<std::uint64_t>(*this) < static_cast<std::uint64_t>(other);
  }
  [[nodiscard]] static std::int64_t Generate() noexcept;
};

}

namespace std {

template <> struct hash<bedrock::Snowflake> {
  size_t operator()(const bedrock::Snowflake& snowflake) const noexcept {
    return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(snowflake));
  }
};

}
