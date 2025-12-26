#include "common/log.h"

#include <stdlib.h>

#include <thread>

int main() {
  bedrock::log::SetLogLevel(bedrock::log::Level::kTrace);

  std::thread([]() { bedrock::log::Trace("test message."); }).detach();
  std::thread([]() { bedrock::log::Debug("test message."); }).detach();
  std::thread([]() { bedrock::log::Info("test message."); }).detach();
  std::thread([]() { bedrock::log::Warn("test message."); }).detach();
  std::thread([]() { bedrock::log::Error("test message."); }).detach();
  std::thread([]() { bedrock::log::Fatal("test message."); }).detach();

  bedrock::log::Debug("test message.");

  return EXIT_SUCCESS;
}
