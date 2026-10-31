/**
 * @file
 * @brief YAML 구문 트리에서 presentation-aware Parse event를 방출한다.
 */
#include "archive/yaml/event.h"
#include "archive/yaml/grammar.h"

namespace bedrock::archive::yaml {

EventSink::~EventSink() = default;

bool EventEmitter::Emit(const std::vector<SyntaxNode>& syntax) {
  for (const SyntaxNode& root : syntax) {
    if (!Visit(root)) {
      return false;
    }
  }
  return true;
}

bool EventEmitter::Visit(const SyntaxNode& node) {
  const auto visit_collection = [this, &node](EventKind start_kind,
                                              EventKind end_kind) {
    if (!output_->OnEvent({.kind = start_kind, .begin = node.begin})) {
      return false;
    }
    for (const SyntaxNode& child : node.children) {
      if (!Visit(child)) {
        return false;
      }
    }
    return output_->OnEvent({.kind = end_kind, .begin = node.end});
  };

  switch (node.kind) {
    case SyntaxKind::kDocument:
      return visit_collection(EventKind::kDocStart, EventKind::kDocEnd);
    case SyntaxKind::kMapping:
      return visit_collection(EventKind::kMapStart, EventKind::kMapEnd);
    case SyntaxKind::kSequence:
      return visit_collection(EventKind::kSeqStart, EventKind::kSeqEnd);
    case SyntaxKind::kScalar:
      return output_->OnEvent({.kind = EventKind::kScalar,
                               .style = node.style,
                               .chomp = node.chomp,
                               .indent = node.indent,
                               .begin = node.begin,
                               .end = node.end});
    case SyntaxKind::kAlias:
      return output_->OnEvent(
          {.kind = EventKind::kAlias, .begin = node.begin, .end = node.end});
    case SyntaxKind::kAnchor:
      return output_->OnEvent(
          {.kind = EventKind::kAnchor, .begin = node.begin, .end = node.end});
    case SyntaxKind::kTag:
      return output_->OnEvent(
          {.kind = EventKind::kTag, .begin = node.begin, .end = node.end});
  }
  // 열거되지 않은 값(손상된 SyntaxKind)은 거부한다.
  return false;
}


}  // namespace bedrock::archive::yaml
