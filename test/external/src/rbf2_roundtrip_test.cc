/**
 * @file rbf2_roundtrip_test.cc
 * @brief RBF2(타입 소거 protobuf식, 경로 B) 왕복 검증.
 *
 * RBF2는 Visitor를 직접 상속(Node 우회)하고, 값에 타입 태그 없이 구조체
 * 필드에 번호만 접두한다. 전 스칼라·중첩·시퀀스·맵·집합·바이트 blob·variant를
 * 파일에 쓰고 다시 읽어 비교한다(.rbf2 산출물 보존). variant는 베이스의
 * OnVariantBegin/End 훅으로 활성 인덱스를 판별자로 실어 지원한다(타입 소거
 * 포맷은 trial 대신 판별자 기반 — 설계 문서 §8d findings).
 */
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "archive_test_status.h"
#include "common/archive.h"
#include "common/archive/transcriber/rbf2_deserializer.h"
#include "common/archive/transcriber/rbf2_serializer.h"

namespace {

using bedrock::archive::Schema;
using bedrock::archive::Visitor;
using bedrock::archive::transcriber::Rbf2Deserializer;
using bedrock::archive::transcriber::Rbf2Serializer;

bool BitEq(float actual, float bytes) {
  return std::bit_cast<std::uint32_t>(actual) ==
         std::bit_cast<std::uint32_t>(bytes);
}
bool BitEq(double actual, double bytes) {
  return std::bit_cast<std::uint64_t>(actual) ==
         std::bit_cast<std::uint64_t>(bytes);
}

struct Nested : public Schema {
  std::int32_t actual = 0;
  std::vector<std::int32_t> nums;
  std::map<std::string, std::int32_t> table;
  std::set<std::string> tags;
  void Accept(Visitor& value) final {
    value.Visit(bedrock::archive::Field{"a", 1}, actual)
        .Visit(bedrock::archive::Field{"nums", 2}, nums)
        .Visit(bedrock::archive::Field{"table", 3}, table)
        .Visit(bedrock::archive::Field{"tags", 4}, tags);
  }
};

struct Data : public Schema {
  bool bytes = false;
  std::byte by{};
  std::int8_t i8 = 0;
  std::uint8_t u8 = 0;
  std::int16_t i16 = 0;
  std::uint16_t u16 = 0;
  std::int32_t i32 = 0;
  std::uint32_t u32 = 0;
  std::int64_t i64 = 0;
  std::uint64_t u64 = 0;
  float f32 = 0.0F;
  double f64 = 0.0;
  std::string s;
  std::vector<std::byte> blob;
  std::variant<std::string, std::vector<std::byte>> var;
  Nested nested;

  void Accept(Visitor& value) final {
    value.Visit(bedrock::archive::Field{"b", 1}, bytes)
        .Visit(bedrock::archive::Field{"by", 2}, by)
        .Visit(bedrock::archive::Field{"i8", 3}, i8)
        .Visit(bedrock::archive::Field{"u8", 4}, u8)
        .Visit(bedrock::archive::Field{"i16", 5}, i16)
        .Visit(bedrock::archive::Field{"u16", 6}, u16)
        .Visit(bedrock::archive::Field{"i32", 7}, i32)
        .Visit(bedrock::archive::Field{"u32", 8}, u32)
        .Visit(bedrock::archive::Field{"i64", 9}, i64)
        .Visit(bedrock::archive::Field{"u64", 10}, u64)
        .Visit(bedrock::archive::Field{"f32", 11}, f32)
        .Visit(bedrock::archive::Field{"f64", 12}, f64)
        .Visit(bedrock::archive::Field{"s", 13}, s)
        .Visit(bedrock::archive::Field{"blob", 14}, blob)
        .Visit(bedrock::archive::Field{"var", 16}, var)
        .Visit(bedrock::archive::Field{"nested", 15}, nested);
  }
};

void ExpectDataEq(const Data& actual, const Data& bytes) {
  EXPECT_EQ(actual.bytes, bytes.bytes);
  EXPECT_EQ(actual.by, bytes.by);
  EXPECT_EQ(actual.i8, bytes.i8);
  EXPECT_EQ(actual.u8, bytes.u8);
  EXPECT_EQ(actual.i16, bytes.i16);
  EXPECT_EQ(actual.u16, bytes.u16);
  EXPECT_EQ(actual.i32, bytes.i32);
  EXPECT_EQ(actual.u32, bytes.u32);
  EXPECT_EQ(actual.i64, bytes.i64);
  EXPECT_EQ(actual.u64, bytes.u64);
  EXPECT_TRUE(BitEq(actual.f32, bytes.f32));
  EXPECT_TRUE(BitEq(actual.f64, bytes.f64));
  EXPECT_EQ(actual.s, bytes.s);
  EXPECT_EQ(actual.blob, bytes.blob);
  EXPECT_EQ(actual.var, bytes.var);
  EXPECT_EQ(actual.nested.actual, bytes.nested.actual);
  EXPECT_EQ(actual.nested.nums, bytes.nested.nums);
  EXPECT_EQ(actual.nested.table, bytes.nested.table);
  EXPECT_EQ(actual.nested.tags, bytes.nested.tags);
}

void FillExtreme(Data& decoded) {
  decoded.bytes = true;
  decoded.by = static_cast<std::byte>(0xAB);
  decoded.i8 = std::numeric_limits<std::int8_t>::min();
  decoded.u8 = std::numeric_limits<std::uint8_t>::max();
  decoded.i16 = std::numeric_limits<std::int16_t>::min();
  decoded.u16 = std::numeric_limits<std::uint16_t>::max();
  decoded.i32 = std::numeric_limits<std::int32_t>::min();
  decoded.u32 = std::numeric_limits<std::uint32_t>::max();
  decoded.i64 = std::numeric_limits<std::int64_t>::min();
  decoded.u64 = std::numeric_limits<std::uint64_t>::max();
  decoded.f32 = std::numeric_limits<float>::max();
  decoded.f64 = std::numeric_limits<double>::lowest();
  decoded.s = "한글 mixed ascii \x01\x02";
  for (int i = 0; i < 256; ++i) {
    decoded.blob.push_back(static_cast<std::byte>(i));
  }
  decoded.nested.actual = -12345;
  decoded.nested.nums = {1, -2, 3, std::numeric_limits<std::int32_t>::max()};
  decoded.nested.table = {{"alice", 90}, {"bob", -7}, {"공백 키", 1}};
  decoded.nested.tags = {"alpha", "beta", "감마"};
}

template <typename T>
void SerializeToFile(const std::string& path, T& value) {
  Rbf2Serializer serializer(0);
  ASSERT_TRUE(serializer.Dump(value).Ok());
  const std::span<const std::byte> output = serializer.Output();
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  ofs.write(reinterpret_cast<const char*>(output.data()),
            static_cast<std::streamsize>(output.size()));
}

template <typename T>
test_support::OwnedStatus DeserializeFromFile(const std::string& path,
                                              T& value) {
  std::ifstream ifs(path, std::ios::binary);
  const std::string input{std::istreambuf_iterator<char>(ifs),
                          std::istreambuf_iterator<char>()};
  const std::span<const std::byte> bytes(
      reinterpret_cast<const std::byte*>(input.data()), input.size());
  Rbf2Deserializer deserializer(0, bytes);
  return test_support::CopyStatus(deserializer.Load(value));
}

TEST(Rbf2RoundTrip, AllTypesStringVariant) {
  Data first_decoded;
  FillExtreme(first_decoded);
  first_decoded.var = std::string("variant-as-string");
  SerializeToFile("rbf2_all_types.rbf2", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rbf2_all_types.rbf2", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
  ASSERT_TRUE(std::holds_alternative<std::string>(second_decoded.var));
}

TEST(Rbf2RoundTrip, VariantSelectsBytesAlternative) {
  Data first_decoded;
  FillExtreme(first_decoded);
  std::vector<std::byte> payload;
  payload.reserve(10);
  for (int i = 0; i < 10; ++i) {
    payload.push_back(static_cast<std::byte>(i * 7));
  }
  first_decoded.var = payload;
  SerializeToFile("rbf2_bytes_variant.rbf2", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rbf2_bytes_variant.rbf2", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
  ASSERT_TRUE(
      std::holds_alternative<std::vector<std::byte>>(second_decoded.var));
}

TEST(Rbf2RoundTrip, EmptyContainers) {
  Data first_decoded;
  SerializeToFile("rbf2_empty.rbf2", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rbf2_empty.rbf2", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
}

TEST(Rbf2RoundTrip, TruncatedInputFailsGracefully) {
  Data first_decoded;
  FillExtreme(first_decoded);
  SerializeToFile("rbf2_full.rbf2", first_decoded);

  std::string full;
  {
    std::ifstream ifs("rbf2_full.rbf2", std::ios::binary);
    full.assign(std::istreambuf_iterator<char>(ifs),
                std::istreambuf_iterator<char>());
  }
  {
    std::ofstream ofs("rbf2_truncated.rbf2",
                      std::ios::binary | std::ios::trunc);
    ofs.write(full.data(), static_cast<std::streamsize>(full.size() / 2));
  }

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rbf2_truncated.rbf2", second_decoded);
  EXPECT_TRUE(status.Failed());
}

}  // namespace
