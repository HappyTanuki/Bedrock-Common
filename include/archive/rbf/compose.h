/**
 * @file compose.h
 * @brief RBF Load의 2단계 — Compose(이벤트 -> 표현 트리) 선언.
 *
 * 스펙(1.2.2) Load의 둘째 단계에 대응한다(YAML의 compose.cc 대응). Parse가
 * 방출한 이벤트 열을 스택으로 조립해 표현 트리(transcriber::Node)를 만든다.
 * 이후 베이스 Deserializer의 Construct가 이 트리를 목표 C++ 타입으로 해소한다.
 */
#pragma once
#include <span>
#include <string>

#include "archive/node.h"
#include "archive/rbf/parse.h"

namespace bedrock::archive::rbf {

/** @brief Compose 결과 — 루트 노드 또는 오류. */
struct ComposeResult {
  bool ok = false;
  std::string error;
  transcriber::Node root;
};

/**
 * @brief 이벤트 열을 표현 트리로 조립한다.
 * @param events Parse가 방출한 이벤트 열.
 */
ComposeResult Compose(std::span<const Event> events);

}  // namespace bedrock::archive::rbf
