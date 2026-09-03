/** @file load.cc @brief YAML Load = Parse + Compose. */
#include "archive/yaml/load.h"

#include <vector>

#include "archive/yaml/parse.h"

namespace bedrock::archive::yaml {

ComposeResult Load(std::u32string_view presentation,
                   const ComposeOptions& options) {
  const std::vector<std::uint32_t> buffer(presentation.begin(),
                                          presentation.end());
  return Load(buffer, options);
}

ComposeResult Load(std::span<const std::uint32_t> presentation,
                   const ComposeOptions& options) {
  ComposeEventBuilder builder(options);
  std::string error;
  if (!ParseToSerializationSink(presentation, builder, error)) {
    ComposeResult result = builder.Finish();
    result.ok = false;
    if (result.error.empty()) {
      result.error = std::move(error);
    }
    return result;
  }
  return builder.Finish();
}

}  // namespace bedrock::archive::yaml
