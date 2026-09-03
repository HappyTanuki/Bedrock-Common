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
#include <string_view>
#include <utility>
#include <vector>

#include "archive/node.h"
#include "archive/yaml/grammar.h"
#include "archive/yaml/load.h"

namespace {

using bedrock::archive::transcriber::Node;

bool LoadFirst(std::u32string_view presentation, Node& representation) {
  bedrock::archive::yaml::ComposeResult loaded =
      bedrock::archive::yaml::Load(presentation);
  if (!loaded.ok || loaded.docs.empty()) {
    return false;
  }
  representation = std::move(loaded.docs.front());
  return true;
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
