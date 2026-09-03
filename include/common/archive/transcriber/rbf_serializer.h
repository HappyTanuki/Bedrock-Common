/** @file rbf_serializer.h @brief Public RbfSerializer API. */
#pragma once
#include "common/archive/transcriber/binary_serializer.h"
#include "common/archive/transcriber/format.h"

namespace bedrock::archive::transcriber {

using RbfSerializer = BinarySerializer<RbfFormat>;

}  // namespace bedrock::archive::transcriber
