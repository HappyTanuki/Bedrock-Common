/**
 * @file parse.h
 * @brief RSF(Reference std::string Format) Load 1단계 — Parse(텍스트 -> 이벤트).
 *
 * RSF는 YAML과 같은 선상의 **자기서술 텍스트** 참조 포맷이다(경로 A). 값마다
 * 종류·타입을 텍스트로 실으므로 스키마 없이 트리를 만들 수 있다. Parse는
 * 문자열을 훑어 컨테이너 시작/끝과 스칼라를 이벤트 열로 방출하고, Compose가
 * 표현 트리로 조립한다.
 *
 * 문법(자기서술):
 *   value  := scalar | seq | map | set
 *   seq    := '[' value* ']'
 *   set    := '(' value* ')'
 *   map    := '{' (key value)* '}'
 *   scalar := 'Z'                        (null/e-node)
 *           | TypeChar '"' escaped '"'   (TypeChar: P N S B Y)
 *   TypeChar -> vtype: P=plain(kNull) N=kNumber S=kString B=kBoolean Y=kBinary
 *   escaped: \\  \"  \xHH (제어문자)  그 외 바이트 원문(UTF-8 포함)
 *   토큰은 공백/개행으로 구분(들여쓰기는 장식).
 */
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "archive/node.h"

namespace bedrock::archive::rsf {

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

/** @brief 파스 이벤트 하나. */
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
 * @brief RSF 텍스트를 이벤트 열로 파스한다.
 * @param text RSF 문서 전체(UTF-8 바이트).
 */
ParseResult Parse(std::span<const char> text);

}  // namespace bedrock::archive::rsf
