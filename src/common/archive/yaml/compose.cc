/**
 * @file compose.cc
 * @brief YAML serialization tree를 representation으로 Compose한다.
 */
#include "archive/yaml/compose.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "common/util/base64.h"

namespace bedrock::archive::yaml {
namespace {

/**
 * @brief 노드에 붙은 태그가 노드 종류·값 형식과 적합한지 검증한다.
 *
 * 컬렉션(sequence/mapping)에 스칼라 전용 표준 태그(!!int, !!float,
 * !!bool, !!str, !!binary 등)가 붙으면 representation 규칙 위반이다.
 * !!binary 스칼라는 base64(RFC 4648)로 디코드 가능해야 한다.
 */
bool ValidateNodeTag(const SerializationNode& serialized, std::string& error) {
  static const char* const kScalarOnlyTags[] = {
      "!!int", "!!float", "!!bool", "!!str", "!!binary", "!!timestamp"};
  if (serialized.kind != SerializationNode::Kind::kScalar) {
    for (const char* scalar_only_tag : kScalarOnlyTags) {
      if (serialized.tag == scalar_only_tag) {
        error = std::string("tag ") + scalar_only_tag +
                " is not allowed on a collection";
        return false;
      }
    }
    return true;
  }
  if (serialized.tag == "!!binary") {
    std::vector<std::uint8_t> decoded;
    if (!util::Base64Decode(serialized.scalar, decoded)) {
      error = "!!binary scalar is not valid base64: " + serialized.scalar;
      return false;
    }
  }
  return true;
}

/**
 * @brief 매핑 키의 equality를 판정한다.
 *
 * YAML representation에서 스칼라 키는 (태그, 값 종류, 디코드된 문자열)
 * 삼인조로 동일성을 판단한다. 컬렉션 키는 구조 전체를 재귀 비교한다.
 */
bool SameKey(const transcriber::Node& left, const transcriber::Node& right) {
  if (left.kind != right.kind || left.tag != right.tag ||
      left.vtype != right.vtype) {
    return false;
  }
  if (left.kind == transcriber::Node::Kind::kScalar) {
    return left.scalar == right.scalar && left.null == right.null;
  }
  if (left.items.size() != right.items.size() ||
      left.pairs.size() != right.pairs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.items.size(); ++index) {
    if (!SameKey(left.items[index], right.items[index])) {
      return false;
    }
  }
  for (std::size_t index = 0; index < left.pairs.size(); ++index) {
    if (!SameKey(left.pairs[index].key, right.pairs[index].key) ||
        !SameKey(left.pairs[index].value, right.pairs[index].value)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief 매핑 안에서 키가 유일한지 검증한다(YAML 1.2.2 §3.1.1).
 *
 * "a: 1\na: 2\n"처럼 같은 매핑에 동일 키가 두 번 나타나면
 * representation 모델 위반이다. 다른 매핑끼리는 서로 독립이다.
 */
bool ValidateKeyUniqueness(const SerializationNode& mapping,
                           std::string& error) {
  std::vector<const SerializationNode::Pair*> seen;
  seen.reserve(mapping.pairs.size());
  for (const SerializationNode::Pair& pair : mapping.pairs) {
    for (const SerializationNode::Pair* previous : seen) {
      if (previous->key.tag == pair.key.tag &&
          previous->key.value_type == pair.key.value_type &&
          previous->key.scalar == pair.key.scalar &&
          previous->key.null == pair.key.null &&
          previous->key.kind == pair.key.kind &&
          previous->key.items.size() == pair.key.items.size() &&
          previous->key.pairs.size() == pair.key.pairs.size()) {
        error = "duplicate key: " + pair.key.scalar;
        return false;
      }
    }
    seen.push_back(&pair);
  }
  return true;
}

bool ComposeNode(const SerializationNode& serialized,
                 std::map<std::string, transcriber::Node>& anchors,
                 transcriber::Node& representation, bool allow_duplicate_keys,
                 std::string& error) {
  if (serialized.kind == SerializationNode::Kind::kAlias) {
    const auto iterator = anchors.find(serialized.alias);
    if (iterator == anchors.end()) {
      error = "unresolved alias *" + serialized.alias;
      return false;
    }
    representation = iterator->second;
    return true;
  }

  representation.null = serialized.null;
  representation.tag = serialized.tag;
  representation.vtype = serialized.value_type;
  representation.scalar = serialized.scalar;
  if (serialized.kind == SerializationNode::Kind::kScalar) {
    if (!ValidateNodeTag(serialized, error)) {
      return false;
    }
    representation.kind = transcriber::Node::Kind::kScalar;
  } else if (serialized.kind == SerializationNode::Kind::kSequence) {
    if (!ValidateNodeTag(serialized, error)) {
      return false;
    }
    representation.kind = transcriber::Node::Kind::kSequence;
    for (const SerializationNode& item : serialized.items) {
      transcriber::Node child;
      if (!ComposeNode(item, anchors, child, allow_duplicate_keys, error)) {
        return false;
      }
      representation.items.push_back(std::move(child));
    }
  } else {
    if (!ValidateNodeTag(serialized, error)) {
      return false;
    }
    if (!allow_duplicate_keys && !ValidateKeyUniqueness(serialized, error)) {
      return false;
    }
    representation.kind = transcriber::Node::Kind::kMapping;
    for (const SerializationNode::Pair& pair : serialized.pairs) {
      transcriber::Node key;
      transcriber::Node value;
      if (!ComposeNode(pair.key, anchors, key, allow_duplicate_keys, error) ||
          !ComposeNode(pair.value, anchors, value, allow_duplicate_keys,
                       error)) {
        return false;
      }
      representation.pairs.push_back({std::move(key), std::move(value)});
    }
  }
  if (!serialized.anchor.empty()) {
    anchors[serialized.anchor] = representation;
  }
  return true;
}

}  // namespace

struct ComposeEventBuilder::Impl {
  struct Build {
    transcriber::Node node;
    transcriber::Node key;
    bool has_key = false;
    std::string anchor;
  };

  explicit Impl(const ComposeOptions& compose_options)
      : options(compose_options) {}

  bool Fail(std::string message) {
    result.error = std::move(message);
    failed = true;
    return false;
  }

  bool Attach(transcriber::Node&& node) {
    if (stack.empty()) {
      root = std::move(node);
      have_root = true;
      return true;
    }
    Build& top = stack.back();
    if (top.node.kind == transcriber::Node::Kind::kSequence) {
      top.node.items.push_back(std::move(node));
      return true;
    }
    if (top.node.kind == transcriber::Node::Kind::kMapping) {
      if (!top.has_key) {
        top.key = std::move(node);
        top.has_key = true;
      } else {
        top.node.pairs.push_back({std::move(top.key), std::move(node)});
        top.has_key = false;
      }
      return true;
    }
    return Fail("node has no parent");
  }

  bool ValidateKeys(const transcriber::Node& mapping) {
    if (options.allow_duplicate_keys) {
      return true;
    }
    std::vector<const transcriber::Node::Pair*> seen;
    seen.reserve(mapping.pairs.size());
    for (const transcriber::Node::Pair& pair : mapping.pairs) {
      for (const transcriber::Node::Pair* previous : seen) {
        if (previous->key.tag == pair.key.tag &&
            previous->key.vtype == pair.key.vtype &&
            previous->key.scalar == pair.key.scalar &&
            previous->key.null == pair.key.null &&
            previous->key.kind == pair.key.kind &&
            previous->key.items.size() == pair.key.items.size() &&
            previous->key.pairs.size() == pair.key.pairs.size()) {
          return Fail("duplicate key: " + pair.key.scalar);
        }
      }
      seen.push_back(&pair);
    }
    return true;
  }

  bool OnEvent(SerializationEvent&& event) {
    switch (event.kind) {
      case SerializationEventKind::kDocStart:
        have_root = false;
        return true;
      case SerializationEventKind::kDocEnd:
        if (!stack.empty()) {
          return Fail("container remains open at document end");
        }
        if (!have_root) {
          root = transcriber::Node{};
          root.null = true;
        }
        result.docs.push_back(std::move(root));
        root = transcriber::Node{};
        have_root = false;
        anchors.clear();
        return true;
      case SerializationEventKind::kNode: {
        if (event.node.kind == SerializationNode::Kind::kAlias) {
          const auto iterator = anchors.find(event.node.alias);
          if (iterator == anchors.end()) {
            return Fail("unresolved alias *" + event.node.alias);
          }
          transcriber::Node alias = iterator->second;
          return Attach(std::move(alias));
        }
        if (!ValidateNodeTag(event.node, result.error)) {
          failed = true;
          return false;
        }
        transcriber::Node representation;
        representation.kind = transcriber::Node::Kind::kScalar;
        representation.null = event.node.null;
        representation.tag = std::move(event.node.tag);
        representation.vtype = event.node.value_type;
        representation.scalar = std::move(event.node.scalar);
        if (!event.node.anchor.empty()) {
          anchors[event.node.anchor] = representation;
        }
        return Attach(std::move(representation));
      }
      case SerializationEventKind::kMapStart:
      case SerializationEventKind::kSeqStart: {
        if (!ValidateNodeTag(event.node, result.error)) {
          failed = true;
          return false;
        }
        Build build;
        build.node.kind = event.kind == SerializationEventKind::kMapStart
                              ? transcriber::Node::Kind::kMapping
                              : transcriber::Node::Kind::kSequence;
        build.node.tag = std::move(event.node.tag);
        build.anchor = std::move(event.node.anchor);
        stack.push_back(std::move(build));
        return true;
      }
      case SerializationEventKind::kMapEnd:
      case SerializationEventKind::kSeqEnd: {
        if (stack.empty()) {
          return Fail("container closes without a matching start");
        }
        Build build = std::move(stack.back());
        stack.pop_back();
        if (build.has_key) {
          return Fail("mapping key has no value");
        }
        if (build.node.kind == transcriber::Node::Kind::kMapping &&
            !ValidateKeys(build.node)) {
          return false;
        }
        if (!build.anchor.empty()) {
          anchors[build.anchor] = build.node;
        }
        return Attach(std::move(build.node));
      }
    }
    return Fail("unknown serialization event");
  }

  ComposeOptions options;
  ComposeResult result;
  std::map<std::string, transcriber::Node> anchors;
  std::vector<Build> stack;
  transcriber::Node root;
  bool have_root = false;
  bool failed = false;
};

ComposeEventBuilder::ComposeEventBuilder(const ComposeOptions& options)
    : impl_(new Impl(options)) {}

ComposeEventBuilder::~ComposeEventBuilder() { delete impl_; }

bool ComposeEventBuilder::OnEvent(SerializationEvent&& event) {
  return impl_->OnEvent(std::move(event));
}

ComposeResult ComposeEventBuilder::Finish() {
  if (!impl_->failed && !impl_->stack.empty()) {
    impl_->Fail("container remains open at stream end");
  }
  if (!impl_->failed) {
    impl_->result.ok = true;
  }
  return std::move(impl_->result);
}

ComposeResult Compose(const SerializationStream& serialization,
                      const ComposeOptions& options) {
  ComposeResult result;
  std::map<std::string, transcriber::Node> anchors;
  for (const SerializationNode& document : serialization.documents) {
    transcriber::Node representation;
    if (!ComposeNode(document, anchors, representation,
                     options.allow_duplicate_keys, result.error)) {
      return result;
    }
    result.docs.push_back(std::move(representation));
    anchors.clear();
  }
  result.ok = true;
  return result;
}

}  // namespace bedrock::archive::yaml
