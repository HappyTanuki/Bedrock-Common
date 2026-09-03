/**
 * @file rbf_roundtrip_test.cc
 * @brief 참조 바이너리 프로토콜(RBF) 왕복 검증 — YAML과 같은 선상의 포맷.
 *
 * RBF는 Serializer/Deserializer(Node 기반)를 상속해 Present/Parse 훅만
 * 구현하고 Represent/Construct는 공유한다. 전 스칼라·중첩·시퀀스·맵·집합·
 * variant·바이트 blob을 극값으로 채워 **파일에 쓰고 다시 읽어** 전 필드를
 * 비교한다(산출물 .bin 파일이 남아 실제 인코딩을 확인할 수 있다).
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
#include <variant>
#include <vector>

#include "archive_test_status.h"
#include "common/archive.h"
#include "common/archive/transcriber/rbf_deserializer.h"
#include "common/archive/transcriber/rbf_serializer.h"

namespace {

using bedrock::archive::Schema;
using bedrock::archive::Visitor;
using bedrock::archive::transcriber::RbfDeserializer;
using bedrock::archive::transcriber::RbfSerializer;

bool BitEq(float actual, float bytes) {
  return std::bit_cast<std::uint32_t>(actual) ==
         std::bit_cast<std::uint32_t>(bytes);
}
bool BitEq(double actual, double bytes) {
  return std::bit_cast<std::uint64_t>(actual) ==
         std::bit_cast<std::uint64_t>(bytes);
}

/** @brief 중첩 하위 스키마. */
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

/** @brief 최상위 스키마 — 전 스칼라 + 컨테이너 + variant + 중첩. */
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
    // 일부 필드는 Field{name, number} 형태 — RBF는 이름 키를 쓰므로 번호는
    // 무시되지만, protobuf 백엔드가 쓸 필드번호 저작이 컴파일됨을 함께 확인.
    value.Visit(bedrock::archive::Field{"b", 1}, bytes)
        .Visit(bedrock::archive::Field{"by", 2}, by)
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

void ExpectNestedEq(const Nested& actual, const Nested& bytes) {
  EXPECT_EQ(actual.actual, bytes.actual);
  EXPECT_EQ(actual.nums, bytes.nums);
  EXPECT_EQ(actual.table, bytes.table);
  EXPECT_EQ(actual.tags, bytes.tags);
}

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
  ExpectNestedEq(actual.nested, bytes.nested);
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

/** @brief 스키마를 RBF로 파일에 쓴다(산출물 보존). */
template <typename T>
void SerializeToFile(const std::string& path, T& value) {
  RbfSerializer serializer(0);
  ASSERT_TRUE(serializer.Dump(value).Ok());
  const std::span<const std::byte> output = serializer.Output();
  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  ofs.write(reinterpret_cast<const char*>(output.data()),
            static_cast<std::streamsize>(output.size()));
}

/** @brief RBF 파일을 스키마로 읽는다(상태 반환). */
template <typename T>
test_support::OwnedStatus DeserializeFromFile(const std::string& path,
                                              T& value) {
  std::ifstream ifs(path, std::ios::binary);
  const std::string input{std::istreambuf_iterator<char>(ifs),
                          std::istreambuf_iterator<char>()};
  const std::span<const std::byte> bytes(
      reinterpret_cast<const std::byte*>(input.data()), input.size());
  RbfDeserializer deserializer(0, bytes);
  return test_support::CopyStatus(deserializer.Load(value));
}

TEST(RbfRoundTrip, AllTypesStringVariant) {
  Data first_decoded;
  FillExtreme(first_decoded);
  first_decoded.var = std::string("variant-as-string");

  SerializeToFile("rbf_all_types.bin", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rbf_all_types.bin", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
  ASSERT_TRUE(std::holds_alternative<std::string>(second_decoded.var));
}

TEST(RbfRoundTrip, VariantSelectsBytesAlternative) {
  Data first_decoded;
  FillExtreme(first_decoded);
  std::vector<std::byte> payload;
  payload.reserve(10);
  for (int i = 0; i < 10; ++i) {
    payload.push_back(static_cast<std::byte>(i * 7));
  }
  first_decoded.var = payload;

  SerializeToFile("rbf_bytes_variant.bin", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rbf_bytes_variant.bin", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
  ASSERT_TRUE(
      std::holds_alternative<std::vector<std::byte>>(second_decoded.var));
  EXPECT_EQ(std::get<std::vector<std::byte>>(second_decoded.var), payload);
}

TEST(RbfRoundTrip, EmptyContainersRoundTrip) {
  Data first_decoded;  // 기본값 — 빈 컨테이너/문자열, variant는 기본(빈 string)
  SerializeToFile("rbf_empty.bin", first_decoded);

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rbf_empty.bin", second_decoded);
  ASSERT_TRUE(status.Ok()) << status.Message();
  ExpectDataEq(first_decoded, second_decoded);
}

TEST(RbfRoundTrip, TruncatedInputFailsGracefully) {
  Data first_decoded;
  FillExtreme(first_decoded);
  first_decoded.var = std::string("x");
  SerializeToFile("rbf_full.bin", first_decoded);

  // 파일을 읽어 뒷부분을 잘라 손상 파일을 만든다.
  std::string full;
  {
    std::ifstream ifs("rbf_full.bin", std::ios::binary);
    full.assign(std::istreambuf_iterator<char>(ifs),
                std::istreambuf_iterator<char>());
  }
  {
    std::ofstream ofs("rbf_truncated.bin", std::ios::binary | std::ios::trunc);
    ofs.write(full.data(), static_cast<std::streamsize>(full.size() / 2));
  }

  Data second_decoded;
  const test_support::OwnedStatus status =
      DeserializeFromFile("rbf_truncated.bin", second_decoded);
  EXPECT_TRUE(status.Failed());
}

}  // namespace
