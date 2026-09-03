/**
 * @file archive_roundtrip_test.cc
 * @brief Schema/Visitor 기반 YAML 직렬화·역직렬화 왕복 단위 테스트.
 *
 * 불리언, 부호 있는/없는 8~64비트 정수, float/double, 전체 유니코드
 * 범위(U+0000~U+10FFFF) 문자열, 바이트 벡터, variant 벡터, map, 중첩
 * 구조체까지 갖춘 Data를 극값으로 채워 YAMLSerializer로 쓰고
 * YAMLDeserializer로 새 객체에 읽어 들여 전 필드를 비교한다.
 *
 * 추가로 레이어 분리(Represent/Present + 이름 기반 Construct)가 주는
 * 성질들을 검증한다: 필드 순서 무관 로드, 모르는 키 무시, 시퀀스 안의
 * 매핑, 임의 문자열 맵 키(공백/콜론/비ASCII), 파스 오류의 행:열 보고,
 * 없는 필드의 kNoENT 보고.
 */
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "archive_test_status.h"
#include "common/archive.h"
#include "common/archive/transcriber/yaml_deserializer.h"
#include "common/archive/transcriber/yaml_serializer.h"

namespace {

using bedrock::archive::Schema;
using bedrock::archive::Visitor;
using bedrock::archive::transcriber::YAMLDeserializer;
using bedrock::archive::transcriber::YAMLSerializer;

/**
 * @brief U+0000~U+10FFFF(서로게이트 제외) 전 코드포인트를 UTF-8로
 *        직접 인코딩한 문자열을 만든다.
 *
 * C++ 컴파일러의 이스케이프 해석에 기대지 않고 바이트를 손수 조립해,
 * YAML 문자열 스칼라가 유니코드 전 범위를 올바르게 왕복하는지
 * 검증할 입력을 제공한다.
 * @return 조립된 UTF-8 바이트열.
 */
std::string AllUnicodeUtf8() {
  std::string serialized;
  for (char32_t code_point = 0; code_point <= 0x10FFFF; ++code_point) {
    if (code_point >= 0xD800 && code_point <= 0xDFFF) {
      continue;  // 서로게이트: 유효 스칼라 아님
    }
    if (code_point <= 0x7F) {
      serialized.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
      serialized.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
      serialized.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
      serialized.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
      serialized.push_back(
          static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
      serialized.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
      serialized.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
      serialized.push_back(
          static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
      serialized.push_back(
          static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
      serialized.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
  }
  return serialized;
}

/**
 * @brief Data에 중첩되는 하위 스키마.
 *
 * 최상위 Data와 동일하게 불리언부터 map까지 모든 스칼라·컨테이너
 * 타입을 한 벌 더 갖춘다. 각 필드는 Accept에서 대응하는 의미 기반 이름으로
 * Visitor에 등록된다.
 */
struct Nested : public Schema {
  bool boolean_value{};
  std::int8_t signed_8{};
  std::uint8_t unsigned_8{};
  std::int16_t signed_16{};
  std::uint16_t unsigned_16{};
  std::int32_t signed_32{};
  std::uint32_t unsigned_32{};
  std::int64_t signed_64{};
  std::uint64_t unsigned_64{};

  float float_value{};
  double double_value{};

  std::string unicode_text;
  std::string escaped_text;
  std::vector<std::byte> binary_data;

  std::vector<std::variant<std::string, std::vector<std::byte>>> text_or_bytes;

  std::map<std::string, std::int32_t> score_by_name;

  void Accept(Visitor& visitor) final {
    visitor.Visit("boolean_value", boolean_value)
        .Visit("signed_8", signed_8)
        .Visit("unsigned_8", unsigned_8)
        .Visit("signed_16", signed_16)
        .Visit("unsigned_16", unsigned_16)
        .Visit("signed_32", signed_32)
        .Visit("unsigned_32", unsigned_32)
        .Visit("signed_64", signed_64)
        .Visit("unsigned_64", unsigned_64)
        .Visit("float_value", float_value)
        .Visit("double_value", double_value)
        .Visit("unicode_text", unicode_text)
        .Visit("escaped_text", escaped_text)
        .Visit("binary_data", binary_data)
        .Visit("text_or_bytes", text_or_bytes)
        .Visit("score_by_name", score_by_name);
  }
};

/**
 * @brief 이 테스트의 최상위 (역)직렬화 대상 스키마.
 *
 * 스칼라·바이트 벡터 필드에 더해 Nested 하위 객체(nested_object)를
 * 함께 가져, 중첩 구조체 (역)직렬화까지 한 번에 검증한다.
 */
struct Data : public Schema {
  bool boolean_value{};
  std::int8_t signed_8{};
  std::uint8_t unsigned_8{};
  std::int16_t signed_16{};
  std::uint16_t unsigned_16{};
  std::int32_t signed_32{};
  std::uint32_t unsigned_32{};
  std::int64_t signed_64{};
  std::uint64_t unsigned_64{};

  float float_value{};
  double double_value{};

  std::string unicode_text;
  std::string escaped_text;
  std::vector<std::byte> binary_data;

  Nested nested_object;

  void Accept(Visitor& visitor) final {
    visitor.Visit("boolean_value", boolean_value)
        .Visit("signed_8", signed_8)
        .Visit("unsigned_8", unsigned_8)
        .Visit("signed_16", signed_16)
        .Visit("unsigned_16", unsigned_16)
        .Visit("signed_32", signed_32)
        .Visit("unsigned_32", unsigned_32)
        .Visit("signed_64", signed_64)
        .Visit("unsigned_64", unsigned_64)
        .Visit("float_value", float_value)
        .Visit("double_value", double_value)
        .Visit("unicode_text", unicode_text)
        .Visit("escaped_text", escaped_text)
        .Visit("binary_data", binary_data)
        .Visit("nested_object", nested_object);
  }
};

/** @brief Nested의 전 필드가 같은지 검증한다. */
void ExpectNestedEq(const Nested& actual, const Nested& expected) {
  EXPECT_EQ(actual.boolean_value, expected.boolean_value);
  EXPECT_EQ(actual.signed_8, expected.signed_8);
  EXPECT_EQ(actual.unsigned_8, expected.unsigned_8);
  EXPECT_EQ(actual.signed_16, expected.signed_16);
  EXPECT_EQ(actual.unsigned_16, expected.unsigned_16);
  EXPECT_EQ(actual.signed_32, expected.signed_32);
  EXPECT_EQ(actual.unsigned_32, expected.unsigned_32);
  EXPECT_EQ(actual.signed_64, expected.signed_64);
  EXPECT_EQ(actual.unsigned_64, expected.unsigned_64);
  EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.float_value),
            std::bit_cast<std::uint32_t>(expected.float_value));
  EXPECT_EQ(std::bit_cast<std::uint64_t>(actual.double_value),
            std::bit_cast<std::uint64_t>(expected.double_value));
  EXPECT_EQ(actual.unicode_text, expected.unicode_text);
  EXPECT_EQ(actual.escaped_text, expected.escaped_text);
  EXPECT_EQ(actual.binary_data, expected.binary_data);
  EXPECT_EQ(actual.text_or_bytes, expected.text_or_bytes);
  EXPECT_EQ(actual.score_by_name, expected.score_by_name);
}

/** @brief Data의 전 필드(중첩 포함)가 같은지 검증한다. */
void ExpectDataEq(const Data& actual, const Data& expected) {
  EXPECT_EQ(actual.boolean_value, expected.boolean_value);
  EXPECT_EQ(actual.signed_8, expected.signed_8);
  EXPECT_EQ(actual.unsigned_8, expected.unsigned_8);
  EXPECT_EQ(actual.signed_16, expected.signed_16);
  EXPECT_EQ(actual.unsigned_16, expected.unsigned_16);
  EXPECT_EQ(actual.signed_32, expected.signed_32);
  EXPECT_EQ(actual.unsigned_32, expected.unsigned_32);
  EXPECT_EQ(actual.signed_64, expected.signed_64);
  EXPECT_EQ(actual.unsigned_64, expected.unsigned_64);
  EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.float_value),
            std::bit_cast<std::uint32_t>(expected.float_value));
  EXPECT_EQ(std::bit_cast<std::uint64_t>(actual.double_value),
            std::bit_cast<std::uint64_t>(expected.double_value));
  EXPECT_EQ(actual.unicode_text, expected.unicode_text);
  EXPECT_EQ(actual.escaped_text, expected.escaped_text);
  EXPECT_EQ(actual.binary_data, expected.binary_data);
  ExpectNestedEq(actual.nested_object, expected.nested_object);
}

/** @brief 스키마를 YAML로 파일에 직렬화한다(산출물 .yaml 보존). */
template <typename T>
void SerializeToFile(const std::string& path, T& value) {
  YAMLSerializer serializer(0);
  ASSERT_TRUE(serializer.Dump(value).Ok());
  const std::string_view output = serializer.Output();
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  ofs.write(output.data(), static_cast<std::streamsize>(output.size()));
}

/** @brief YAML 파일을 스키마로 역직렬화한다(상태를 반환). */
template <typename T>
test_support::OwnedStatus DeserializeFromFile(const std::string& path,
                                              T& value) {
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  const std::streamsize size = ifs.tellg();
  std::string input(static_cast<std::size_t>(size), '\0');
  ifs.seekg(0, std::ios::beg);
  ifs.read(input.data(), size);
  YAMLDeserializer deserializer(0, input);
  return test_support::CopyStatus(deserializer.Load(value));
}

/** @brief 손수 만든 YAML 문자열을 파일로 쓴다(파싱 입력 보존). */
void WriteTextFile(const std::string& path, const std::string& content) {
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
}

TEST(ArchiveRepresentDumpLoadConstruct, AllScalarAndContainerTypes) {
  Data original;
  original.nested_object.boolean_value = true;
  original.nested_object.signed_8 = std::numeric_limits<std::int8_t>::max();
  original.nested_object.unsigned_8 = std::numeric_limits<std::uint8_t>::max();
  original.nested_object.signed_16 = std::numeric_limits<std::int16_t>::max();
  original.nested_object.unsigned_16 =
      std::numeric_limits<std::uint16_t>::max();
  original.nested_object.signed_32 = std::numeric_limits<std::int32_t>::max();
  original.nested_object.unsigned_32 =
      std::numeric_limits<std::uint32_t>::max();
  original.nested_object.signed_64 = std::numeric_limits<std::int64_t>::max();
  original.nested_object.unsigned_64 =
      std::numeric_limits<std::uint64_t>::max();
  original.nested_object.float_value = std::numeric_limits<float>::max();
  original.nested_object.double_value = std::numeric_limits<double>::max();
  // 긴 문자열: 모든 유니코드 스칼라 값을 담아 이스케이프 전수 테스트
  original.nested_object.unicode_text = AllUnicodeUtf8();

  // 짧은 문자열: 백슬래시·따옴표·제어문자·멀티바이트를 한 번에 검증
  std::string escaped_sample = "\\\"";  // 백슬래시, 따옴표
  escaped_sample.push_back('\x07');     // BEL(제어) -> \x07
  escaped_sample += "©";  // U+00A9(멀티바이트, 인쇄 가능) -> 그대로
  original.nested_object.escaped_text = escaped_sample;

  std::vector<std::byte> all_byte_values;
  all_byte_values.reserve(256);
  for (int byte_value = 0; byte_value < 256; ++byte_value) {
    all_byte_values.push_back(static_cast<std::byte>(byte_value));
  }
  original.nested_object.binary_data = all_byte_values;
  original.nested_object.text_or_bytes.emplace_back(escaped_sample);
  original.nested_object.text_or_bytes.emplace_back(AllUnicodeUtf8());
  original.nested_object.text_or_bytes.emplace_back(all_byte_values);

  original.nested_object.score_by_name = {
      {"alice", 90}, {"bob", -7}, {"charlie", 2147483647}};

  original.boolean_value = true;
  original.signed_8 = std::numeric_limits<std::int8_t>::min();
  original.unsigned_8 = std::numeric_limits<std::uint8_t>::min();
  original.signed_16 = std::numeric_limits<std::int16_t>::min();
  original.unsigned_16 = std::numeric_limits<std::uint16_t>::min();
  original.signed_32 = std::numeric_limits<std::int32_t>::min();
  original.unsigned_32 = std::numeric_limits<std::uint32_t>::min();
  original.signed_64 = std::numeric_limits<std::int64_t>::min();
  original.unsigned_64 = std::numeric_limits<std::uint64_t>::min();
  original.float_value = std::numeric_limits<float>::min();
  original.double_value = std::numeric_limits<double>::min();
  original.unicode_text = AllUnicodeUtf8();
  original.escaped_text = escaped_sample;
  original.binary_data = all_byte_values;

  SerializeToFile("archive_all_types.yaml", original);

  Data decoded{};
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_all_types.yaml", decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(original, decoded);
}

/** @brief 순서/모르는 키 테스트용 소형 스키마. */
struct Player : public Schema {
  std::int32_t hp = 0;
  std::string name;
  void Accept(Visitor& visitor) final {
    visitor.Visit("hp", hp).Visit("name", name);
  }
};

TEST(ArchiveRepresentDumpLoadConstruct,
     FieldOrderIndependentAndUnknownKeysIgnored) {
  // Accept 순서(hp -> name)와 반대로 쓰고, 모르는 키를 끼워 넣는다
  const std::string yaml =
      "name: \"bob\"\n"
      "unknown_key: whatever\n"
      "hp: 7\n";
  WriteTextFile("archive_field_order.yaml", yaml);
  Player mapping;
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_field_order.yaml", mapping);
  ASSERT_TRUE(status.Ok()) << status.Message();
  EXPECT_EQ(mapping.hp, 7);
  EXPECT_EQ(mapping.name, "bob");
}

/** @brief 시퀀스 안의 매핑(컨테이너-in-시퀀스) 테스트용 스키마. */
struct MappingRows : public Schema {
  std::vector<std::map<std::string, std::int32_t>> rows;
  void Accept(Visitor& visitor) final { visitor.Visit("rows", rows); }
};

TEST(ArchiveRepresentDumpLoadConstruct, SequenceOfMappings) {
  MappingRows original;
  original.rows = {{{"a", 1}, {"b", 2}}, {{"c", 3}}};
  SerializeToFile("archive_seq_of_maps.yaml", original);
  MappingRows decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_seq_of_maps.yaml", decoded);
  ASSERT_TRUE(status.Ok()) << status.Message()
                           << " (see archive_seq_of_maps.yaml)";
  EXPECT_EQ(original.rows, decoded.rows);
}

/** @brief 대형 scalar 없이 작은 node를 대량 생성하는 stress schema. */
struct NodeHeavyData : public Schema {
  std::vector<std::map<std::string, std::int32_t>> nodes;

  void Accept(Visitor& visitor) final { visitor.Visit("nodes", nodes); }
};

TEST(ArchiveRepresentDumpLoadConstruct, ManySmallNodesRoundTrip) {
  constexpr std::size_t kNodeCount = 50'000;
  NodeHeavyData original;
  original.nodes.reserve(kNodeCount);
  for (std::size_t index = 0; index < kNodeCount; ++index) {
    original.nodes.push_back(
        {{"active", index % 2 == 0 ? 1 : 0},
         {"id", static_cast<std::int32_t>(index)},
         {"label_hash", static_cast<std::int32_t>(index * 31U)}});
  }

  SerializeToFile("archive_node_heavy.yaml", original);

  NodeHeavyData decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_node_heavy.yaml", decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  EXPECT_EQ(decoded.nodes, original.nodes);
}

/** @brief 임의 문자열 키 맵 테스트용 스키마. */
struct StringKeyMap : public Schema {
  std::map<std::string, std::int32_t> mapping;
  void Accept(Visitor& visitor) final { visitor.Visit("m", mapping); }
};

TEST(ArchiveRepresentDumpLoadConstruct, ArbitraryMapKeysAreQuoted) {
  StringKeyMap original;
  original.mapping = {{"has space", 1},
                      {"colon: inside", 2},
                      {"한글 키", 3},
                      {"- leading dash", 4}};
  SerializeToFile("archive_map_keys.yaml", original);
  StringKeyMap decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_map_keys.yaml", decoded);
  ASSERT_TRUE(status.Ok()) << status.Message()
                           << " (see archive_map_keys.yaml)";
  EXPECT_EQ(original.mapping, decoded.mapping);
}

/** @brief 집합(set/unordered_set) 테스트용 스키마. */
struct SetCollection : public Schema {
  std::set<std::string> tags;
  std::unordered_set<std::int32_t> ids;
  void Accept(Visitor& visitor) final {
    visitor.Visit("tags", tags).Visit("ids", ids);
  }
};

TEST(ArchiveRepresentDumpLoadConstruct, SetsRoundTripAsSequences) {
  SetCollection original;
  original.tags = {"alpha", "beta", "감마"};
  original.ids = {7, -3, 2026};
  SerializeToFile("archive_sets.yaml", original);
  SetCollection decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_sets.yaml", decoded);
  ASSERT_TRUE(status.Ok()) << status.Message() << " (see archive_sets.yaml)";
  EXPECT_EQ(original.tags, decoded.tags);
  EXPECT_EQ(original.ids, decoded.ids);
}

/** @brief 중복 키(multimap) 테스트용 스키마. */
struct DuplicateKeyMap : public Schema {
  std::multimap<std::string, std::int32_t> mapping;
  void Accept(Visitor& visitor) final { visitor.Visit("mapping", mapping); }
};

TEST(ArchiveRepresentDumpLoadConstruct, MultimapKeepsDuplicateKeys) {
  DuplicateKeyMap original;
  original.mapping = {{"k", 1},
                      {"k", 2},  // 표현 트리가 중복 키와 순서를 보존
                      {"x", 9}};
  SerializeToFile("archive_multimap.yaml", original);
  DuplicateKeyMap decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_multimap.yaml", decoded);
  ASSERT_TRUE(status.Ok()) << status.Message()
                           << " (see archive_multimap.yaml)";
  EXPECT_EQ(original.mapping, decoded.mapping);
}

TEST(ArchiveRepresentDumpLoadConstruct, ParseErrorReportsLocation) {
  WriteTextFile("archive_parse_error.yaml",
                "hp: [1\nname: x\n");  // 닫히지 않은 [
  Player mapping;
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_parse_error.yaml", mapping);
  ASSERT_TRUE(status.Failed());
  // 오류 메시지에 행:열 위치("YAML L:C")가 담겨야 한다
  EXPECT_NE(status.Message().find("YAML"), std::string::npos)
      << status.Message();
}

TEST(ArchiveRepresentDumpLoadConstruct, MissingFieldReportsName) {
  WriteTextFile("archive_missing_field.yaml", "hp: 1\n");  // name 없음
  Player mapping;
  const test_support::OwnedStatus status =
      DeserializeFromFile("archive_missing_field.yaml", mapping);
  ASSERT_TRUE(status.Failed());
  EXPECT_NE(status.Message().find("name"), std::string::npos)
      << status.Message();
}

}  // namespace
