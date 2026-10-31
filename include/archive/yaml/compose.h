/**
 * @file compose.h
 * @brief YAML serialization tree를 representation으로 Compose한다.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "archive/node.h"
#include "archive/yaml/serialization.h"

namespace bedrock::archive::yaml {

struct ComposeResult {
  bool ok = false;
  std::string error;
  std::vector<transcriber::Node> docs;
  std::int64_t identity_error = 0;
};

/** @brief Serialization stream을 representation 문서들로 Compose한다. */
ComposeResult Compose(const SerializationStream& serialization);

/** @brief serialization event를 representation으로 직접 Compose한다. */
class ComposeEventBuilder final : public SerializationSink {
 public:
  ComposeEventBuilder();
  ~ComposeEventBuilder() override;
  ComposeEventBuilder(const ComposeEventBuilder&) = delete;
  ComposeEventBuilder& operator=(const ComposeEventBuilder&) = delete;

  bool OnEvent(SerializationEvent&& event) override;
  ComposeResult Finish();

 private:
  struct Build {
    transcriber::Node node;
    transcriber::Node key;
    bool has_key = false;
    std::string anchor;
  };

  bool Fail(std::string message);
  bool Attach(transcriber::Node&& node);
  bool ValidateKeys(const transcriber::Node& mapping);

  ComposeResult result;
  std::map<std::string, transcriber::Node> anchors;
  std::vector<Build> stack;
  transcriber::Node root;
  bool have_root = false;
  bool failed = false;
};

}  // namespace bedrock::archive::yaml
