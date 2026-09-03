/** @file yaml_deserializer.h @brief Public YAMLDeserializer API. */
#pragma once
#include "common/archive/transcriber/format.h"
#include "common/archive/transcriber/text_deserializer.h"

namespace bedrock::archive::transcriber {

using YAMLDeserializer = TextDeserializer<YAMLFormat>;

}  // namespace bedrock::archive::transcriber
