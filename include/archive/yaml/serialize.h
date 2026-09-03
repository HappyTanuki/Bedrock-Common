/**
 * @file serialize.h
 * @brief YAML representation을 serialization tree로 Serialize한다.
 */
#pragma once

#include "archive/node.h"
#include "archive/yaml/serialization.h"

namespace bedrock::archive::yaml {

/** @brief Representation을 순차 접근 가능한 serialization stream으로 만든다. */
SerializationStream Serialize(const transcriber::Node& representation);

}  // namespace bedrock::archive::yaml
