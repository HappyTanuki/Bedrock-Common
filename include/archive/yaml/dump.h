/**
 * @file dump.h
 * @brief YAML Dump = Serialize + Present.
 */
#pragma once

#include <vector>

#include "archive/node.h"
#include "archive/yaml/present.h"

namespace bedrock::archive::yaml {

/** @brief Representation을 Serialize한 뒤 character stream으로 Present한다. */
PresentResult Dump(const transcriber::Node& representation);

/** @brief 문서 여러 개를 하나의 multi-document stream으로 Present한다. */
PresentResult Dump(std::vector<transcriber::Node> representations);

}  // namespace bedrock::archive::yaml
