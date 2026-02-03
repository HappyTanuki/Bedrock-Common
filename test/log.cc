#include "common/log.h"

#include <stdlib.h>

#include <thread>

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
