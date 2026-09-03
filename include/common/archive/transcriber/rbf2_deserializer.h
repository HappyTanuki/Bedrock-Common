/** @file rbf2_deserializer.h @brief Public Rbf2Deserializer API. */
#pragma once
#include "common/archive/transcriber/binary_deserializer.h"
#include "common/archive/transcriber/format.h"

namespace bedrock::archive::transcriber {

using Rbf2Deserializer = BinaryDeserializer<Rbf2Format>;

}  // namespace bedrock::archive::transcriber
