/**
 * @file compose.cc
 * @brief RSF Compose(이벤트 -> 표현 트리) 구현부.
 *
 * 프레임 스택으로 조립한다(rbf/compose.cc와 동일 골격): 컨테이너 시작에
 * 프레임을 쌓고 자식을 모으며, 끝에서 노드로 마감해 부모에 붙인다.
 */
#include "archive/rsf/compose.h"

#include <utility>
#include <vector>

namespace bedrock::archive::rsf {

namespace {}  // namespace

ComposeResult Compose(std::span<const Event> events) {
  struct Frame {
    transcriber::Node node;
    std::vector<transcriber::Node> children;
  };
  std::vector<Frame> stack;
  transcriber::Node root;
  bool have_root = false;

  auto emit = [&](transcriber::Node&& node) {
    if (stack.empty()) {
      root = std::move(node);
      have_root = true;
    } else {
      stack.back().children.push_back(std::move(node));
    }
  };

  for (const Event& event : events) {
    switch (event.kind) {
      case EventKind::kScalar: {
        transcriber::Node node;
        node.kind = transcriber::Node::Kind::kScalar;
        node.null = event.null;
        node.vtype = event.vtype;
        node.scalar = event.scalar;
        emit(std::move(node));
        break;
      }
      case EventKind::kSeqStart: {
        Frame frame;
        frame.node.kind = transcriber::Node::Kind::kSequence;
        stack.push_back(std::move(frame));
        break;
      }
      case EventKind::kSetStart: {
        Frame frame;
        frame.node.kind = transcriber::Node::Kind::kSet;
        stack.push_back(std::move(frame));
        break;
      }
      case EventKind::kMapStart: {
        Frame frame;
        frame.node.kind = transcriber::Node::Kind::kMapping;
        stack.push_back(std::move(frame));
        break;
      }
      case EventKind::kSeqEnd:
      case EventKind::kSetEnd: {
        if (stack.empty()) {
          return ComposeResult{false, "rsf: unbalanced container end", {}};
        }
        Frame frame = std::move(stack.back());
        stack.pop_back();
        frame.node.items = std::move(frame.children);
        emit(std::move(frame.node));
        break;
      }
      case EventKind::kMapEnd: {
        if (stack.empty()) {
          return ComposeResult{false, "rsf: unbalanced mapping end", {}};
        }
        Frame frame = std::move(stack.back());
        stack.pop_back();
        if (frame.children.size() % 2 != 0) {
          return ComposeResult{false, "rsf: odd mapping child count", {}};
        }
        for (std::size_t i = 0; i + 1 < frame.children.size(); i += 2) {
          frame.node.pairs.push_back(
              {std::move(frame.children[i]), std::move(frame.children[i + 1])});
        }
        emit(std::move(frame.node));
        break;
      }
    }
  }

  if (!stack.empty()) {
    return ComposeResult{false, "rsf: unterminated container", {}};
  }
  if (!have_root) {
    return ComposeResult{false, "rsf: empty document", {}};
  }
  return ComposeResult{true, "", std::move(root)};
}

}  // namespace bedrock::archive::rsf
