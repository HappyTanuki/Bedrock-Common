/**
 * @file yaml_deserializer.h
 * @brief YAML 포맷 전용 역직렬화기 YAMLDeserializer 선언.
 *
 * Construct(트리 탐색·타입 해소)는 베이스 Deserializer가 수행하므로,
 * 이 클래스는 포맷 훅 두 개만 구현한다:
 *  - LoadDocument: UTF-8 스트림 → 파스(yaml::Grammar) → 조립
 *    (yaml::Compose) → 표현 트리. 문법 오류는 행:열 위치와 함께 보고.
 *  - IsBinaryScalar: !!binary 태그 판정(YAML 방언).
 */
#pragma once
#include "common/archive/transciber.h"

namespace bedrock::archive::transcriber {

/** @brief YAML 포맷 전용 역직렬화기(Parse+Compose 훅). */
class YAMLDeserializer : public Deserializer {
 public:
  /**
   * @brief YAML 역직렬화기를 생성한다(파싱은 첫 방문 때 지연 수행).
   * @param machine_id Snowflake ID 생성에 쓰이는 머신 식별자.
   * @param input_stream 읽어 들일 입력 스트림.
   */
  YAMLDeserializer(std::uint16_t machine_id, std::istream& input_stream)
      : Deserializer(machine_id, input_stream) {}
  /** @brief 가상 소멸자. */
  virtual ~YAMLDeserializer() override;

 private:
  /**
   * @brief 스트림 전체를 읽어 UTF-8 디코드 → 파스 → 조립한다.
   * 부분 매치는 최심 도달 지점의 "YAML 행:열"을 담아 실패를 보고한다.
   */
  Status LoadDocument(std::istream& in, Node& out) final override;
  /** @brief YAML 방언: !!binary 태그가 붙은 스칼라가 바이너리다. */
  bool IsBinaryScalar(const Node& n) const final override;
};

}  // namespace bedrock::archive::transcriber
