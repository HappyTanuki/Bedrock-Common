/** @file rbf_deserializer.h @brief Public RbfDeserializer API. */
#pragma once
#include "common/archive/transcriber/binary_deserializer.h"
#include "common/archive/transcriber/format.h"

namespace bedrock::archive::transcriber {

using RbfDeserializer = BinaryDeserializer<RbfFormat>;

}  // namespace bedrock::archive::transcriber
