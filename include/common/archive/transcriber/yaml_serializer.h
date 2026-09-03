/** @file yaml_serializer.h @brief Public YAMLSerializer API. */
#pragma once
#include "common/archive/transcriber/format.h"
#include "common/archive/transcriber/text_serializer.h"

namespace bedrock::archive::transcriber {

using YAMLSerializer = TextSerializer<YAMLFormat>;

}  // namespace bedrock::archive::transcriber
