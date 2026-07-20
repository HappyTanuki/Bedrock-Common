#include "common/archive.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <variant>

#include "common/archive/transcriber/yaml_deserializer.h"
#include "common/archive/transcriber/yaml_serializer.h"

struct Nested : public bedrock::archive::Schema {
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

  void Accept(bedrock::archive::Visitor& visitor) final override;
};

void Nested::Accept(bedrock::archive::Visitor& visitor) {
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

struct Data : public bedrock::archive::Schema {
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

  void Accept(bedrock::archive::Visitor& visitor) final override;
};

void Data::Accept(bedrock::archive::Visitor& visitor) {
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

#define FILE_NAME "test.yaml"

static std::string AllUnicodeUtf8() {
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

int main() {
  Data d1;
  d1.nested_object.value1 = std::numeric_limits<bool>::max();
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
  // 긴 문자열 경로(|). 모든 UTF-8값 0x00~10FFFF를 담아 이스케이프 전수 테스트.
  // (C++ 이스케이프 해석에 의존하지 않고 바이트를 직접 만든다)
  d1.nested_object.value12 = AllUnicodeUtf8();

  // 짧은 문자열 경로("..."). 리터럴 백슬래시 시퀀스가 그대로 들어가는지 확인.
  // R"(...)" raw 리터럴이라 \d \i 등이 컴파일러에 해석되지 않고 그대로 바이트가
  // 됨.
  // 짧은 문자열 경로("..."). 백슬래시·따옴표·제어문자·멀티바이트를 한 번에
  // 검증.
  std::string v13 = "\\\"";  // 백슬래시, 따옴표
  v13.push_back('\x07');     // BEL(제어) → \x07
  v13 += "©";                // © U+00A9(멀티바이트, 인쇄 가능) → 그대로
  d1.nested_object.value13 = v13;

  std::vector<std::byte> v14 = {};
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

  d1.value1 = std::numeric_limits<bool>::max();
  d1.value2 = std::numeric_limits<std::int8_t>::max();
  d1.value3 = std::numeric_limits<std::uint8_t>::max();
  d1.value4 = std::numeric_limits<std::int16_t>::max();
  d1.value5 = std::numeric_limits<std::uint16_t>::max();
  d1.value6 = std::numeric_limits<std::int32_t>::max();
  d1.value7 = std::numeric_limits<std::uint32_t>::max();
  d1.value8 = std::numeric_limits<std::int64_t>::max();
  d1.value9 = std::numeric_limits<std::uint64_t>::max();
  d1.value10 = std::numeric_limits<float>::max();
  d1.value11 = std::numeric_limits<double>::max();
  d1.value12 = AllUnicodeUtf8();
  d1.value13 = v13;
  d1.value14 = v14;

  Data d2;
  d2.value1 = std::numeric_limits<bool>::min();
  d2.value2 = std::numeric_limits<std::int8_t>::min();
  d2.value3 = std::numeric_limits<std::uint8_t>::min();
  d2.value4 = std::numeric_limits<std::int16_t>::min();
  d2.value5 = std::numeric_limits<std::uint16_t>::min();
  d2.value6 = std::numeric_limits<std::int32_t>::min();
  d2.value7 = std::numeric_limits<std::uint32_t>::min();
  d2.value8 = std::numeric_limits<std::int64_t>::min();
  d2.value9 = std::numeric_limits<std::uint64_t>::min();
  d2.value10 = std::numeric_limits<float>::min();
  d2.value11 = std::numeric_limits<double>::min();

  std::ofstream ofs(FILE_NAME);
  {
    bedrock::archive::transcriber::YAMLSerializer serializer(0, ofs);
    serializer(d1, "");
  }  // serializer 소멸 → Flush() → 아직 열려 있는 ofs에 기록
  ofs.close();

  std::ifstream ifs(FILE_NAME);
  {
    bedrock::archive::transcriber::YAMLDeserializer deserializer(0, ifs);
    deserializer(d1);
  }
  ifs.close();

  return 0;
}
