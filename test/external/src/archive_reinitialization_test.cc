/**
 * @file archive_reinitialization_test.cc
 * @brief Public serializers own views and can be reinitialized.
 */
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "common/archive.h"
#include "common/archive/transcriber/rbf2_deserializer.h"
#include "common/archive/transcriber/rbf2_serializer.h"
#include "common/archive/transcriber/rbf_deserializer.h"
#include "common/archive/transcriber/rbf_serializer.h"
#include "common/archive/transcriber/rsf_deserializer.h"
#include "common/archive/transcriber/rsf_serializer.h"
#include "common/archive/transcriber/yaml_deserializer.h"
#include "common/archive/transcriber/yaml_serializer.h"

#if __has_include(                                                       \
    "common/archive/transcriber/text_serializer_session.h") ||           \
    __has_include(                                                       \
        "common/archive/transcriber/text_deserializer_session.h") ||     \
        __has_include(                                                   \
            "common/archive/transcriber/binary_serializer_session.h") || \
            __has_include(                                               \
                "common/archive/transcriber/binary_deserializer_session.h")
#error "Obsolete session templates must not remain in the public API"
#endif

namespace {

namespace transcriber = bedrock::archive::transcriber;

static_assert(
    std::is_same_v<transcriber::YAMLSerializer,
                   transcriber::TextSerializer<transcriber::YAMLFormat>>);
static_assert(
    std::is_same_v<transcriber::YAMLDeserializer,
                   transcriber::TextDeserializer<transcriber::YAMLFormat>>);
static_assert(
    std::is_same_v<transcriber::RsfSerializer,
                   transcriber::TextSerializer<transcriber::RsfFormat>>);
static_assert(
    std::is_same_v<transcriber::RsfDeserializer,
                   transcriber::TextDeserializer<transcriber::RsfFormat>>);
static_assert(
    std::is_same_v<transcriber::RbfSerializer,
                   transcriber::BinarySerializer<transcriber::RbfFormat>>);
static_assert(
    std::is_same_v<transcriber::RbfDeserializer,
                   transcriber::BinaryDeserializer<transcriber::RbfFormat>>);
static_assert(
    std::is_same_v<transcriber::Rbf2Serializer,
                   transcriber::BinarySerializer<transcriber::Rbf2Format>>);
static_assert(
    std::is_same_v<transcriber::Rbf2Deserializer,
                   transcriber::BinaryDeserializer<transcriber::Rbf2Format>>);

struct ArchiveData final : bedrock::archive::Schema {
  std::int32_t number = 0;
  std::string text;

  void Accept(bedrock::archive::Visitor& visitor) final {
    visitor.Visit("number", number).Visit("text", text);
  }
};

TEST(ArchiveReinitialization, SerializerResetStartsANewOperation) {
  ArchiveData first;
  first.number = 1;
  first.text = "first";

  bedrock::archive::transcriber::YAMLSerializer serializer(0);
  ASSERT_TRUE(serializer.Dump(first).Ok());
  const std::string first_output(serializer.Output());
  EXPECT_FALSE(serializer.Dump(first).Ok());

  serializer.Reset();

  ArchiveData second;
  second.number = 2;
  second.text = "second";
  ASSERT_TRUE(serializer.Dump(second).Ok());
  const std::string second_output(serializer.Output());

  EXPECT_NE(first_output, second_output);
}

TEST(ArchiveReinitialization, DeserializerInitializeStartsANewOperation) {
  bedrock::archive::transcriber::YAMLDeserializer deserializer(
      0, "number: 1\ntext: \"first\"\n");

  ArchiveData first;
  ASSERT_TRUE(deserializer.Load(first).Ok());
  EXPECT_EQ(first.number, 1);
  EXPECT_EQ(first.text, "first");
  EXPECT_FALSE(deserializer.Load(first).Ok());

  ASSERT_TRUE(deserializer.Initialize("number: 2\ntext: \"second\"\n").Ok());

  ArchiveData second;
  ASSERT_TRUE(deserializer.Load(second).Ok());
  EXPECT_EQ(second.number, 2);
  EXPECT_EQ(second.text, "second");
}

TEST(ArchiveReinitialization, RsfRestartsAfterExplicitReinitialization) {
  ArchiveData source;
  source.number = 3;
  source.text = "rsf";

  bedrock::archive::transcriber::RsfSerializer serializer(0);
  ASSERT_TRUE(serializer.Dump(source).Ok());
  const std::string first_output(serializer.Output());
  EXPECT_TRUE(serializer.Dump(source).Failed());
  serializer.Reset();
  ASSERT_TRUE(serializer.Dump(source).Ok());

  bedrock::archive::transcriber::RsfDeserializer deserializer(0, first_output);
  ArchiveData decoded;
  ASSERT_TRUE(deserializer.Load(decoded).Ok());
  EXPECT_TRUE(deserializer.Load(decoded).Failed());
  ASSERT_TRUE(deserializer.Initialize(first_output).Ok());
  ASSERT_TRUE(deserializer.Load(decoded).Ok());
  EXPECT_EQ(decoded.text, source.text);
}

template <typename Serializer, typename Deserializer>
void VerifyBinaryRestart() {
  ArchiveData source;
  source.number = 7;
  source.text = "binary";

  Serializer serializer(0);
  ASSERT_TRUE(serializer.Dump(source).Ok());
  const std::span<const std::byte> output = serializer.Output();
  const std::vector<std::byte> first_output(output.begin(), output.end());
  EXPECT_TRUE(serializer.Dump(source).Failed());
  serializer.Reset();
  ASSERT_TRUE(serializer.Dump(source).Ok());

  Deserializer deserializer(0, first_output);
  ArchiveData decoded;
  ASSERT_TRUE(deserializer.Load(decoded).Ok());
  EXPECT_TRUE(deserializer.Load(decoded).Failed());
  ASSERT_TRUE(deserializer.Initialize(first_output).Ok());
  ASSERT_TRUE(deserializer.Load(decoded).Ok());
  EXPECT_EQ(decoded.number, source.number);
  EXPECT_EQ(decoded.text, source.text);
}

TEST(ArchiveReinitialization, RbfRestartsAfterExplicitReinitialization) {
  VerifyBinaryRestart<bedrock::archive::transcriber::RbfSerializer,
                      bedrock::archive::transcriber::RbfDeserializer>();
}

TEST(ArchiveReinitialization, Rbf2RestartsAfterExplicitReinitialization) {
  VerifyBinaryRestart<bedrock::archive::transcriber::Rbf2Serializer,
                      bedrock::archive::transcriber::Rbf2Deserializer>();
}

}  // namespace
