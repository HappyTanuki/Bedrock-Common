#include "common/log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <ctime>
#endif
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

extern char* tzname[2];

namespace bedrock::log {

void Log(Level lvl, std::string_view msg, bool level_setting) {
  static Level log_level = Level::kDebug;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  std::uint32_t ms;

#ifdef _WIN32
  LARGE_INTEGER freq, ts;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&ts);
  ms = ts.QuadPart * 1000LL / freq.QuadPart;
#else
  ::timespec ts;
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  ms = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
#endif
  ms %= 1000;
  ::time_t timer = ::time(NULL);
  ::tm* t = ::localtime(&timer);

  const char* tz = nullptr;

  std::string prefix = "";

  char buffer[100] = "";
  if (::strcmp(tzname[0], tzname[1]) != 0) {
    ::sprintf(buffer, "[%04d-%02d-%02d %02d:%02d:%02d.%03d %s/%s]",
              1900 + t->tm_year, t->tm_mon + 1, t->tm_mday, t->tm_hour,
              t->tm_min, t->tm_sec, ms, tzname[0], tzname[1]);
  } else {
    ::sprintf(buffer, "[%04d-%02d-%02d %02d:%02d:%02d.%03d %s]",
              1900 + t->tm_year, t->tm_mon + 1, t->tm_mday, t->tm_hour,
              t->tm_min, t->tm_sec, ms, tzname[0]);
  }

  prefix += buffer;

  if (level_setting) {
    log_level = lvl;
    return;
  }

  if (lvl < log_level) {
    return;
  }

  if (::isatty(STDERR_FILENO)) {
    switch (lvl) {
      case Level::kTrace:
        prefix += "[TRACE]";
        break;
      case Level::kDebug:
        prefix += "\x1b[32m[DEBUG]\x1b[0m";
        break;
      case Level::kInfo:
        prefix += "\x1b[34m[ INFO]\x1b[0m";
        break;
      case Level::kWarn:
        prefix += "\x1b[33m[ WARN]\x1b[0m";
        break;
      case Level::kError:
        prefix += "\x1b[31m[ERROR]\x1b[0m";
        break;
      case Level::kFatal:
        prefix += "\x1b[31m[FATAL]\x1b[0m";
        break;
      default:
        break;
    }
  } else {
    switch (lvl) {
      case Level::kTrace:
        prefix += "[TRACE]";
        break;
      case Level::kDebug:
        prefix += "[DEBUG]";
        break;
      case Level::kInfo:
        prefix += "[ INFO]";
        break;
      case Level::kWarn:
        prefix += "[ WARN]";
        break;
      case Level::kError:
        prefix += "[ERROR]";
        break;
      case Level::kFatal:
        prefix += "[FATAL]";
        break;
      default:
        break;
    }
  }

  std::cerr << prefix << "[" << std::this_thread::get_id() << "]: " << msg
            << "\n";

  return;
}

void SetLogLevel(Level lvl) { Log(lvl, "", true); }

void Trace(std::string_view msg) { return Log(Level::kTrace, msg); }
void Debug(std::string_view msg) { return Log(Level::kDebug, msg); }
void Info(std::string_view msg) { return Log(Level::kInfo, msg); }
void Warn(std::string_view msg) { return Log(Level::kWarn, msg); }
void Error(std::string_view msg) { return Log(Level::kError, msg); }
void Fatal(std::string_view msg) { return Log(Level::kFatal, msg); }

}  // namespace bedrock::log