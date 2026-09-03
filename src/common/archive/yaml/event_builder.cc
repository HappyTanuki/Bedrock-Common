/**
 * @file
 * @brief YAML 구문 트리를 Parse 내부 serialization event로 변환한다.
 */
#include "archive/yaml/event.h"
#include "archive/yaml/grammar.h"

namespace bedrock::archive::yaml {
namespace {

bool Visit(const SyntaxNode& node, EventSink& sink) {
  switch (node.kind) {
    case SyntaxKind::kDocument:
      if (!sink.OnEvent({.kind = EventKind::kDocStart, .begin = node.begin})) {
        return false;
      }
      for (const SyntaxNode& child : node.children) {
        if (!Visit(child, sink)) {
          return false;
        }
      }
      return sink.OnEvent({.kind = EventKind::kDocEnd, .begin = node.end});
    case SyntaxKind::kMapping:
      if (!sink.OnEvent({.kind = EventKind::kMapStart, .begin = node.begin})) {
        return false;
      }
      for (const SyntaxNode& child : node.children) {
        if (!Visit(child, sink)) {
          return false;
        }
      }
      return sink.OnEvent({.kind = EventKind::kMapEnd, .begin = node.end});
    case SyntaxKind::kSequence:
      if (!sink.OnEvent({.kind = EventKind::kSeqStart, .begin = node.begin})) {
        return false;
      }
      for (const SyntaxNode& child : node.children) {
        if (!Visit(child, sink)) {
          return false;
        }
      }
      return sink.OnEvent({.kind = EventKind::kSeqEnd, .begin = node.end});
    case SyntaxKind::kScalar:
      return sink.OnEvent({.kind = EventKind::kScalar,
                           .style = node.style,
                           .chomp = node.chomp,
                           .indent = node.indent,
                           .begin = node.begin,
                           .end = node.end});
    case SyntaxKind::kAlias:
      return sink.OnEvent(
          {.kind = EventKind::kAlias, .begin = node.begin, .end = node.end});
    case SyntaxKind::kAnchor:
      return sink.OnEvent(
          {.kind = EventKind::kAnchor, .begin = node.begin, .end = node.end});
    case SyntaxKind::kTag:
      return sink.OnEvent(
          {.kind = EventKind::kTag, .begin = node.begin, .end = node.end});
  }
  return false;
}

class CollectingEventSink final : public EventSink {
 public:
  explicit CollectingEventSink(Events& events) : events_(events) {}

  bool OnEvent(const Event& event) final {
    events_.list.push_back(event);
    return true;
  }

 private:
  Events& events_;
};

}  // namespace

bool EmitEvents(const std::vector<SyntaxNode>& syntax, EventSink& sink) {
  for (const SyntaxNode& root : syntax) {
    if (!Visit(root, sink)) {
      return false;
    }
  }
  return true;
}

Events BuildEvents(const std::vector<SyntaxNode>& syntax) {
  Events events;
  CollectingEventSink sink(events);
  static_cast<void>(EmitEvents(syntax, sink));
  return events;
}

}  // namespace bedrock::archive::yaml
