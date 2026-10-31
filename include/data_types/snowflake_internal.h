#pragma once
#include <cstdint>

#include "common/data_types/snowflake.h"
#include "platform/os_io.h"

namespace bedrock::internal::snowflake {
struct IssuerState {
  std::int64_t observed_ms = 0;
  std::int64_t issued_ms = 0;
  std::uint16_t sequence = 0;
  bool observed = false;
  bool issued = false;
};
std::int64_t IssueAt(IssuerState&, std::uint8_t machine,
                     std::uint8_t slot, std::int64_t unix_ms) noexcept;
// Testable acquisition fence: failed samples leave the existing slot/state owned;
// a retry must sample again and cannot issue until time advances past initial_ms.
using ClockSample = std::int64_t (*)(void*);
std::int64_t CompleteAcquisitionFence(IssuerState&, std::int64_t initial_ms,
                                      ClockSample, void*) noexcept;
// Maps a failed range lock from either registration path: the waiting
// registration-gate lock and a slot lock that is not merely busy (the slot scan
// skips kBusy without mapping it). kUnsupported becomes kUnsupportedPlatform;
// every other failure becomes kLockFailed. A successful status is outside the
// input contract. Registry open/read/write failures are mapped separately.
SnowflakeError MapLockFailure(os::IoStatus status) noexcept;
}
