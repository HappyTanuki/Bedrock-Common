/**
 * @file event.h
 * @brief YAML 구문 트리 변환 결과인 Parse 내부 event와 sink 선언.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "archive/yaml/syntax.h"

namespace bedrock::archive::yaml {

/** @brief Parse 내부 node event의 종류. */
enum class EventKind : std::uint8_t {
  kDocStart,
  kDocEnd,
  kMapStart,
  kMapEnd,
  kSeqStart,
  kSeqEnd,
  kScalar,
  kAlias,
  kAnchor,
  kTag,
};

/**
 * @brief 구문 트리 visitor가 생성하는 presentation-aware event 하나.
 *
 * begin/end는 입력 버퍼의 코드포인트 오프셋 [begin,end)이다. kScalar에서는
 * 본문 구간, kAlias/kAnchor는 앵커 이름, kTag는 태그 원문 전체 구간이다.
 */
struct Event {
  EventKind kind = EventKind::kDocStart;
  ScalarStyle style = ScalarStyle::kPlain;
  ChompKind chomp = ChompKind::kClip;
  std::ptrdiff_t indent = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
};

/**
 * @brief Parse event를 순서대로 소비하는 내부 sink.

 * Parse는 구문 트리를 materialize한 뒤 EventEmitter가 presentation 정보를 담은
 * event를 순서대로 방출한다. Event는 원문 구간과 YAML 스타일을 가지므로
 * 포맷 중립 API가 아니며, false를 반환하면 방출을 중단한다.
 */
class EventSink {
 public:
  virtual ~EventSink();
  virtual bool OnEvent(const Event& event) = 0;
};

/** @brief 구문 트리를 순회하며 Parse event를 output에 방출하는 작업 객체. */
class EventEmitter final {
 public:
  explicit EventEmitter(EventSink& output) : output_(&output) {}

  bool Emit(const std::vector<SyntaxNode>& syntax);

 private:
  bool Visit(const SyntaxNode& node);

  EventSink* output_;
};

}  // namespace bedrock::archive::yaml
