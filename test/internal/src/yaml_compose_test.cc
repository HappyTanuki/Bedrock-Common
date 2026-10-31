/**
 * @file yaml_compose_test.cc
 * @brief YAML serialization -> representation 조립(Compose) 단위 테스트.
 *
 * 파스+조립을 거친 트리의 구조·스칼라 값(이스케이프 해석, 따옴표 축약,
 * 접힘, chomping, 태그, 앵커/별칭 해소, null 판정)을 확인한다.
 */
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "archive/node.h"
#include "archive/yaml/compose.h"
#include "archive/yaml/grammar.h"
#include "archive/yaml/parse.h"

namespace {

using bedrock::archive::transcriber::Node;

bool LoadFirst(std::u32string_view presentation, Node& representation) {
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(presentation);
  if (!parsed.ok) {
    return false;
  }
  bedrock::archive::yaml::ComposeResult loaded =
      bedrock::archive::yaml::Compose(parsed.serialization);
  if (!loaded.ok || loaded.docs.empty()) {
    return false;
  }
  representation = std::move(loaded.docs.front());
  return true;
}

using bedrock::archive::yaml::ComposeResult;
using bedrock::archive::yaml::SerializationNode;
using bedrock::archive::yaml::SerializationStream;

void ExpectScalar(const Node& node, std::string_view scalar) {
  EXPECT_EQ(node.kind, Node::Kind::kScalar);
  EXPECT_FALSE(node.null);
  EXPECT_EQ(node.scalar, scalar);
}

void ExpectMappingScalar(const Node& mapping, std::string_view key,
                         std::string_view scalar) {
  ASSERT_EQ(mapping.kind, Node::Kind::kMapping);
  const Node* value = mapping.Find(key);
  ASSERT_NE(value, nullptr) << key;
  ExpectScalar(*value, scalar);
}

TEST(YamlCompose, CompositionResultsMatchExpectedCharacterization) {
  using PayloadCheck = void (*)(const ComposeResult&);
  struct Input {
    const char* name;
    std::u32string_view presentation;
    bool expected_ok;
    std::string_view expected_error;
    std::size_t expected_docs;
    PayloadCheck check_payload;
  };
  const Input inputs[] = {
      {"plain_mapping", U"a: 1\nb: two\n", true, "", 1,
       [](const ComposeResult& result) {
         const Node& document = result.docs.front();
         EXPECT_EQ(document.pairs.size(), 2U);
         ExpectMappingScalar(document, "a", "1");
         ExpectMappingScalar(document, "b", "two");
       }},
      {"nested_sequence_mapping",
       U"root:\n  - a: 1\n  - b:\n      - x\n      - y\n", true, "", 1,
       [](const ComposeResult& result) {
         const Node* root = result.docs.front().Find("root");
         ASSERT_NE(root, nullptr);
         ASSERT_EQ(root->kind, Node::Kind::kSequence);
         ASSERT_EQ(root->items.size(), 2U);
         ExpectMappingScalar(root->items[0], "a", "1");
         const Node* values = root->items[1].Find("b");
         ASSERT_NE(values, nullptr);
         ASSERT_EQ(values->kind, Node::Kind::kSequence);
         ASSERT_EQ(values->items.size(), 2U);
         ExpectScalar(values->items[0], "x");
         ExpectScalar(values->items[1], "y");
       }},
      {"multiple_documents", U"---\na: 1\n---\nb: [2, 3]\n", true, "", 2,
       [](const ComposeResult& result) {
         ExpectMappingScalar(result.docs[0], "a", "1");
         const Node* values = result.docs[1].Find("b");
         ASSERT_NE(values, nullptr);
         ASSERT_EQ(values->kind, Node::Kind::kSequence);
         ASSERT_EQ(values->items.size(), 2U);
         ExpectScalar(values->items[0], "2");
         ExpectScalar(values->items[1], "3");
       }},
      {"anchor_alias", U"x: &a hi\ny: *a\n", true, "", 1,
       [](const ComposeResult& result) {
         const Node* anchor = result.docs.front().Find("x");
         const Node* alias = result.docs.front().Find("y");
         ASSERT_NE(anchor, nullptr);
         ASSERT_NE(alias, nullptr);
         ExpectScalar(*anchor, "hi");
         ExpectScalar(*alias, "hi");
         EXPECT_TRUE(anchor->SameIdentity(*alias));
       }},
      {"alias_from_previous_document", U"---\nx: &a hi\n---\ny: *a\n", false,
       "unresolved alias *a", 1,
       [](const ComposeResult& result) {
         ExpectMappingScalar(result.docs.front(), "x", "hi");
       }},
      {"duplicate_key", U"a: one\na: two\n", false, "duplicate key: a", 0,
       nullptr},
      {"pairs_normalization", U"!!pairs\n- a: one\n- a: two\n", true, "", 1,
       [](const ComposeResult& result) {
         const Node& pairs = result.docs.front();
         ASSERT_EQ(pairs.kind, Node::Kind::kMapping);
         EXPECT_TRUE(pairs.duplicate_keys);
         ASSERT_EQ(pairs.pairs.size(), 2U);
         EXPECT_EQ(pairs.pairs[0].key.scalar, "a");
         ExpectScalar(pairs.pairs[0].value, "one");
         EXPECT_EQ(pairs.pairs[1].key.scalar, "a");
         ExpectScalar(pairs.pairs[1].value, "two");
       }},
      {"pairs_invalid_entry", U"!!pairs\n- a: one\n  b: two\n", false,
       "each !!pairs entry must be a single-pair mapping", 0, nullptr},
      {"integer_tag_on_collection", U"!!int [1, 2]\n", false,
       "tag !!int is not allowed on a collection", 0, nullptr},
      {"invalid_binary", U"!!binary \"not-base64!\"\n", false,
       "!!binary scalar is not valid base64: not-base64!", 0, nullptr},
      {"empty_document", U"---\n...\n", true, "", 1,
       [](const ComposeResult& result) {
         EXPECT_EQ(result.docs.front().kind, Node::Kind::kScalar);
         EXPECT_TRUE(result.docs.front().null);
       }},
      // event path는 map close의 중복 키 검사 전에 자식 노드를 검증한다.
      {"duplicate_key_with_invalid_binary",
       U"a: !!binary \"not-base64!\"\na: x\n", false,
       "!!binary scalar is not valid base64: not-base64!", 0, nullptr},
      {"alias_keys", U"&k a: 1\n*k : 2\n", false, "duplicate key: a", 0,
       nullptr},
  };

  for (const Input& input : inputs) {
    SCOPED_TRACE(input.name);
    const bedrock::archive::yaml::ParseResult parsed =
        bedrock::archive::yaml::Parse(input.presentation);
    ASSERT_TRUE(parsed.ok) << parsed.error;

    const ComposeResult result =
        bedrock::archive::yaml::Compose(parsed.serialization);
    EXPECT_EQ(result.ok, input.expected_ok);
    EXPECT_EQ(result.error, input.expected_error);
    ASSERT_EQ(result.docs.size(), input.expected_docs);
    if (input.check_payload != nullptr) {
      input.check_payload(result);
    }
  }
}

TEST(YamlCompose, PreservesPayloadOnSequenceAndMappingNodes) {
  SerializationStream serialization;
  SerializationNode sequence;
  sequence.kind = SerializationNode::Kind::kSequence;
  sequence.null = true;
  sequence.value_type = bedrock::archive::transcriber::ValueType::kSequence;
  sequence.scalar = "sequence payload";

  SerializationNode mapping;
  mapping.kind = SerializationNode::Kind::kMapping;
  mapping.null = true;
  mapping.value_type = bedrock::archive::transcriber::ValueType::kMapping;
  mapping.scalar = "mapping payload";
  SerializationNode key;
  key.value_type = bedrock::archive::transcriber::ValueType::kString;
  key.scalar = "key";
  SerializationNode value;
  value.scalar = "value";
  mapping.pairs.push_back({std::move(key), std::move(value)});
  sequence.items.push_back(std::move(mapping));
  serialization.documents.push_back(std::move(sequence));

  const ComposeResult composed = bedrock::archive::yaml::Compose(serialization);
  ASSERT_TRUE(composed.ok) << composed.error;
  ASSERT_EQ(composed.docs.size(), 1U);
  const Node& root = composed.docs.front();
  EXPECT_EQ(root.kind, Node::Kind::kSequence);
  EXPECT_TRUE(root.null);
  EXPECT_EQ(root.vtype, bedrock::archive::transcriber::ValueType::kSequence);
  EXPECT_EQ(root.scalar, "sequence payload");
  ASSERT_EQ(root.items.size(), 1U);
  const Node& child = root.items.front();
  EXPECT_EQ(child.kind, Node::Kind::kMapping);
  EXPECT_TRUE(child.null);
  EXPECT_EQ(child.vtype, bedrock::archive::transcriber::ValueType::kMapping);
  EXPECT_EQ(child.scalar, "mapping payload");
  ASSERT_EQ(child.pairs.size(), 1U);
  EXPECT_EQ(child.pairs.front().key.scalar, "key");
  EXPECT_EQ(child.pairs.front().value.scalar, "value");
}

TEST(YamlCompose, BasicStructureAndLookup) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"a: 1\nlist:\n  - x\n  - \"y z\"\n", decoded));
  EXPECT_EQ(decoded.kind, Node::Kind::kMapping);
  const Node* actual = decoded.Find("a");
  ASSERT_NE(actual, nullptr);
  EXPECT_EQ(actual->scalar, "1");
  const Node* list = decoded.Find("list");
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->kind, Node::Kind::kSequence);
  ASSERT_EQ(list->items.size(), 2U);
  EXPECT_EQ(list->items[0].scalar, "x");
  EXPECT_EQ(list->items[1].scalar, "y z");
  EXPECT_EQ(decoded.Find("missing"), nullptr);
}

TEST(YamlCompose, DoubleQuotedEscapes) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"s: \"a\\tb\\u0041\\\\\"\n", decoded));
  const Node* scalar = decoded.Find("s");
  ASSERT_NE(scalar, nullptr);
  EXPECT_EQ(scalar->scalar, "a\tbA\\");
}

TEST(YamlCompose, BinaryLiteralKeepsTag) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"b: !!binary |\n  QUJD\n  REVG\n", decoded));
  const Node* bytes = decoded.Find("b");
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(bytes->tag, "!!binary");
  EXPECT_EQ(bytes->scalar, "QUJD\nREVG\n");
}

TEST(YamlCompose, AliasResolvesToAnchorValue) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"x: &a hi\ny: *a\n", decoded));
  const Node* yaml_text = decoded.Find("y");
  ASSERT_NE(yaml_text, nullptr);
  EXPECT_EQ(yaml_text->scalar, "hi");
}

TEST(YamlCompose, NullVersusEmptyString) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"k:\ne: \"\"\n", decoded));
  const Node* key = decoded.Find("k");
  ASSERT_NE(key, nullptr);
  EXPECT_TRUE(key->null);
  const Node* event = decoded.Find("e");
  ASSERT_NE(event, nullptr);
  EXPECT_FALSE(event->null);
  EXPECT_TRUE(event->scalar.empty());
}

TEST(YamlCompose, SingleQuoteUnescape) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"s: 'it''s'\n", decoded));
  const Node* scalar = decoded.Find("s");
  ASSERT_NE(scalar, nullptr);
  EXPECT_EQ(scalar->scalar, "it's");
}

TEST(YamlCompose, FoldedBlock) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"f: >\n  a\n  b\n\n  c\n", decoded));
  const Node* field = decoded.Find("f");
  ASSERT_NE(field, nullptr);
  EXPECT_EQ(field->scalar, "a b\nc\n");
}

TEST(YamlCompose, PlainMultilineFolds) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"p: a\n b\n", decoded));
  const Node* pair = decoded.Find("p");
  ASSERT_NE(pair, nullptr);
  EXPECT_EQ(pair->scalar, "a b");
}

TEST(YamlCompose, DoubleQuotedMultilineFolds) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"d: \"a\n b\"\n", decoded));
  const Node* value = decoded.Find("d");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->scalar, "a b");
}

TEST(YamlCompose, DoubleQuotedFoldingPreservesEscapeSemantics) {
  Node decoded;
  ASSERT_TRUE(
      LoadFirst(U"trims: \"a  \n  b\"\n"
                U"joins: \"a\\\n  b\"\n"
                U"escaped_space: \"a\\ \n b\"\n"
                U"empty: \"a\n\n b\"\n",
                decoded));

  ASSERT_NE(decoded.Find("trims"), nullptr);
  EXPECT_EQ(decoded.Find("trims")->scalar, "a b");
  ASSERT_NE(decoded.Find("joins"), nullptr);
  EXPECT_EQ(decoded.Find("joins")->scalar, "ab");
  ASSERT_NE(decoded.Find("escaped_space"), nullptr);
  EXPECT_EQ(decoded.Find("escaped_space")->scalar, "a  b");
  ASSERT_NE(decoded.Find("empty"), nullptr);
  EXPECT_EQ(decoded.Find("empty")->scalar, "a\nb");
}

TEST(YamlCompose, ChompingKeepAndStrip) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"k: |+\n a\n\n\n", decoded));
  const Node* key = decoded.Find("k");
  ASSERT_NE(key, nullptr);
  EXPECT_EQ(key->scalar, "a\n\n\n");

  Node second_decoded;
  ASSERT_TRUE(LoadFirst(U"k: |-\n  a\n", second_decoded));
  const Node* second_key = second_decoded.Find("k");
  ASSERT_NE(second_key, nullptr);
  EXPECT_EQ(second_key->scalar, "a");
}

TEST(YamlCompose, SequenceOfCompactMappings) {
  Node decoded;
  ASSERT_TRUE(LoadFirst(U"- a: 1\n  b: 2\n- c: 3\n", decoded));
  ASSERT_EQ(decoded.kind, Node::Kind::kSequence);
  ASSERT_EQ(decoded.items.size(), 2U);
  const Node* bytes = decoded.items[0].Find("b");
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(bytes->scalar, "2");
}

TEST(YamlCompose, UnresolvedAliasFails) {
  Node decoded;
  EXPECT_FALSE(LoadFirst(U"y: *nope\n", decoded));
}

}  // namespace
