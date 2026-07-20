#include "common/data_types/snowflake.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace bedrock {

// std::thread::id를 std::hash로 해시하면 값이 너무 커서 Snowflake의 7bit
// thread_id에 그대로 넣을 수 없고, 스레드가 재사용될 때 id도 바뀐다. 그래서
// 각 스레드가 처음 id를 요청할 때 전역 카운터에서 순번을 하나 받아
// thread_local에 캐시한다. 같은 스레드는 항상 같은 번호를 돌려받고, 살아 있는
// 동안 다른 스레드와 겹치지 않는다. 다만 프로세스 수명 동안 128개를 초과해
// 스레드가 생성되면 번호가 래핑되어 재사용된다(timestamp+sequence로 보완).
static constexpr std::uint16_t kThreadIdMask = 0x7F;  // 7bit: 0~127

inline static std::uint16_t GetThisThreadId() {
  static std::atomic<std::uint32_t> counter{0};
  thread_local std::uint16_t id = static_cast<std::uint16_t>(
      counter.fetch_add(1, std::memory_order_relaxed) & kThreadIdMask);
  return id;
}

inline static std::uint64_t CurrentTimestampMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

// 최초 Generate() 호출 시점을 epoch으로 사용
inline static std::uint64_t EpochMs() {
  static const std::uint64_t epoch = CurrentTimestampMs();
  return epoch;
}

Snowflake Snowflake::Generate(std::uint16_t machine_id) {
  thread_local std::uint64_t last_ts = 0;
  thread_local std::uint32_t seq = 0;

  std::uint64_t now = CurrentTimestampMs() - EpochMs();

  if (now > last_ts) {
    // 새 ms로 진입
    seq = 0;
  } else {
    now = last_ts;
    if (seq >= 0xFFF) {  // 12bit 초과 → 다음 ms까지 yield & 스핀락
      do {
        std::this_thread::yield();
        now = CurrentTimestampMs() - EpochMs();
      } while (now <= last_ts);
      seq = 0;
    }
  }

  last_ts = now;

  Snowflake s = 0llu;
  s.sign = 0;
  s.timestamp = now;
  s.machine_id = machine_id;
  s.thread_id = GetThisThreadId();
  s.sequence = seq++;
  return s;
}
}  // namespace bedrock
