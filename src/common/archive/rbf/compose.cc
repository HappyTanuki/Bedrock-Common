/**
 * @file compose.cc
 * @brief RBF Compose(이벤트 -> 표현 트리) 구현부.
 *
 * 이벤트 열을 프레임 스택으로 조립한다: 컨테이너 시작에 프레임을 쌓고,
 * 자식(스칼라/완성된 컨테이너)을 프레임에 모으며, 끝에서 노드로 마감해
 * 부모에 붙인다. 매핑은 모인 자식을 (키,값) 순서쌍으로 짝짓는다.
 */
#include "archive/rbf/compose.h"

#include <utility>
#include <vector>

namespace bedrock::archive::rbf {

namespace {}  // namespace

ComposeResult Compose(std::span<const Event> events) {
  /** @brief 조립 중 컨테이너 하나 — 완성될 노드와 모인 자식들. */
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
          return ComposeResult{false, "rbf: unbalanced container end", {}};
        }
        Frame frame = std::move(stack.back());
        stack.pop_back();
        frame.node.items = std::move(frame.children);
        emit(std::move(frame.node));
        break;
      }
      case EventKind::kMapEnd: {
        if (stack.empty()) {
          return ComposeResult{false, "rbf: unbalanced mapping end", {}};
        }
        Frame frame = std::move(stack.back());
        stack.pop_back();
        if (frame.children.size() % 2 != 0) {
          return ComposeResult{false, "rbf: odd mapping child count", {}};
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
    return ComposeResult{false, "rbf: unterminated container", {}};
  }
  if (!have_root) {
    return ComposeResult{false, "rbf: empty document", {}};
  }
  return ComposeResult{true, "", std::move(root)};
}

}  // namespace bedrock::archive::rbf
