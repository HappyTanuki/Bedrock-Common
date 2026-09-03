/**
 * @file archive.h
 * @brief 아카이브(직렬화) 프레임워크의 최상위 진입점.
 *
 * 사용자 데이터 타입이 구현해야 하는 Schema 인터페이스를 정의한다.
 * Visitor 패턴의 Element 역할이며, 실제 방문 로직은
 * common/archive/visitor.h 의 Visitor가 담당한다.
 */
#pragma once

#include "common/archive/visitor.h"

namespace bedrock::archive {

/**
 * @brief 직렬화 대상 데이터 타입이 구현하는 Element 인터페이스.
 *
 * 사용자 구조체가 이 구조체를 상속하고 Accept()를 구현하면 Visitor를
 * 통해 직렬화·역직렬화될 수 있다.
 */
struct Schema {
  Schema() = default;
  Schema(const Schema&) = delete;
  Schema& operator=(const Schema&) = delete;
  Schema(Schema&&) = delete;
  Schema& operator=(Schema&&) = delete;
  /** @brief 가상 소멸자. */
  virtual ~Schema();
  /**
   * @brief Visitor를 받아들여 자신의 멤버들을 방문시킨다.
   * @param visitor 방문을 수행하는 Visitor(직렬화기/역직렬화기).
   */
  virtual void Accept(Visitor& visitor) = 0;
};

}  // namespace bedrock::archive
