/**
 * @file serialization.h
 * @brief YAML Parse와 Present 사이의 serialization tree 모델.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "archive/node.h"

namespace bedrock::archive::yaml {

/** @brief Presentation 정보가 제거된 YAML serialization tree 노드. */
struct SerializationNode {
  struct Pair;

  enum class Kind : std::uint8_t {
    kScalar,
    kSequence,
    kMapping,
    kAlias,
  };

  Kind kind = Kind::kScalar;
  bool null = false;
  std::string tag;
  std::string anchor;
  std::string alias;
  transcriber::ValueType value_type = transcriber::ValueType::kNull;
  std::string scalar;
  std::vector<SerializationNode> items;
  std::vector<Pair> pairs;

  [[nodiscard]] const SerializationNode* Find(std::string_view key) const;
};

struct SerializationNode::Pair {
  SerializationNode key;
  SerializationNode value;
};

/** @brief 문서 순서를 보존하는 YAML serialization stream. */
struct SerializationStream {
  std::vector<SerializationNode> documents;
};

/** @brief presentation decoding 뒤의 serialization event 종류. */
enum class SerializationEventKind : std::uint8_t {
  kDocStart,
  kDocEnd,
  kMapStart,
  kMapEnd,
  kSeqStart,
  kSeqEnd,
  kNode,
};

/** @brief tree materialization 없이 전달하는 serialization 단계 event. */
struct SerializationEvent {
  SerializationEventKind kind = SerializationEventKind::kDocStart;
  SerializationNode node;
};

/** @brief serialization event를 순서대로 소비하는 내부 sink. */
class SerializationSink {
 public:
  virtual ~SerializationSink() = default;
  virtual bool OnEvent(SerializationEvent&& event) = 0;
};

}  // namespace bedrock::archive::yaml
