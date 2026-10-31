/**
 * @file serialize.cc
 * @brief YAML representation graph를 serialization tree로 Serialize한다.
 */
#include "archive/yaml/serialize.h"

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include "archive/node.h"
#include "archive/transcriber.h"

namespace bedrock::archive::yaml {

SerializationSink::~SerializationSink() = default;
namespace {


void CountReferences(const transcriber::Node& representation,
                     std::map<std::uint64_t, std::size_t>& reference_counts,
                     std::set<std::uint64_t>& expanded) {
  const std::uint64_t identity = static_cast<std::uint64_t>(representation.identity);
  ++reference_counts[identity];
  if (!expanded.insert(identity).second) {
    return;
  }
  for (const transcriber::Node& item : representation.items) {
    CountReferences(item, reference_counts, expanded);
  }
  for (const transcriber::Node::Pair& pair : representation.pairs) {
    CountReferences(pair.key, reference_counts, expanded);
    CountReferences(pair.value, reference_counts, expanded);
  }
}

SerializationNode SerializeNode(
    const transcriber::Node& representation,
    const std::map<std::uint64_t, std::size_t>& reference_counts,
    std::map<std::uint64_t, std::string>& anchors, std::size_t& next_anchor) {
  const std::uint64_t identity = static_cast<std::uint64_t>(representation.identity);
  const auto count = reference_counts.find(identity);
  const bool shared =
      count != reference_counts.end() && count->second > std::size_t{1};
  if (shared) {
    const auto existing = anchors.find(identity);
    if (existing != anchors.end()) {
      SerializationNode alias;
      alias.kind = SerializationNode::Kind::kAlias;
      alias.alias = existing->second;
      return alias;
    }
  }

  SerializationNode serialized;
  if (shared) {
    serialized.anchor = "id" + std::to_string(next_anchor++);
    anchors.emplace(identity, serialized.anchor);
  }
  serialized.null = representation.null;
  serialized.tag = representation.tag;
  serialized.value_type = representation.vtype;
  serialized.scalar = representation.scalar;
  if (representation.kind == transcriber::Node::Kind::kScalar) {
    serialized.kind = SerializationNode::Kind::kScalar;
  } else if (representation.kind == transcriber::Node::Kind::kMapping) {
    if (representation.duplicate_keys) {
      serialized.kind = SerializationNode::Kind::kSequence;
      serialized.tag = "!!pairs";
      for (const transcriber::Node::Pair& pair : representation.pairs) {
        SerializationNode item;
        item.kind = SerializationNode::Kind::kMapping;
        item.pairs.push_back(
            {SerializeNode(pair.key, reference_counts, anchors, next_anchor),
             SerializeNode(pair.value, reference_counts, anchors,
                           next_anchor)});
        serialized.items.push_back(std::move(item));
      }
    } else {
      serialized.kind = SerializationNode::Kind::kMapping;
      for (const transcriber::Node::Pair& pair : representation.pairs) {
        serialized.pairs.push_back(
            {SerializeNode(pair.key, reference_counts, anchors, next_anchor),
             SerializeNode(pair.value, reference_counts, anchors,
                           next_anchor)});
      }
    }
  } else {
    serialized.kind = SerializationNode::Kind::kSequence;
    for (const transcriber::Node& item : representation.items) {
      serialized.items.push_back(
          SerializeNode(item, reference_counts, anchors, next_anchor));
    }
  }
  return serialized;
}

}  // namespace

const SerializationNode* SerializationNode::Find(std::string_view key) const {
  for (const Pair& pair : pairs) {
    if (pair.key.kind == Kind::kScalar && !pair.key.null &&
        pair.key.scalar == key) {
      return &pair.value;
    }
  }
  return nullptr;
}

SerializationStream Serialize(const transcriber::Node& representation) {
  SerializationStream serialization;
  serialization.identity_error = transcriber::FindIdentityError(representation);
  if (serialization.identity_error < 0) return serialization;
  std::map<std::uint64_t, std::size_t> reference_counts;
  std::set<std::uint64_t> expanded;
  CountReferences(representation, reference_counts, expanded);

  std::map<std::uint64_t, std::string> anchors;
  std::size_t next_anchor = 1;
  serialization.documents.push_back(
      SerializeNode(representation, reference_counts, anchors, next_anchor));
  return serialization;
}

}  // namespace bedrock::archive::yaml
