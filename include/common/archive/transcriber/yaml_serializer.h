/**
 * @file yaml_serializer.h
 * @brief YAML 포맷 전용 직렬화기 YAMLSerializer 선언.
 *
 * Represent(표현 트리 구성)는 베이스 Serializer가 수행하므로, 이
 * 클래스는 Present 훅만 구현한다: 트리를 YAML 텍스트로 렌더링해 출력
 * 스트림에 쓴다. 인용/이스케이프/들여쓰기 등 제시 규칙은 전부 이
 * 구현(yaml_serializer.cc)에만 존재한다 — 읽기 경로의 디코더
 * (yaml/compose.cc)와 왕복 대칭을 맞출 때 볼 곳이 한 곳으로 고정된다.
 */
#pragma once
#include <cstddef>

#include "common/archive/transciber.h"

namespace bedrock::archive::transcriber {

/** @brief YAML 포맷 전용 직렬화기(Present 훅). */
class YAMLSerializer : public Serializer {
 public:
  /**
   * @brief YAML 직렬화기를 생성한다.
   * @param machine_id Snowflake ID 생성에 쓰이는 머신 식별자.
   * @param output_stream 기록할 출력 스트림.
   */
  YAMLSerializer(std::uint16_t machine_id, std::ostream& output_stream)
      : Serializer(machine_id, output_stream) {}
  /** @brief 가상 소멸자. 소멸 시 Flush()를 호출해 구성된 트리를
   *  출력 스트림에 기록한다. */
  virtual ~YAMLSerializer() override;

 private:
  /** @brief 표현 트리를 YAML 텍스트로 렌더링한다(Present 단계). */
  Status PresentDocument(const Node& root) final override;

  /** @brief 블록 컨테이너를 indent 단계에서 렌더링한다. */
  void PresentBlock(const Node& n, std::size_t indent);
  /** @brief ':' 또는 '-' 바로 뒤에 오는 값 꼬리를 렌더링한다. */
  void PresentEntryValue(const Node& n, std::size_t indent);
  /** @brief 매핑 키를 렌더링한다(문자열 키는 인용, 그 외 plain). */
  void PresentKey(const Node& key);
};

}  // namespace bedrock::archive::transcriber
