/**
 * @file yaml_compose_test.cc
 * @brief YAML 이벤트 → 표현(Node) 트리 조립(Compose) 단위 테스트.
 *
 * 파스+조립을 거친 트리의 구조·스칼라 값(이스케이프 해석, 따옴표 축약,
 * 접힘, 청킹, 태그, 앵커/별칭 해소, null 판정)을 확인한다.
 */
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "common/archive/transcriber/node.h"
#include "common/archive/yaml/compose.h"
#include "common/archive/yaml/grammar.h"

namespace {

using bedrock::archive::transcriber::Node;
using bedrock::archive::yaml::Compose;
using bedrock::archive::yaml::ComposeResult;
using bedrock::archive::yaml::Cursor;
using bedrock::archive::yaml::Diag;
using bedrock::archive::yaml::Events;
using bedrock::archive::yaml::Grammar;

/** @brief 입력을 파스+조립해 첫 문서를 out에 담는다(실패 시 false). */
bool ParseDoc(std::u32string_view yaml, Node& out) {
  std::vector<std::uint32_t> buf(yaml.begin(), yaml.end());
  Events events;
  Diag diag;
  Cursor cur{
      std::span<const std::uint32_t>(buf.data(), static_cast<std::size_t>(0)),
      std::span<const std::uint32_t>(buf.data(), buf.size()), &events, &diag};
  static_cast<void>(Grammar::Isl_yaml_stream(cur));
  if (cur.before.size() != buf.size()) {
    return false;
  }
  ComposeResult r = Compose(buf, events.list);
  if (!r.ok || r.docs.empty()) {
    return false;
  }
  out = std::move(r.docs.front());
  return true;
}

TEST(YamlCompose, BasicStructureAndLookup) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"a: 1\nlist:\n  - x\n  - \"y z\"\n", d));
  EXPECT_EQ(d.kind, Node::Kind::kMapping);
  const Node* a = d.Find("a");
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->scalar, "1");
  const Node* list = d.Find("list");
  ASSERT_NE(list, nullptr);
  ASSERT_EQ(list->kind, Node::Kind::kSequence);
  ASSERT_EQ(list->items.size(), 2u);
  EXPECT_EQ(list->items[0].scalar, "x");
  EXPECT_EQ(list->items[1].scalar, "y z");
  EXPECT_EQ(d.Find("missing"), nullptr);
}

TEST(YamlCompose, DoubleQuotedEscapes) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"s: \"a\\tb\\u0041\\\\\"\n", d));
  const Node* s = d.Find("s");
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->scalar, "a\tbA\\");
}

TEST(YamlCompose, BinaryLiteralKeepsTag) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"b: !!binary |\n  QUJD\n  REVG\n", d));
  const Node* b = d.Find("b");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->tag, "!!binary");
  EXPECT_EQ(b->scalar, "QUJD\nREVG\n");
}

TEST(YamlCompose, AliasResolvesToAnchorValue) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"x: &a hi\ny: *a\n", d));
  const Node* y = d.Find("y");
  ASSERT_NE(y, nullptr);
  EXPECT_EQ(y->scalar, "hi");
}

TEST(YamlCompose, NullVersusEmptyString) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"k:\ne: \"\"\n", d));
  const Node* k = d.Find("k");
  ASSERT_NE(k, nullptr);
  EXPECT_TRUE(k->null);
  const Node* e = d.Find("e");
  ASSERT_NE(e, nullptr);
  EXPECT_FALSE(e->null);
  EXPECT_TRUE(e->scalar.empty());
}

TEST(YamlCompose, SingleQuoteUnescape) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"s: 'it''s'\n", d));
  const Node* s = d.Find("s");
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->scalar, "it's");
}

TEST(YamlCompose, FoldedBlock) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"f: >\n  a\n  b\n\n  c\n", d));
  const Node* f = d.Find("f");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->scalar, "a b\nc\n");
}

TEST(YamlCompose, PlainMultilineFolds) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"p: a\n b\n", d));
  const Node* p = d.Find("p");
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->scalar, "a b");
}

TEST(YamlCompose, DoubleQuotedMultilineFolds) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"d: \"a\n b\"\n", d));
  const Node* v = d.Find("d");
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(v->scalar, "a b");
}

TEST(YamlCompose, ChompingKeepAndStrip) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"k: |+\n a\n\n\n", d));
  const Node* k = d.Find("k");
  ASSERT_NE(k, nullptr);
  EXPECT_EQ(k->scalar, "a\n\n\n");

  Node d2;
  ASSERT_TRUE(ParseDoc(U"k: |-\n  a\n", d2));
  const Node* k2 = d2.Find("k");
  ASSERT_NE(k2, nullptr);
  EXPECT_EQ(k2->scalar, "a");
}

TEST(YamlCompose, SequenceOfCompactMappings) {
  Node d;
  ASSERT_TRUE(ParseDoc(U"- a: 1\n  b: 2\n- c: 3\n", d));
  ASSERT_EQ(d.kind, Node::Kind::kSequence);
  ASSERT_EQ(d.items.size(), 2u);
  const Node* b = d.items[0].Find("b");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->scalar, "2");
}

TEST(YamlCompose, UnresolvedAliasFails) {
  constexpr std::u32string_view yaml = U"y: *nope\n";
  std::vector<std::uint32_t> buf(yaml.begin(), yaml.end());
  Events events;
  Cursor cur{
      std::span<const std::uint32_t>(buf.data(), static_cast<std::size_t>(0)),
      std::span<const std::uint32_t>(buf.data(), buf.size()), &events,
      nullptr};
  static_cast<void>(Grammar::Isl_yaml_stream(cur));
  const ComposeResult r = Compose(buf, events.list);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error.empty());
}

}  // namespace
