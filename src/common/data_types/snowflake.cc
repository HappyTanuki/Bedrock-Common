#include "common/data_types/snowflake.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>

namespace bedrock {

// 이 스레드 슬롯 풀을 쓰는 이유는 std::thread::id를 std::hash로 해시했을 때
// 값이 너무 커서 Snowflake의 7bit thread_id에 들어가지 못하는 경우가 많아
// 충돌을 일으키며, 또 스레드가 재사용될 때마다 id가 바뀌므로 Snowflake의
// uniqueness를 보장할 수 없기 때문임.
class ThreadSlotPool {
 public:
  static constexpr std::uint16_t kMaxSlots = 128;

  std::uint16_t Acquire() {
    std::lock_guard<std::mutex> lock(mu_);
    if (!free_list_.empty()) {
      auto s = free_list_.front();
      free_list_.pop();
      return s;
    }
    // 풀이 다 찼으면 fallback (예: 0 반환하거나 예외)
    next_++;
    next_ &= kMaxSlots;
    return next_;
  }

  void Release(std::uint16_t slot) {
    std::lock_guard<std::mutex> lock(mu_);
    free_list_.push(slot);
  }

 private:
  std::mutex mu_;
  std::queue<std::uint16_t> free_list_;
  std::uint16_t next_ = 0;
};

static inline ThreadSlotPool& GlobalSlotPool() {
  static ThreadSlotPool pool;
  return pool;
}

// RAII 래퍼: 생성 시 슬롯 획득, 소멸 시 반납
struct ThreadSlotGuard {
  std::uint16_t slot;
  ThreadSlotGuard() : slot(GlobalSlotPool().Acquire()) {}
  ~ThreadSlotGuard() { GlobalSlotPool().Release(slot); }
};

inline static std::uint16_t GetThisThreadId() {
  thread_local ThreadSlotGuard guard;
  return guard.slot;
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
