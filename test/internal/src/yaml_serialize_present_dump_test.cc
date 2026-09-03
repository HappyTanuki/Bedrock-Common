#include <gtest/gtest.h>

#include <string>

#include "archive/yaml/dump.h"
#include "archive/yaml/load.h"
#include "archive/yaml/present.h"
#include "archive/yaml/serialize.h"

namespace {

TEST(YamlSerializePresentDump, DumpIsSerializeThenPresent) {
  const bedrock::archive::yaml::ComposeResult loaded =
      bedrock::archive::yaml::Load(U"value: \"text\"\n");
  ASSERT_TRUE(loaded.ok) << loaded.error;
  ASSERT_EQ(loaded.docs.size(), 1U);

  const bedrock::archive::yaml::SerializationStream serialized =
      bedrock::archive::yaml::Serialize(loaded.docs.front());
  const bedrock::archive::yaml::PresentResult presented =
      bedrock::archive::yaml::Present(serialized);
  const bedrock::archive::yaml::PresentResult dumped =
      bedrock::archive::yaml::Dump(loaded.docs.front());

  ASSERT_TRUE(presented.ok) << presented.error;
  ASSERT_TRUE(dumped.ok) << dumped.error;
  EXPECT_EQ(dumped.text, presented.text);
  EXPECT_EQ(dumped.text, "value: \"text\"\n");
}

TEST(YamlSerializePresentDump, SerializeTurnsSharedIdentityIntoAnchorAndAlias) {
  const bedrock::archive::yaml::ComposeResult loaded =
      bedrock::archive::yaml::Load(
          U"original: &source value\nalias: *source\n");
  ASSERT_TRUE(loaded.ok) << loaded.error;

  const bedrock::archive::yaml::SerializationStream serialized =
      bedrock::archive::yaml::Serialize(loaded.docs.front());
  const bedrock::archive::yaml::SerializationNode* original =
      serialized.documents.front().Find("original");
  const bedrock::archive::yaml::SerializationNode* alias =
      serialized.documents.front().Find("alias");
  ASSERT_NE(original, nullptr);
  ASSERT_NE(alias, nullptr);
  EXPECT_FALSE(original->anchor.empty());
  EXPECT_EQ(alias->kind,
            bedrock::archive::yaml::SerializationNode::Kind::kAlias);
  EXPECT_EQ(alias->alias, original->anchor);

  const bedrock::archive::yaml::PresentResult presented =
      bedrock::archive::yaml::Present(serialized);
  ASSERT_TRUE(presented.ok) << presented.error;
  const bedrock::archive::yaml::ComposeResult reloaded =
      bedrock::archive::yaml::Load(
          std::u32string(presented.text.begin(), presented.text.end()));
  ASSERT_TRUE(reloaded.ok) << reloaded.error;
  ASSERT_NE(reloaded.docs.front().Find("original"), nullptr);
  ASSERT_NE(reloaded.docs.front().Find("alias"), nullptr);
  EXPECT_TRUE(reloaded.docs.front().Find("original")->SameIdentity(
      *reloaded.docs.front().Find("alias")));
}

}  // namespace
