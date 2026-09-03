/**
 * @file dump.cc
 * @brief YAML Dump = Serialize + Present.
 */
#include "archive/yaml/dump.h"

#include <utility>
#include <vector>

#include "archive/yaml/serialize.h"

namespace bedrock::archive::yaml {

PresentResult Dump(const transcriber::Node& representation) {
  return Present(Serialize(representation));
}

PresentResult Dump(std::vector<transcriber::Node> representations) {
  SerializationStream serialization;
  serialization.documents.reserve(representations.size());
  for (transcriber::Node& representation : representations) {
    SerializationStream single = Serialize(representation);
    serialization.documents.push_back(std::move(single.documents.at(0)));
  }
  return Present(std::move(serialization));
}

}  // namespace bedrock::archive::yaml
