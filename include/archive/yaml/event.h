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
 *
 * 현재 DOM 경로는 Events에 수집한 뒤 SerializationStream을 만든다. 이 sink
 * 경계는 향후 parser가 구문 트리나 event vector를 materialize하지 않고 event를
 * 직접 전달할 수 있게 유지한다. Event가 source range와 YAML style을 가지므로
 * 아직 포맷 중립 public SAX 계약은 아니다. false를 반환하면 방출을 중단한다.
 */
class EventSink {
 public:
  virtual ~EventSink() = default;
  virtual bool OnEvent(const Event& event) = 0;
};

/** @brief 완성된 구문 트리에서 생성하는 materialized event adapter. */
struct Events {
  std::vector<Event> list;
};

/** @brief 구문 트리를 순회하며 sink에 Parse event를 순서대로 방출한다. */
bool EmitEvents(const std::vector<SyntaxNode>& syntax, EventSink& sink);

/** @brief 구문 트리를 순회해 materialized event adapter를 만든다. */
Events BuildEvents(const std::vector<SyntaxNode>& syntax);

}  // namespace bedrock::archive::yaml
