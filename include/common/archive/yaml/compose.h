/**
 * @file compose.h
 * @brief YAML 이벤트 열 → 표현(Node) 트리 조립기(Compose) 선언.
 *
 * 스펙(YAML 3.1) Load의 두 번째 단계다: Parse(Grammar가 방출한 이벤트)
 * 를 소비해 문서별 transcriber::Node 트리를 만든다. 이 단계에서
 * 스칼라 값을 확정한다 — 이스케이프 해석(겹따옴표), 따옴표 축약(''),
 * 접힘(plain/quoted/folded), 청킹(블록 스칼라) — 그리고 앵커를 등록하고
 * 별칭을 값 복사로 해소한다. 태그는 원문 그대로 노드에 싣는다(태그
 * 해소·타입 변환은 Construct 단계의 몫).
 */
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "common/archive/transcriber/node.h"
#include "common/archive/yaml/grammar.h"

namespace bedrock::archive::yaml {

/** @brief Compose 결과 — 문서들의 표현 트리 또는 조립 오류. */
struct ComposeResult {
  /** @brief 조립 성공 여부. */
  bool ok = false;
  /** @brief 실패 사유(미해소 별칭 등). ok=true면 빈 문자열. */
  std::string error;
  /** @brief 문서별 루트 노드(스트림의 문서 순서대로). */
  std::vector<transcriber::Node> docs;
};

/**
 * @brief 이벤트 열을 표현 트리로 조립한다.
 * @param buf 파스에 사용한 코드포인트 버퍼(이벤트 구간 해석용).
 * @param events Grammar::Isl_yaml_stream이 확정한 이벤트 열.
 */
ComposeResult Compose(std::span<const std::uint32_t> buf,
                      std::span<const Event> events);

}  // namespace bedrock::archive::yaml
