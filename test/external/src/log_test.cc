/**
 * @file log_test.cc
 * @brief 로거의 다중 스레드 동시 호출 테스트.
 */
#include "common/log.h"

#include <gtest/gtest.h>

#include <thread>

TEST(Log, ConcurrentCallsComplete) {
  bedrock::log::SetLogLevel(bedrock::log::Level::kTrace);

  std::thread trace_thread([]() { bedrock::log::Trace("test message."); });
  std::thread debug_thread([]() { bedrock::log::Debug("test message."); });
  std::thread info_thread([]() { bedrock::log::Info("test message."); });
  std::thread warn_thread([]() { bedrock::log::Warn("test message."); });
  std::thread error_thread([]() { bedrock::log::Error("test message."); });
  std::thread fatal_thread([]() { bedrock::log::Fatal("test message."); });

  bedrock::log::Debug("test message.");

  trace_thread.join();
  debug_thread.join();
  info_thread.join();
  warn_thread.join();
  error_thread.join();
  fatal_thread.join();
  SUCCEED();
}
