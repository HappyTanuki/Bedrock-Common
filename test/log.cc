/**
 * @file log.cc
 * @brief bedrock::log 로거의 다중 스레드 동시 호출 테스트.
 *
 * Trace~Fatal 여섯 레벨을 각각 별도 스레드에서 동시에 한 번씩
 * 호출하고, 메인 스레드에서도 Debug를 호출해, 여러 스레드가 동시에
 * 로깅해도 크래시나 데드락 없이 동작하는지 확인한다.
 *
 * @return 스레드들이 모두 정상 join되면 항상 EXIT_SUCCESS(0). 출력
 *         내용 자체는 검사하지 않으며, 충돌·데드락이 곧 실패다.
 */
#include "common/log.h"

#include <stdlib.h>

#include <thread>

/** @brief 로그 레벨을 Trace로 설정하고 6개 스레드에서 동시에 로깅한다. */
int main() {
  bedrock::log::SetLogLevel(bedrock::log::Level::kTrace);

  auto t1 = std::thread([]() { bedrock::log::Trace("test message."); });
  auto t2 = std::thread([]() { bedrock::log::Debug("test message."); });
  auto t3 = std::thread([]() { bedrock::log::Info("test message."); });
  auto t4 = std::thread([]() { bedrock::log::Warn("test message."); });
  auto t5 = std::thread([]() { bedrock::log::Error("test message."); });
  auto t6 = std::thread([]() { bedrock::log::Fatal("test message."); });

  bedrock::log::Debug("test message.");

  t1.join();
  t2.join();
  t3.join();
  t4.join();
  t5.join();
  t6.join();

  return EXIT_SUCCESS;
}
