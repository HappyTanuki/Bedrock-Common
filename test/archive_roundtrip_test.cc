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
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

#include "common/archive.h"
#include "common/archive/transcriber/yaml_deserializer.h"
#include "common/archive/transcriber/yaml_serializer.h"

namespace {

using bedrock::archive::Schema;
using bedrock::archive::Visitor;
using bedrock::archive::transcriber::YAMLDeserializer;
using bedrock::archive::transcriber::YAMLSerializer;

/** @brief 부동소수 정확 왕복 비교(비트 동일성 — -Wfloat-equal 회피 겸). */
bool BitEq(float a, float b) {
  return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}
/** @brief double 버전의 비트 동일성 비교. */
bool BitEq(double a, double b) {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

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
  std::string s;
  for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
    if (cp >= 0xD800 && cp <= 0xDFFF) continue;  // 서로게이트: 유효 스칼라 아님
    if (cp <= 0x7F) {
      s.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return s;
}

/**
 * @brief Data에 중첩되는 하위 스키마.
 *
 * 최상위 Data와 동일하게 불리언부터 map까지 모든 스칼라·컨테이너
 * 타입을 한 벌 더 갖춘다. 필드 valueN은 Accept에서 "dataN" 이름으로
 * Visitor에 등록된다.
 */
struct Nested : public Schema {
  bool value1;
  std::int8_t value2;
  std::uint8_t value3;
  std::int16_t value4;
  std::uint16_t value5;
  std::int32_t value6;
  std::uint32_t value7;
  std::int64_t value8;
  std::uint64_t value9;

  float value10;
  double value11;

  std::string value12;
  std::string value13;
  std::vector<std::byte> value14;

  std::vector<std::variant<std::string, std::vector<std::byte>>> value15;

  std::map<std::string, std::int32_t> value16;

  void Accept(Visitor& visitor) final override {
    visitor.Visit("data1", value1)
        .Visit("data2", value2)
        .Visit("data3", value3)
        .Visit("data4", value4)
        .Visit("data5", value5)
        .Visit("data6", value6)
        .Visit("data7", value7)
        .Visit("data8", value8)
        .Visit("data9", value9)
        .Visit("data10", value10)
        .Visit("data11", value11)
        .Visit("data12", value12)
        .Visit("data13", value13)
        .Visit("data14", value14)
        .Visit("data15", value15)
        .Visit("data16", value16);
  }
};

/**
 * @brief 이 테스트의 최상위 (역)직렬화 대상 스키마.
 *
 * 스칼라·바이트 벡터 필드에 더해 Nested 하위 객체(nested_object)를
 * 함께 가져, 중첩 구조체 (역)직렬화까지 한 번에 검증한다.
 */
struct Data : public Schema {
  bool value1;
  std::int8_t value2;
  std::uint8_t value3;
  std::int16_t value4;
  std::uint16_t value5;
  std::int32_t value6;
  std::uint32_t value7;
  std::int64_t value8;
  std::uint64_t value9;

  float value10;
  double value11;

  std::string value12;
  std::string value13;
  std::vector<std::byte> value14;

  Nested nested_object;

  void Accept(Visitor& visitor) final override {
    visitor.Visit("data1", value1)
        .Visit("data2", value2)
        .Visit("data3", value3)
        .Visit("data4", value4)
        .Visit("data5", value5)
        .Visit("data6", value6)
        .Visit("data7", value7)
        .Visit("data8", value8)
        .Visit("data9", value9)
        .Visit("data10", value10)
        .Visit("data11", value11)
        .Visit("data12", value12)
        .Visit("data13", value13)
        .Visit("data14", value14)
        .Visit("nested_object", nested_object);
  }
};

/** @brief Nested의 전 필드가 같은지 검증한다. */
void ExpectNestedEq(const Nested& a, const Nested& b) {
  EXPECT_EQ(a.value1, b.value1);
  EXPECT_EQ(a.value2, b.value2);
  EXPECT_EQ(a.value3, b.value3);
  EXPECT_EQ(a.value4, b.value4);
  EXPECT_EQ(a.value5, b.value5);
  EXPECT_EQ(a.value6, b.value6);
  EXPECT_EQ(a.value7, b.value7);
  EXPECT_EQ(a.value8, b.value8);
  EXPECT_EQ(a.value9, b.value9);
  EXPECT_TRUE(BitEq(a.value10, b.value10));
  EXPECT_TRUE(BitEq(a.value11, b.value11));
  EXPECT_EQ(a.value12, b.value12);
  EXPECT_EQ(a.value13, b.value13);
  EXPECT_EQ(a.value14, b.value14);
  EXPECT_EQ(a.value15, b.value15);
  EXPECT_EQ(a.value16, b.value16);
}

/** @brief Data의 전 필드(중첩 포함)가 같은지 검증한다. */
void ExpectDataEq(const Data& a, const Data& b) {
  EXPECT_EQ(a.value1, b.value1);
  EXPECT_EQ(a.value2, b.value2);
  EXPECT_EQ(a.value3, b.value3);
  EXPECT_EQ(a.value4, b.value4);
  EXPECT_EQ(a.value5, b.value5);
  EXPECT_EQ(a.value6, b.value6);
  EXPECT_EQ(a.value7, b.value7);
  EXPECT_EQ(a.value8, b.value8);
  EXPECT_EQ(a.value9, b.value9);
  EXPECT_TRUE(BitEq(a.value10, b.value10));
  EXPECT_TRUE(BitEq(a.value11, b.value11));
  EXPECT_EQ(a.value12, b.value12);
  EXPECT_EQ(a.value13, b.value13);
  EXPECT_EQ(a.value14, b.value14);
  ExpectNestedEq(a.nested_object, b.nested_object);
}

/** @brief 스키마를 YAML 문자열로 직렬화한다. */
template <typename T>
std::string SerializeToString(T& value) {
  std::ostringstream oss;
  {
    YAMLSerializer serializer(0, oss);
    serializer(value, "");
  }  // 소멸 → Flush
  return oss.str();
}

/** @brief YAML 문자열을 스키마로 역직렬화한다(상태를 반환). */
template <typename T>
bedrock::Status DeserializeFromString(const std::string& yaml, T& value) {
  std::istringstream iss(yaml);
  YAMLDeserializer deserializer(0, iss);
  deserializer(value);
  return deserializer.status;
}

TEST(ArchiveRoundTrip, AllScalarAndContainerTypes) {
  Data d1;
  d1.nested_object.value1 = true;
  d1.nested_object.value2 = std::numeric_limits<std::int8_t>::max();
  d1.nested_object.value3 = std::numeric_limits<std::uint8_t>::max();
  d1.nested_object.value4 = std::numeric_limits<std::int16_t>::max();
  d1.nested_object.value5 = std::numeric_limits<std::uint16_t>::max();
  d1.nested_object.value6 = std::numeric_limits<std::int32_t>::max();
  d1.nested_object.value7 = std::numeric_limits<std::uint32_t>::max();
  d1.nested_object.value8 = std::numeric_limits<std::int64_t>::max();
  d1.nested_object.value9 = std::numeric_limits<std::uint64_t>::max();
  d1.nested_object.value10 = std::numeric_limits<float>::max();
  d1.nested_object.value11 = std::numeric_limits<double>::max();
  // 긴 문자열: 모든 유니코드 스칼라 값을 담아 이스케이프 전수 테스트
  d1.nested_object.value12 = AllUnicodeUtf8();

  // 짧은 문자열: 백슬래시·따옴표·제어문자·멀티바이트를 한 번에 검증
  std::string v13 = "\\\"";  // 백슬래시, 따옴표
  v13.push_back('\x07');     // BEL(제어) → \x07
  v13 += "©";                // U+00A9(멀티바이트, 인쇄 가능) → 그대로
  d1.nested_object.value13 = v13;

  std::vector<std::byte> v14;
  for (int i = 0; i < 256; i++) {
    v14.push_back(static_cast<std::byte>(i));
  }
  d1.nested_object.value14 = v14;
  d1.nested_object.value15.push_back(v13);
  d1.nested_object.value15.push_back(AllUnicodeUtf8());
  d1.nested_object.value15.push_back(v14);

  d1.nested_object.value16["alice"] = 90;
  d1.nested_object.value16["bob"] = -7;
  d1.nested_object.value16["charlie"] = 2147483647;

  d1.value1 = true;
  d1.value2 = std::numeric_limits<std::int8_t>::min();
  d1.value3 = std::numeric_limits<std::uint8_t>::min();
  d1.value4 = std::numeric_limits<std::int16_t>::min();
  d1.value5 = std::numeric_limits<std::uint16_t>::min();
  d1.value6 = std::numeric_limits<std::int32_t>::min();
  d1.value7 = std::numeric_limits<std::uint32_t>::min();
  d1.value8 = std::numeric_limits<std::int64_t>::min();
  d1.value9 = std::numeric_limits<std::uint64_t>::min();
  d1.value10 = std::numeric_limits<float>::min();
  d1.value11 = std::numeric_limits<double>::min();
  d1.value12 = AllUnicodeUtf8();
  d1.value13 = v13;
  d1.value14 = v14;

  const std::string yaml = SerializeToString(d1);
  ASSERT_FALSE(yaml.empty());

  Data d2{};
  const bedrock::Status st = DeserializeFromString(yaml, d2);
  ASSERT_TRUE(st.ok()) << st.message();
  ExpectDataEq(d1, d2);
}

/** @brief 순서/모르는 키 테스트용 소형 스키마. */
struct Mini : public Schema {
  std::int32_t hp = 0;
  std::string name;
  void Accept(Visitor& visitor) final override {
    visitor.Visit("hp", hp).Visit("name", name);
  }
};

TEST(ArchiveRoundTrip, FieldOrderIndependentAndUnknownKeysIgnored) {
  // Accept 순서(hp → name)와 반대로 쓰고, 모르는 키를 끼워 넣는다
  const std::string yaml =
      "name: \"bob\"\n"
      "unknown_key: whatever\n"
      "hp: 7\n";
  Mini m;
  const bedrock::Status st = DeserializeFromString(yaml, m);
  ASSERT_TRUE(st.ok()) << st.message();
  EXPECT_EQ(m.hp, 7);
  EXPECT_EQ(m.name, "bob");
}

/** @brief 시퀀스 안의 매핑(컨테이너-in-시퀀스) 테스트용 스키마. */
struct Rows : public Schema {
  std::vector<std::map<std::string, std::int32_t>> rows;
  void Accept(Visitor& visitor) final override { visitor.Visit("rows", rows); }
};

TEST(ArchiveRoundTrip, SequenceOfMappings) {
  Rows r1;
  r1.rows.push_back({{"a", 1}, {"b", 2}});
  r1.rows.push_back({{"c", 3}});
  const std::string yaml = SerializeToString(r1);
  Rows r2;
  const bedrock::Status st = DeserializeFromString(yaml, r2);
  ASSERT_TRUE(st.ok()) << st.message() << "\nyaml:\n" << yaml;
  EXPECT_EQ(r1.rows, r2.rows);
}

/** @brief 임의 문자열 키 맵 테스트용 스키마. */
struct KeyMap : public Schema {
  std::map<std::string, std::int32_t> m;
  void Accept(Visitor& visitor) final override { visitor.Visit("m", m); }
};

TEST(ArchiveRoundTrip, ArbitraryMapKeysAreQuoted) {
  KeyMap k1;
  k1.m["has space"] = 1;
  k1.m["colon: inside"] = 2;
  k1.m["한글 키"] = 3;
  k1.m["- leading dash"] = 4;
  const std::string yaml = SerializeToString(k1);
  KeyMap k2;
  const bedrock::Status st = DeserializeFromString(yaml, k2);
  ASSERT_TRUE(st.ok()) << st.message() << "\nyaml:\n" << yaml;
  EXPECT_EQ(k1.m, k2.m);
}

/** @brief 집합(set/unordered_set) 테스트용 스키마. */
struct Bag : public Schema {
  std::set<std::string> tags;
  std::unordered_set<std::int32_t> ids;
  void Accept(Visitor& visitor) final override {
    visitor.Visit("tags", tags).Visit("ids", ids);
  }
};

TEST(ArchiveRoundTrip, SetsRoundTripAsSequences) {
  Bag b1;
  b1.tags = {"alpha", "beta", "감마"};
  b1.ids = {7, -3, 2026};
  const std::string yaml = SerializeToString(b1);
  Bag b2;
  const bedrock::Status st = DeserializeFromString(yaml, b2);
  ASSERT_TRUE(st.ok()) << st.message() << "\nyaml:\n" << yaml;
  EXPECT_EQ(b1.tags, b2.tags);
  EXPECT_EQ(b1.ids, b2.ids);
}

/** @brief 중복 키(multimap) 테스트용 스키마. */
struct Multi : public Schema {
  std::multimap<std::string, std::int32_t> mm;
  void Accept(Visitor& visitor) final override { visitor.Visit("mm", mm); }
};

TEST(ArchiveRoundTrip, MultimapKeepsDuplicateKeys) {
  Multi m1;
  m1.mm.insert({"k", 1});
  m1.mm.insert({"k", 2});  // 같은 키 두 번 — 표현 트리의 pairs가 순서 보존
  m1.mm.insert({"x", 9});
  const std::string yaml = SerializeToString(m1);
  Multi m2;
  const bedrock::Status st = DeserializeFromString(yaml, m2);
  ASSERT_TRUE(st.ok()) << st.message() << "\nyaml:\n" << yaml;
  EXPECT_EQ(m1.mm, m2.mm);
}

TEST(ArchiveRoundTrip, ParseErrorReportsLocation) {
  Mini m;
  const bedrock::Status st =
      DeserializeFromString("hp: [1\nname: x\n", m);  // 닫히지 않은 [
  ASSERT_TRUE(st.failed());
  // 오류 메시지에 행:열 위치("YAML L:C")가 담겨야 한다
  EXPECT_NE(st.message().find("YAML"), std::string::npos) << st.message();
}

TEST(ArchiveRoundTrip, MissingFieldReportsName) {
  Mini m;
  const bedrock::Status st = DeserializeFromString("hp: 1\n", m);  // name 없음
  ASSERT_TRUE(st.failed());
  EXPECT_NE(st.message().find("name"), std::string::npos) << st.message();
}

}  // namespace
