#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "archive/yaml/compose.h"
#include "archive/yaml/parse.h"
#include "archive/yaml/present.h"
#include "archive/yaml/serialize.h"

namespace {

class RecordingSerializationSink final
    : public bedrock::archive::yaml::SerializationSink {
 public:
  bool OnEvent(bedrock::archive::yaml::SerializationEvent&& event) final {
    kinds.push_back(event.kind);
    return true;
  }

  std::vector<bedrock::archive::yaml::SerializationEventKind> kinds;
};

TEST(YamlParseComposeLoad, ParseStreamsSerializationEventsToSink) {
  const std::u32string input = U"a: [1, 2]\n";
  const std::vector<std::uint32_t> presentation(input.begin(), input.end());
  RecordingSerializationSink sink;
  std::string error;

  ASSERT_TRUE(bedrock::archive::yaml::Parse(
      std::span<const std::uint32_t>(presentation), sink, error))
      << error;
  EXPECT_EQ(sink.kinds,
            (std::vector<bedrock::archive::yaml::SerializationEventKind>{
                bedrock::archive::yaml::SerializationEventKind::kDocStart,
                bedrock::archive::yaml::SerializationEventKind::kMapStart,
                bedrock::archive::yaml::SerializationEventKind::kNode,
                bedrock::archive::yaml::SerializationEventKind::kSeqStart,
                bedrock::archive::yaml::SerializationEventKind::kNode,
                bedrock::archive::yaml::SerializationEventKind::kNode,
                bedrock::archive::yaml::SerializationEventKind::kSeqEnd,
                bedrock::archive::yaml::SerializationEventKind::kMapEnd,
                bedrock::archive::yaml::SerializationEventKind::kDocEnd}));
}

TEST(YamlParseComposeLoad, ParseReportsMalformedInputToSinkCaller) {
  const std::u32string input = U"a: [1, 2\n";
  const std::vector<std::uint32_t> presentation(input.begin(), input.end());
  RecordingSerializationSink sink;
  std::string error;
  const bedrock::archive::yaml::ParseResult tree_result =
      bedrock::archive::yaml::Parse(
          std::span<const std::uint32_t>(presentation));
  ASSERT_FALSE(tree_result.ok);
  EXPECT_FALSE(bedrock::archive::yaml::Parse(
      std::span<const std::uint32_t>(presentation), sink, error));
  EXPECT_EQ(error, "YAML parse failed at 2:1");
  EXPECT_EQ(error, tree_result.error);
  EXPECT_TRUE(sink.kinds.empty());
}

TEST(YamlParseComposeLoad, ParseRemovesPresentationDetailsBeforeCompose) {
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(U"plain: a\nquoted: \"a\"\n");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  ASSERT_EQ(parsed.serialization.documents.size(), 1U);

  const bedrock::archive::yaml::SerializationNode* plain =
      parsed.serialization.documents.front().Find("plain");
  const bedrock::archive::yaml::SerializationNode* quoted =
      parsed.serialization.documents.front().Find("quoted");
  ASSERT_NE(plain, nullptr);
  ASSERT_NE(quoted, nullptr);
  EXPECT_EQ(plain->scalar, "a");
  EXPECT_EQ(quoted->scalar, "a");
}

TEST(YamlParseComposeLoad, ParseThenComposeDecodesScalar) {
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(U"value: \"a\\tb\"\n");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  const bedrock::archive::yaml::ComposeResult composed =
      bedrock::archive::yaml::Compose(parsed.serialization);
  ASSERT_TRUE(composed.ok) << composed.error;

  ASSERT_EQ(composed.docs.size(), 1U);
  ASSERT_NE(composed.docs.front().Find("value"), nullptr);
  EXPECT_EQ(composed.docs.front().Find("value")->scalar, "a\tb");
}

TEST(YamlParseComposeLoad, ParsePreservesAliasesUntilCompose) {
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(U"original: &item value\nalias: *item\n");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  const bedrock::archive::yaml::SerializationNode* alias =
      parsed.serialization.documents.front().Find("alias");
  ASSERT_NE(alias, nullptr);
  EXPECT_EQ(alias->kind,
            bedrock::archive::yaml::SerializationNode::Kind::kAlias);
  EXPECT_EQ(alias->alias, "item");

  const bedrock::archive::yaml::ComposeResult composed =
      bedrock::archive::yaml::Compose(parsed.serialization);
  ASSERT_TRUE(composed.ok) << composed.error;
  ASSERT_NE(composed.docs.front().Find("original"), nullptr);
  ASSERT_NE(composed.docs.front().Find("alias"), nullptr);
  EXPECT_TRUE(composed.docs.front()
                  .Find("original")
                  ->SameIdentity(*composed.docs.front().Find("alias")));
  EXPECT_EQ(composed.docs.front().Find("alias")->scalar, "value");
}

namespace {

bedrock::archive::yaml::ComposeResult LoadText(
    std::u32string_view presentation) {
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(presentation);
  if (!parsed.ok) {
    return {false, parsed.error, {}};
  }
  return bedrock::archive::yaml::Compose(parsed.serialization);
}

bedrock::archive::yaml::ComposeResult LoadText(const char* presentation) {
  const std::u32string text(presentation, presentation + strlen(presentation));
  return LoadText(text);
}

bedrock::archive::yaml::PresentResult PresentRepresentations(
    std::vector<bedrock::archive::transcriber::Node> representations) {
  bedrock::archive::yaml::SerializationStream serialization;
  serialization.documents.reserve(representations.size());
  for (const bedrock::archive::transcriber::Node& representation :
       representations) {
    bedrock::archive::yaml::SerializationStream document =
        bedrock::archive::yaml::Serialize(representation);
    serialization.documents.push_back(std::move(document.documents.front()));
  }
  return bedrock::archive::yaml::Present(std::move(serialization));
}

}  // namespace

TEST(YamlParseComposeLoad, ComposeRejectsTagOnCollection) {
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(U"value: !!int [1, 2]\n");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  const bedrock::archive::yaml::SerializationNode* value =
      parsed.serialization.documents.front().Find("value");
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->tag, "!!int");
  const bedrock::archive::yaml::ComposeResult loaded =
      LoadText("value: !!int [1, 2]\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_NE(loaded.error.find("!!int"), std::string::npos) << loaded.error;
}

TEST(YamlParseComposeLoad, ComposeRejectsBinaryTagWithoutBase64) {
  const bedrock::archive::yaml::ComposeResult loaded =
      LoadText("blob: !!binary \"not base64!!\"\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_NE(loaded.error.find("base64"), std::string::npos) << loaded.error;
}

TEST(YamlParseComposeLoad, ComposeAcceptsBinaryTagWithBase64) {
  const bedrock::archive::yaml::ComposeResult loaded =
      LoadText("blob: !!binary QUJD\n");
  ASSERT_TRUE(loaded.ok) << loaded.error;
  const bedrock::archive::transcriber::Node* blob =
      loaded.docs.front().Find("blob");
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob->tag, "!!binary");
  EXPECT_EQ(blob->scalar, "QUJD");
}

TEST(YamlParseComposeLoad, ComposeRejectsScalarOnlyTagOnMapping) {
  const bedrock::archive::yaml::ComposeResult loaded =
      LoadText("value: !!bool {a: 1}\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_NE(loaded.error.find("!!bool"), std::string::npos) << loaded.error;
}

TEST(YamlParseComposeLoad, ComposeAcceptsCollectionTagsOnCollections) {
  ASSERT_TRUE(LoadText("list: !!seq [1]\n").ok);
  ASSERT_TRUE(LoadText("map: !!map {a: 1}\n").ok);
}

TEST(YamlParseComposeLoad, ComposeKeepsUnknownAndVerbatimTagsOnScalars) {
  const bedrock::archive::yaml::ComposeResult unknown =
      LoadText("a: !my-tag v\n");
  ASSERT_TRUE(unknown.ok) << unknown.error;
  EXPECT_EQ(unknown.docs.front().Find("a")->tag, "!my-tag");

  const bedrock::archive::yaml::ComposeResult verbatim =
      LoadText("b: !<tag:example.com,2000:app/x> v\n");
  ASSERT_TRUE(verbatim.ok) << verbatim.error;
  EXPECT_NE(verbatim.docs.front().Find("b")->tag.find("app/x"),
            std::string::npos);
}

TEST(YamlParseComposeLoad, ComposeRejectsDuplicateMappingKeys) {
  const bedrock::archive::yaml::ComposeResult loaded = LoadText("a: 1\na: 2\n");
  EXPECT_FALSE(loaded.ok);
  EXPECT_NE(loaded.error.find("duplicate key"), std::string::npos)
      << loaded.error;
}

TEST(YamlParseComposeLoad, ComposeNormalizesPairsToDuplicateMapping) {
  const bedrock::archive::yaml::ComposeResult loaded = LoadText(
      "pairs: !!pairs\n"
      "  - k: 1\n"
      "  - k: 2\n");
  ASSERT_TRUE(loaded.ok) << loaded.error;
  const bedrock::archive::transcriber::Node* pairs =
      loaded.docs.front().Find("pairs");
  ASSERT_NE(pairs, nullptr);
  EXPECT_EQ(pairs->kind, bedrock::archive::transcriber::Node::Kind::kMapping);
  ASSERT_EQ(pairs->pairs.size(), 2U);
  EXPECT_EQ(pairs->pairs[0].key.scalar, "k");
  EXPECT_EQ(pairs->pairs[1].key.scalar, "k");
}

TEST(YamlParseComposeLoad, ComposeAllowsDuplicateKeysInDifferentMappings) {
  const bedrock::archive::yaml::ComposeResult loaded = LoadText(
      "outer:\n"
      "  a: 1\n"
      "inner:\n"
      "  a: 2\n");
  ASSERT_TRUE(loaded.ok) << loaded.error;
}

TEST(YamlParseComposeLoad, ComposeTreatsDistinctScalarTypesAsDistinctKeys) {
  // "1"(string)과 1(int)은 스칼라 종류가 다른 별개 키다.
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(U"quoted: \"1\"\nplain: 1\n");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  const bedrock::archive::yaml::SerializationNode& document =
      parsed.serialization.documents.front();
  const bedrock::archive::yaml::SerializationNode* quoted =
      document.Find("quoted");
  const bedrock::archive::yaml::SerializationNode* plain =
      document.Find("plain");
  ASSERT_NE(quoted, nullptr);
  ASSERT_NE(plain, nullptr);
  EXPECT_EQ(quoted->value_type,
            bedrock::archive::transcriber::ValueType::kString);
  EXPECT_NE(plain->value_type,
            bedrock::archive::transcriber::ValueType::kString);
}

TEST(YamlParseComposeLoad, ParseSplitsMultiDocumentStream) {
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(U"first: 1\n---\nsecond: 2\n");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  ASSERT_EQ(parsed.serialization.documents.size(), 2U);
  ASSERT_NE(parsed.serialization.documents[0].Find("first"), nullptr);
  ASSERT_NE(parsed.serialization.documents[1].Find("second"), nullptr);
}

TEST(YamlParseComposeLoad, ComposeProducesOneNodePerDocument) {
  const bedrock::archive::yaml::ParseResult parsed =
      bedrock::archive::yaml::Parse(U"first: 1\n---\nsecond: 2\n");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  const bedrock::archive::yaml::ComposeResult composed =
      bedrock::archive::yaml::Compose(parsed.serialization);
  ASSERT_TRUE(composed.ok) << composed.error;
  ASSERT_EQ(composed.docs.size(), 2U);
  EXPECT_NE(composed.docs[0].Find("first"), nullptr);
  EXPECT_NE(composed.docs[1].Find("second"), nullptr);
}

TEST(YamlParseComposeLoad, LoadKeepsDocumentBoundariesAndAnchorScoping) {
  // 앵커는 문서 단위로 스코프가 끊긴다 — 두 문서에 같은 이름을 써도 무해.
  const bedrock::archive::yaml::ComposeResult loaded = LoadText(
      "a: &value 1\n"
      "---\n"
      "b: &value 2\n");
  ASSERT_TRUE(loaded.ok) << loaded.error;
  ASSERT_EQ(loaded.docs.size(), 2U);
}

TEST(YamlParseComposeLoad, DumpEmitsMultiDocumentStream) {
  const bedrock::archive::yaml::ComposeResult loaded =
      LoadText("first: 1\n---\nsecond: 2\n");
  ASSERT_TRUE(loaded.ok) << loaded.error;
  std::vector<bedrock::archive::transcriber::Node> docs = loaded.docs;

  const bedrock::archive::yaml::PresentResult dumped =
      PresentRepresentations(std::move(docs));
  ASSERT_TRUE(dumped.ok) << dumped.error;
  EXPECT_NE(dumped.text.find("---"), std::string::npos);

  const std::u32string text(dumped.text.begin(), dumped.text.end());
  const bedrock::archive::yaml::ComposeResult round_tripped = LoadText(text);
  ASSERT_TRUE(round_tripped.ok) << round_tripped.error;
  ASSERT_EQ(round_tripped.docs.size(), 2U);
  EXPECT_NE(round_tripped.docs[0].Find("first"), nullptr);
  EXPECT_NE(round_tripped.docs[1].Find("second"), nullptr);
}

TEST(YamlParseComposeLoad, BinaryRoundTripsThroughDumpAndLoad) {
  const bedrock::archive::yaml::ComposeResult first =
      LoadText("blob: !!binary |-\n  QUJD\n");
  ASSERT_TRUE(first.ok) << first.error;

  const bedrock::archive::yaml::PresentResult dumped =
      bedrock::archive::yaml::Present(
          bedrock::archive::yaml::Serialize(first.docs.front()));
  ASSERT_TRUE(dumped.ok) << dumped.error;
  EXPECT_NE(dumped.text.find("!!binary"), std::string::npos);

  const std::u32string text(dumped.text.begin(), dumped.text.end());
  const bedrock::archive::yaml::ComposeResult second = LoadText(text);
  ASSERT_TRUE(second.ok) << second.error;
  // base64 디코더는 줄바꿈을 무시하므로 블록 스칼라의 개행이 남아도 무해하다
  EXPECT_EQ(second.docs.front().Find("blob")->scalar, "QUJD\n");
}

}  // namespace
