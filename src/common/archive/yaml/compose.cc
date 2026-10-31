/**
 * @file compose.cc
 * @brief YAML serialization tree를 representation으로 Compose한다.
 */
#include "archive/yaml/compose.h"

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
  if (serialized.tag == "!!pairs" &&
      serialized.kind != SerializationNode::Kind::kSequence) {
    error = "tag !!pairs requires a sequence";
    return false;
  }
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
 * @brief YAML !!pairs sequence를 duplicate-key mapping representation으로
 * 낮춘다.
 *
 * tag:yaml.org,2002:pairs는 한 항목짜리 mapping들의 sequence이며 같은 키를
 * 여러 번 가질 수 있다. 일반 YAML mapping의 key uniqueness는 완화하지 않는다.
 */
bool NormalizePairs(transcriber::Node& node, std::string& error) {
  if (node.tag != "!!pairs") {
    return true;
  }
  if (node.kind != transcriber::Node::Kind::kSequence) {
    error = "tag !!pairs requires a sequence";
    return false;
  }

  std::vector<transcriber::Node::Pair> pairs;
  pairs.reserve(node.items.size());
  for (transcriber::Node& item : node.items) {
    if (item.kind != transcriber::Node::Kind::kMapping ||
        item.pairs.size() != 1U) {
      error = "each !!pairs entry must be a single-pair mapping";
      return false;
    }
    pairs.push_back(std::move(item.pairs.front()));
  }

  node.kind = transcriber::Node::Kind::kMapping;
  node.duplicate_keys = true;
  node.items.clear();
  node.pairs = std::move(pairs);
  return true;
}

// Parse가 내보내는 것과 같은 event로 materialized tree를 재생한다.
bool ReplayNode(const SerializationNode& serialized, SerializationSink& sink) {
  SerializationEvent event;
  event.node.kind = serialized.kind;
  event.node.null = serialized.null;
  event.node.tag = serialized.tag;
  event.node.anchor = serialized.anchor;
  event.node.value_type = serialized.value_type;
  event.node.scalar = serialized.scalar;

  switch (serialized.kind) {
    case SerializationNode::Kind::kScalar:
      event.kind = SerializationEventKind::kNode;
      return sink.OnEvent(std::move(event));
    case SerializationNode::Kind::kAlias:
      event.kind = SerializationEventKind::kNode;
      event.node.alias = serialized.alias;
      return sink.OnEvent(std::move(event));
    case SerializationNode::Kind::kSequence:
      event.kind = SerializationEventKind::kSeqStart;
      if (!sink.OnEvent(std::move(event))) {
        return false;
      }
      for (const SerializationNode& item : serialized.items) {
        if (!ReplayNode(item, sink)) {
          return false;
        }
      }
      event = SerializationEvent{};
      event.kind = SerializationEventKind::kSeqEnd;
      return sink.OnEvent(std::move(event));
    case SerializationNode::Kind::kMapping:
      event.kind = SerializationEventKind::kMapStart;
      if (!sink.OnEvent(std::move(event))) {
        return false;
      }
      for (const SerializationNode::Pair& pair : serialized.pairs) {
        if (!ReplayNode(pair.key, sink) || !ReplayNode(pair.value, sink)) {
          return false;
        }
      }
      event = SerializationEvent{};
      event.kind = SerializationEventKind::kMapEnd;
      return sink.OnEvent(std::move(event));
  }
  return false;
}

}  // namespace

bool ComposeEventBuilder::Fail(std::string message) {
  result.error = std::move(message);
  failed = true;
  return false;
}

bool ComposeEventBuilder::Attach(transcriber::Node&& node) {
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

bool ComposeEventBuilder::ValidateKeys(const transcriber::Node& mapping) {
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

ComposeEventBuilder::ComposeEventBuilder() = default;

ComposeEventBuilder::~ComposeEventBuilder() = default;

bool ComposeEventBuilder::OnEvent(SerializationEvent&& event) {
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
      build.node.null = event.node.null;
      build.node.vtype = event.node.value_type;
      build.node.scalar = std::move(event.node.scalar);
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
      if (!NormalizePairs(build.node, result.error)) {
        failed = true;
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

ComposeResult ComposeEventBuilder::Finish() {
  if (!failed && !stack.empty()) {
    Fail("container remains open at stream end");
  }
  if (!failed) {
    for (const auto& doc : result.docs) {
      const std::int64_t identity_error = transcriber::FindIdentityError(doc);
      if (identity_error < 0) {
        result.identity_error = identity_error;
        result.docs.clear();
        Fail("Snowflake identity error");
        break;
      }
    }
  }
  if (!failed) {
    result.ok = true;
  }
  return std::move(result);
}

ComposeResult Compose(const SerializationStream& serialization) {
  ComposeResult result;
  if (serialization.identity_error < 0) {
    result.identity_error = serialization.identity_error;
    result.error = "Snowflake identity error";
    return result;
  }

  ComposeEventBuilder builder;
  for (const SerializationNode& document : serialization.documents) {
    SerializationEvent event;
    event.kind = SerializationEventKind::kDocStart;
    if (!builder.OnEvent(std::move(event)) || !ReplayNode(document, builder)) {
      break;
    }
    event = SerializationEvent{};
    event.kind = SerializationEventKind::kDocEnd;
    if (!builder.OnEvent(std::move(event))) {
      break;
    }
  }
  return builder.Finish();
}

}  // namespace bedrock::archive::yaml
