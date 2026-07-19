#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace bedrock {
// Snowflake id
// https://en.wikipedia.org/wiki/Snowflake_ID
struct Snowflake {
  // sequence
  std::uint64_t sequence : 12;
  // instance
  std::uint64_t thread_id : 7;
  std::uint64_t machine_id : 3;
  // timestamp
  std::uint64_t timestamp : 41;
  // Always 0
  std::uint64_t sign : 1;

  Snowflake(std::uint64_t v)
      : sequence(v & 0xFFF),                   // 12비트
        thread_id((v >> 12) & 0x7F),           // 7비트
        machine_id((v >> 19) & 0x7),           // 3비트
        timestamp((v >> 22) & 0x1FFFFFFFFFF),  // 41비트
        sign((v >> 63) & 0x1) {}

  explicit operator std::uint64_t() const {
    return static_cast<std::uint64_t>(sequence) |
           (static_cast<std::uint64_t>(thread_id) << 12) |
           (static_cast<std::uint64_t>(machine_id) << 19) |
           (static_cast<std::uint64_t>(timestamp) << 22) |
           (static_cast<std::uint64_t>(sign) << 63);
  }

  bool operator==(const Snowflake& other) const {
    return static_cast<std::uint64_t>(*this) ==
           static_cast<std::uint64_t>(other);
  }
  bool operator<(const Snowflake& other) const {
    return static_cast<std::uint64_t>(*this) <
           static_cast<std::uint64_t>(other);
  }

  static Snowflake Generate(std::uint16_t machine_id);
};

}  // namespace bedrock

namespace std {
template <>
struct hash<bedrock::Snowflake> {
  size_t operator()(const bedrock::Snowflake& s) const noexcept {
    return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(s));
  }
};
}  // namespace std
