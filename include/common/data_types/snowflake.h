/**
 * @file snowflake.h
 * @brief Snowflake ID 데이터 타입 정의.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace bedrock {
/**
 * @brief Snowflake ID.
 * @see https://en.wikipedia.org/wiki/Snowflake_ID
 */
struct Snowflake {
  /** @brief 시퀀스 번호(12비트). */
  std::uint64_t sequence : 12;
  /** @brief 인스턴스 식별자 중 스레드 ID(7비트). */
  std::uint64_t thread_id : 7;
  /** @brief 인스턴스 식별자 중 머신 ID(3비트). */
  std::uint64_t machine_id : 3;
  /** @brief 타임스탬프(41비트). */
  std::uint64_t timestamp : 41;
  /** @brief 부호 비트(항상 0). */
  std::uint64_t sign : 1;

  /**
   * @brief 64비트 값으로부터 각 필드를 분해해 Snowflake를 생성한다.
   * @param v 필드들을 비트 단위로 담고 있는 64비트 값.
   */
  Snowflake(std::uint64_t v)
      : sequence(v & 0xFFF),                   // 12비트
        thread_id((v >> 12) & 0x7F),           // 7비트
        machine_id((v >> 19) & 0x7),           // 3비트
        timestamp((v >> 22) & 0x1FFFFFFFFFF),  // 41비트
        sign((v >> 63) & 0x1) {}

  /**
   * @brief 각 필드를 비트로 합쳐 64비트 정수로 변환한다.
   */
  explicit operator std::uint64_t() const {
    return static_cast<std::uint64_t>(sequence) |
           (static_cast<std::uint64_t>(thread_id) << 12) |
           (static_cast<std::uint64_t>(machine_id) << 19) |
           (static_cast<std::uint64_t>(timestamp) << 22) |
           (static_cast<std::uint64_t>(sign) << 63);
  }

  /**
   * @brief 64비트 값 기준으로 동등 비교한다.
   */
  bool operator==(const Snowflake& other) const {
    return static_cast<std::uint64_t>(*this) ==
           static_cast<std::uint64_t>(other);
  }
  /**
   * @brief 64비트 값 기준으로 대소 비교한다.
   */
  bool operator<(const Snowflake& other) const {
    return static_cast<std::uint64_t>(*this) <
           static_cast<std::uint64_t>(other);
  }

  /**
   * @brief 현재 시각, 머신 ID, 스레드 ID, 시퀀스를 조합해 새로운
   * Snowflake ID를 생성한다.
   * @param machine_id 프로세스(머신)를 식별하는 값(3비트만 사용).
   * @return 새로 생성된 Snowflake.
   */
  static Snowflake Generate(std::uint16_t machine_id);
};

}  // namespace bedrock

namespace std {
/**
 * @brief bedrock::Snowflake에 대한 std::hash 특수화.
 */
template <>
struct hash<bedrock::Snowflake> {
  /**
   * @brief Snowflake를 64비트 정수로 변환해 해시값을 계산한다.
   */
  size_t operator()(const bedrock::Snowflake& s) const noexcept {
    return std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(s));
  }
};
}  // namespace std
