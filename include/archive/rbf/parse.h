/**
 * @file parse.h
 * @brief RBF Load의 1단계 — Parse(바이트 -> 이벤트) 선언.
 *
 * 스펙(1.2.2) Load의 첫 단계에 대응한다(YAML의 grammar.cc 대응). RBF
 * 바이트열을 훑어 컨테이너 시작/끝 마커와 스칼라를 이벤트 열로 방출한다.
 * 다음 단계 Compose(rbf/compose.h)가 이 이벤트를 표현 트리로 조립한다.
 *
 * 바이너리는 문법이 트리비얼(백트래킹·앵커 없음)하므로 Parse는 태그·길이·
 * 개수를 그대로 읽어 이벤트로 옮기는 단순 변환이다. 손상 입력 방어(개수/
 * 길이 상한, 중첩 깊이 상한)는 이 단계에서 수행한다.
 */
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "archive/node.h"

namespace bedrock::archive::rbf {

/** @brief 파스 이벤트 종류. */
enum class EventKind : std::uint8_t {
  kScalar,
  kSeqStart,
  kSeqEnd,
  kSetStart,
  kSetEnd,
  kMapStart,
  kMapEnd,
};

/** @brief 파스 이벤트 하나(스칼라는 값 필드가 채워진다). */
struct Event {
  EventKind kind = EventKind::kScalar;
  bool null = false;                                             ///< kScalar
  transcriber::ValueType vtype = transcriber::ValueType::kNull;  ///< kScalar
  std::string scalar;                                            ///< kScalar
};

/** @brief Parse 결과 — 이벤트 열 또는 오류. */
struct ParseResult {
  bool ok = false;
  std::string error;
  std::vector<Event> events;
};

/**
 * @brief RBF 바이트열을 이벤트 열로 파스한다.
 * @param bytes 매직을 포함한 전체 RBF 스트림.
 */
ParseResult Parse(std::span<const std::uint8_t> bytes);

}  // namespace bedrock::archive::rbf
