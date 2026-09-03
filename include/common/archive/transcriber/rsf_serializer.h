/** @file rsf_serializer.h @brief Public RsfSerializer API. */
#pragma once
#include "common/archive/transcriber/format.h"
#include "common/archive/transcriber/text_serializer.h"

namespace bedrock::archive::transcriber {

using RsfSerializer = TextSerializer<RsfFormat>;

}  // namespace bedrock::archive::transcriber
