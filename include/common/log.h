/**
 * @file log.h
 * @brief 스레드 안전 레벨링 콘솔 로거 API.
 *
 * 로그 레벨(Level)별 편의 함수와 전역 로그 레벨 설정 함수를
 * 선언합니다. 실제 출력은 stderr 로 이뤄지며 스레드 간 동시 호출을
 * 지원합니다(구현: src/common/log.cc).
 */
#pragma once
#include <cstdint>
#include <string_view>

namespace bedrock::log {

/**
 * @brief 로그 심각도 레벨.
 *
 * 숫자가 클수록 더 심각합니다. SetLogLevel() 로 지정한 값보다 낮은
 * 레벨의 로그 호출은 출력되지 않습니다.
 */
enum class Level : std::uint8_t {
  /** @brief 가장 상세한 추적 정보. */
  kTrace = 0,
  /** @brief 디버그용 상세 정보. */
  kDebug = 1,
  /** @brief 일반 정보성 메시지. */
  kInfo = 2,
  /** @brief 경고. 오류는 아니지만 주의가 필요한 상황. */
  kWarn = 3,
  /** @brief 오류. 처리는 계속되나 실패한 동작이 있음. */
  kError = 4,
  /** @brief 치명적 오류. 가장 심각한 레벨. */
  kFatal = 5
};

/**
 * @brief 메시지를 레벨과 함께 stderr 로 출력합니다.
 *
 * level_setting 이 true 이면 실제로는 로그를 남기지 않고 전역 로그
 * 레벨만 lvl 로 설정합니다(SetLogLevel() 이 내부적으로 이 경로를
 * 사용). false(기본값)일 때는 lvl 이 현재 로그 레벨보다 낮으면
 * 출력을 건너뜁니다. 타임스탬프(밀리초)·시간대·스레드 ID 를 담은
 * 접두어를 붙이며, stderr 가 TTY 이면 레벨 태그에 ANSI 색상을
 * 적용합니다. 뮤텍스로 보호되어 여러 스레드에서 동시 호출해도
 * 안전합니다.
 *
 * @param lvl 로그 레벨(또는 level_setting=true 일 때 설정할 레벨).
 * @param msg 출력할 메시지(level_setting=true 이면 무시됨).
 * @param level_setting true 이면 로깅 대신 전역 레벨 설정 동작을
 *        수행합니다. 기본값 false.
 */
void Log(Level lvl, std::string_view msg, bool level_setting = false);

/**
 * @brief 전역 로그 레벨 임계값을 설정합니다.
 *
 * lvl 보다 낮은 레벨의 이후 Log() 호출은 출력되지 않습니다. 내부적
 * 으로 Log(lvl, "", true) 를 호출해 구현됩니다.
 *
 * @param lvl 새로 설정할 최소 로그 레벨.
 */
void SetLogLevel(Level lvl);

/** @brief kTrace 레벨(가장 상세)로 msg 를 로깅합니다. */
void Trace(std::string_view msg);
/** @brief kDebug 레벨로 msg 를 로깅합니다. */
void Debug(std::string_view msg);
/** @brief kInfo 레벨로 msg 를 로깅합니다. */
void Info(std::string_view msg);
/** @brief kWarn 레벨로 msg 를 로깅합니다. */
void Warn(std::string_view msg);
/** @brief kError 레벨로 msg 를 로깅합니다. */
void Error(std::string_view msg);
/** @brief kFatal 레벨(가장 심각)로 msg 를 로깅합니다. */
void Fatal(std::string_view msg);

}  // namespace bedrock::log
