/**
 * @file compose.h
 * @brief YAML serialization tree를 representation으로 Compose한다.
 */
#pragma once

#include <string>
#include <vector>

#include "archive/node.h"
#include "archive/yaml/serialization.h"

namespace bedrock::archive::yaml {

/** @brief Compose 옵션 — 포맷 방언이 허용하는 관대함을 조정한다. */
struct ComposeOptions {
  /**
   * @brief 매핑 안의 중복 키를 허용한다.
   *
   * YAML 1.2.2 §3.1.1은 매핑 키의 유일성을 representation 모델이
   * 요구하지만, multimap 같은 스키마는 중복 키가 의미를 갖는다. 이
   * 옵션을 켜면 중복 키 검증을 건너뛰고 쌍을 그대로 보존한다.
   */
  bool allow_duplicate_keys = false;
};

struct ComposeResult {
  bool ok = false;
  std::string error;
  std::vector<transcriber::Node> docs;
};

/** @brief Serialization stream을 representation 문서들로 Compose한다. */
ComposeResult Compose(const SerializationStream& serialization,
                      const ComposeOptions& options = {});

/** @brief serialization event를 representation으로 직접 Compose한다. */
class ComposeEventBuilder final : public SerializationSink {
 public:
  explicit ComposeEventBuilder(const ComposeOptions& options = {});
  ~ComposeEventBuilder() override;
  ComposeEventBuilder(const ComposeEventBuilder&) = delete;
  ComposeEventBuilder& operator=(const ComposeEventBuilder&) = delete;

  bool OnEvent(SerializationEvent&& event) override;
  ComposeResult Finish();

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace bedrock::archive::yaml
