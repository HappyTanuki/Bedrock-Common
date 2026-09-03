/** @file rsf_deserializer.h @brief Public RsfDeserializer API. */
#pragma once
#include "common/archive/transcriber/format.h"
#include "common/archive/transcriber/text_deserializer.h"

namespace bedrock::archive::transcriber {

using RsfDeserializer = TextDeserializer<RsfFormat>;

}  // namespace bedrock::archive::transcriber
