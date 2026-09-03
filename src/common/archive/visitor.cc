/**
 * @file visitor.cc
 * @brief Visitor::operator()(순회 진입점)과 중첩 Schema 방문,
 * State vtable 앵커의 구현.
 */
#include "common/archive/visitor.h"

#include "common/archive.h"

namespace bedrock::archive {

Visitor::~Visitor() = default;

void Visitor::operator()(Schema& root, std::string_view name) {
  OnRootBegin(Field{name});
  root.Accept(*this);
  OnRootEnd();
}

// 중첩 struct: 객체로 진입 -> 자식 스키마를 재귀 방문 -> 객체 종료.
// 직렬화/역직렬화 공용 — OnObjectBegin/End와 Accept가 각 방향으로 디스패치된다.
Visitor& Visitor::Visit(const Field& name, Schema& value) {
  OnObjectBegin(name);
  value.Accept(*this);
  OnObjectEnd();
  return *this;
}

}  // namespace bedrock::archive
