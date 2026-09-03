/**
 * @file log.cc
 * @brief 스레드 안전 레벨링 콘솔 로거 구현.
 *
 * 타임스탬프(밀리초)·시간대·스레드 ID 접두어 구성과 TTY 여부에 따른
 * ANSI 색상 처리를 포함해 Log()/SetLogLevel() 및 레벨별 편의 함수의
 * 실제 동작을 구현합니다.
 */
#include "common/log.h"

#ifdef _WIN32
#include <io.h>
#include <time.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <array>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#ifndef _WIN32

#endif

namespace bedrock::log {

void Log(Level lvl, std::string_view msg, bool level_setting) {
  static Level log_level = Level::kDebug;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);

  std::uint32_t milliseconds = 0;

#ifdef _WIN32
  LARGE_INTEGER freq, monotonic_time;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&monotonic_time);
  milliseconds = monotonic_time.QuadPart * 1000LL / freq.QuadPart;
  _tzset();
#else
  ::timespec monotonic_time{};
  ::clock_gettime(CLOCK_MONOTONIC, &monotonic_time);
  milliseconds =
      monotonic_time.tv_sec * 1000LL + monotonic_time.tv_nsec / 1000000LL;
#endif
  milliseconds %= 1000;
  ::time_t timer = ::time(nullptr);
  ::tm* local_time = ::localtime(&timer);

  std::string prefix;

  std::array<char, 100> buffer{};

#ifdef _WIN32
  if (::strcmp(_tzname[0], _tzname[1]) != 0) {
    ::snprintf(buffer.data(), buffer.size(),
               "[%04d-%02d-%02d %02d:%02d:%02d.%03d %s/%s]",
               1900 + local_time->tm_year, local_time->tm_mon + 1,
               local_time->tm_mday, local_time->tm_hour, local_time->tm_min,
               local_time->tm_sec, milliseconds, _tzname[0], _tzname[1]);
  } else {
    ::snprintf(buffer.data(), buffer.size(),
               "[%04d-%02d-%02d %02d:%02d:%02d.%03d %s]",
               1900 + local_time->tm_year, local_time->tm_mon + 1,
               local_time->tm_mday, local_time->tm_hour, local_time->tm_min,
               local_time->tm_sec, milliseconds, _tzname[0]);
  }
#else
  if (::strcmp(tzname[0], tzname[1]) != 0) {
    ::snprintf(buffer.data(), buffer.size(),
               "[%04d-%02d-%02d %02d:%02d:%02d.%03d %s/%s]",
               1900 + local_time->tm_year, local_time->tm_mon + 1,
               local_time->tm_mday, local_time->tm_hour, local_time->tm_min,
               local_time->tm_sec, milliseconds, tzname[0], tzname[1]);
  } else {
    ::snprintf(buffer.data(), buffer.size(),
               "[%04d-%02d-%02d %02d:%02d:%02d.%03d %s]",
               1900 + local_time->tm_year, local_time->tm_mon + 1,
               local_time->tm_mday, local_time->tm_hour, local_time->tm_min,
               local_time->tm_sec, milliseconds, tzname[0]);
  }
#endif

  prefix += buffer.data();

  if (level_setting) {
    log_level = lvl;
    return;
  }

  if (lvl < log_level) {
    return;
  }

  bool is_tty = true;

#if _WIN32
  is_tty = ::_isatty(_fileno(stderr));
#else
  is_tty = (::isatty(STDERR_FILENO) != 0);
#endif

  if (is_tty) {
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
}

void SetLogLevel(Level lvl) { Log(lvl, "", true); }

void Trace(std::string_view msg) { Log(Level::kTrace, msg); }
void Debug(std::string_view msg) { Log(Level::kDebug, msg); }
void Info(std::string_view msg) { Log(Level::kInfo, msg); }
void Warn(std::string_view msg) { Log(Level::kWarn, msg); }
void Error(std::string_view msg) { Log(Level::kError, msg); }
void Fatal(std::string_view msg) { Log(Level::kFatal, msg); }

}  // namespace bedrock::log