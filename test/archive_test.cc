#include "common/archive.h"

#include <fstream>
#include <limits>

#include "common/archive/transcriber/yaml_deserializer.h"
#include "common/archive/transcriber/yaml_serializer.h"

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

  void Accept(bedrock::archive::Visitor& visitor) override;
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
      .Visit("data11", value11);
}

#define FILE_NAME "test.yaml"

int main() {
  Data d1;
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
  bedrock::archive::transcriber::YAMLSerializer serializer(0, ofs);
  serializer(d1);
  ofs.close();

  std::ifstream ifs(FILE_NAME);
  bedrock::archive::transcriber::YAMLDeserializer deserializer(0, ifs);
  deserializer(d2);
  ifs.close();

  return 0;
}
