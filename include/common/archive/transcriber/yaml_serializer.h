#pragma once
#include <string>
#include <vector>

#include "transciber.h"

namespace bedrock::archive::transcriber {

class YAMLSerializer : public Serializer {
 public:
  YAMLSerializer(std::uint16_t machine_id, std::ostream& output_stream)
      : Serializer(machine_id, output_stream) {}
  virtual ~YAMLSerializer() override;

  Status Flush() final override;

  void OnRootBegin(std::string_view name) final override;
  void OnRootEnd() final override;

  void OnObjectBegin(std::string_view name) final override;
  void OnObjectEnd() final override;

  std::size_t OnSeqBegin(std::string_view name,
                         std::size_t count) final override;
  void OnSeqEnd() final override;

  std::size_t OnMapBegin(std::string_view name,
                         std::size_t count) final override;
  void OnMapEnd() final override;

  // clang-format off
  YAMLSerializer& Visit(std::string_view name, bool& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::byte& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::int8_t& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::uint8_t& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::int16_t& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::uint16_t& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::int32_t& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::uint32_t& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::int64_t& value) final override;
  YAMLSerializer& Visit(std::string_view name, std::uint64_t& value) final override;
  YAMLSerializer& Visit(std::string_view name, float& value) final override;
  YAMLSerializer& Visit(std::string_view name, double& value) final override;

  YAMLSerializer& Visit(std::string_view name, std::vector<std::byte>& value) final override;

  YAMLSerializer& Visit(std::string_view name, std::string& value) final override;
  // clang-format on

 protected:
  void InjectAppropriateToken(Object& obj) final override;

 private:
  // ── 구체 State (베이스 Visitor::State 확장) ──
  // 상태 인터페이스·스택은 베이스에 있고, 여기서 컨텍스트별 동작·전이를 구현한다.
  // 스칼라 페이로드(name/str/type)는 EmitScalar가 멤버에 stash하고, 각 상태의
  // OnScalar가 Visitor&를 YAMLSerializer&로 다운캐스트해 읽는다.
  struct FieldState final : State {  // 객체/루트 필드: name: value
    void OnScalar(Visitor&) const override;
    std::string ResolveName(Visitor&, std::string_view) const override;
  };
  struct SeqItemState final : State {  // 시퀀스 원소: - value
    void OnScalar(Visitor&) const override;
    std::string ResolveName(Visitor&, std::string_view) const override;
  };
  struct MapKeyState final : State {  // 다음 스칼라 = 키 (캡처)
    void OnScalar(Visitor&) const override;
    std::string ResolveName(Visitor&, std::string_view) const override;
  };
  struct MapValueState final : State {  // 다음 값 = key: value
    void OnScalar(Visitor&) const override;
    std::string ResolveName(Visitor&, std::string_view) const override;
  };

  static const FieldState kField;
  static const SeqItemState kSeqItem;
  static const MapKeyState kMapKey;
  static const MapValueState kMapValue;

  // 스칼라 값 노드 하나를 만들어 현재 부모의 children에 추가.
  void EmitValueNode(std::string_view name, std::string str, ValueType type);
  // 페이로드 stash 후 현재 상태에 스칼라 처리를 위임.
  Status EmitScalar(std::string_view name, std::string value_str,
                    ValueType type);
  // 컨테이너 진입 공통: 이름 결정 → 노드 생성/push → 자식용 상태 push.
  void BeginContainer(std::string_view name, ValueType container_type,
                      const State& child_state);

  // 현재 스칼라 페이로드 (State의 OnScalar가 다운캐스트해 읽음).
  std::string _scalar_name;
  std::string _scalar_str;
  ValueType _scalar_type = ValueType::kNull;

  // 익명 루트(name=="") 아래 자식을 한 단계 덜 들여쓰기 위한 오프셋.
  std::int32_t _root_indent_level = 0;
  // _state_stack, _pending_key 는 베이스에서 상속.
};

}  // namespace bedrock::archive::transcriber
