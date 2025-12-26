#ifndef BEDROCK_COMMON_COMMON_LOG_H_
#define BEDROCK_COMMON_COMMON_LOG_H_

#include <string_view>

namespace bedrock::log {

enum Level {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
  kFatal = 5
};

void Log(Level lvl, std::string_view msg, bool level_setting = false);
void SetLogLevel(Level lvl);

void Trace(std::string_view msg);
void Debug(std::string_view msg);
void Info(std::string_view msg);
void Warn(std::string_view msg);
void Error(std::string_view msg);
void Fatal(std::string_view msg);

}  // namespace bedrock::log

#endif
