/**
 * @file reference_string_roundtrip_test.cc
 * @brief RSF(Reference std::string Format, 자기서술 텍스트, 경로 A) 왕복 검증.
 *
 * RSF는 Serializer/Deserializer(Node 기반)를 상속해 Present/Parse+Compose
 * 훅만 구현하고 Represent/Construct는 공유한다(YAML/RBF와 같은 선상). 값마다
 * 타입 문자로 vtype를 보존하므로 variant 판별도 기존 Construct 그대로 동작한다.
 * 전 스칼라·중첩·시퀀스·맵·집합·variant·바이트 blob을 **파일에 쓰고 다시
 * 읽어** 비교한다(.rsf 산출물이 남아 실제 텍스트 인코딩을 확인할 수 있다).
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
#include "common/archive/transcriber/rsf_deserializer.h"
#include "common/archive/transcriber/rsf_serializer.h"

namespace {

using bedrock::archive::Schema;
using bedrock::archive::Visitor;
using bedrock::archive::transcriber::RsfDeserializer;
using bedrock::archive::transcriber::RsfSerializer;

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
    value.Visit("a", actual)
        .Visit("nums", nums)
        .Visit("table", table)
        .Visit("tags", tags);
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
    value.Visit("b", bytes)
        .Visit("by", by)
        .Visit("i8", i8)
        .Visit("u8", u8)
        .Visit("i16", i16)
        .Visit("u16", u16)
        .Visit("i32", i32)
        .Visit("u32", u32)
        .Visit("i64", i64)
        .Visit("u64", u64)
        .Visit("f32", f32)
        .Visit("f64", f64)
        .Visit("s", s)
        .Visit("blob", blob)
        .Visit("var", var)
        .Visit("nested", nested);
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
  decoded.s = "한글 mixed ascii \x01\x02\" \\";
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
  RsfSerializer serializer(0);
  ASSERT_TRUE(serializer.Dump(value).Ok());
  const std::string_view output = serializer.Output();
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  ofs.write(output.data(), static_cast<std::streamsize>(output.size()));
}

template <typename T>
test_support::OwnedStatus DeserializeFromFile(const std::string& path,
                                              T& value) {
  std::ifstream ifs(path, std::ios::binary);
  const std::string input{std::istreambuf_iterator<char>(ifs),
                          std::istreambuf_iterator<char>()};
  RsfDeserializer deserializer(0, input);
  return test_support::CopyStatus(deserializer.Load(value));
}

TEST(ReferenceStringRoundTrip, AllTypesStringVariant) {
  Data first_decoded;
  FillExtreme(first_decoded);
  first_decoded.var = std::string("variant-as-string");
  SerializeToFile("rsf_all_types.rsf", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rsf_all_types.rsf", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
  ASSERT_TRUE(std::holds_alternative<std::string>(second_decoded.var));
}

TEST(ReferenceStringRoundTrip, VariantSelectsBytesAlternative) {
  Data first_decoded;
  FillExtreme(first_decoded);
  std::vector<std::byte> payload;
  payload.reserve(10);
  for (int i = 0; i < 10; ++i) {
    payload.push_back(static_cast<std::byte>(i * 7));
  }
  first_decoded.var = payload;
  SerializeToFile("rsf_bytes_variant.rsf", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rsf_bytes_variant.rsf", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
  ASSERT_TRUE(
      std::holds_alternative<std::vector<std::byte>>(second_decoded.var));
}

TEST(ReferenceStringRoundTrip, EmptyContainers) {
  Data first_decoded;
  SerializeToFile("rsf_empty.rsf", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rsf_empty.rsf", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
}

TEST(ReferenceStringRoundTrip, GarbageInputFailsGracefully) {
  {
    std::ofstream ofs("rsf_garbage.rsf", std::ios::binary | std::ios::trunc);
    ofs << "{ not-a-valid-token ]";
  }
  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rsf_garbage.rsf", second_decoded);
  EXPECT_TRUE(status.Failed());
}

}  // namespace
