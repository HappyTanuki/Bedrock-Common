/** @file rbf2_serializer.h @brief Public Rbf2Serializer API. */
#pragma once
#include "common/archive/transcriber/binary_serializer.h"
#include "common/archive/transcriber/format.h"

namespace bedrock::archive::transcriber {

using Rbf2Serializer = BinarySerializer<Rbf2Format>;

}  // namespace bedrock::archive::transcriber
