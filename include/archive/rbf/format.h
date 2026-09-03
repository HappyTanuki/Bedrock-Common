/**
 * @file format.h
 * @brief 참조 바이너리 프로토콜(RBF)의 와이어 상수.
 *
 * RBF는 YAML과 같은 선상의 자기서술 포맷으로, 스펙(1.2.2) Load/Dump의 계층
 * 구조를 그대로 따른다:
 *  - Dump(쓰기): Represent(native->Node, 베이스 RepresentCore) ->
 * Present(Node->bytes, rbf_serializer.cc). YAML처럼 write는 이벤트 단계 없이
 * 단일 훅.
 *  - Load(읽기): Parse(bytes->events, rbf/parse) -> Compose(events->Node,
 * rbf/compose) -> Construct(Node->native, 베이스 ConstructCore). YAML의
 * grammar+compose 대응.
 *
 * 이 헤더는 쓰기(Present)와 읽기(Parse)가 공유하는 최소 바이트 상수만 둔다.
 * 실제 프로토콜이 아니라 자기서술 바이너리 계열(CBOR/UBJSON 등)의 계층 참조용.
 *
 * 와이어 문법:
 *   Stream := Magic("BRF1") Node
 *   Node   := KindByte Body
 *   KindByte: 0=Scalar 1=Sequence 2=Mapping 3=Set
 *   Scalar Body   := NullByte(u8) VType(varint) Len(varint) bytes(scalar)
 *   Seq/Set Body  := Count(varint) Node*Count
 *   Mapping Body  := Count(varint) (키Node 값Node)*Count
 */
#pragma once
#include <array>
#include <cstdint>

namespace bedrock::archive::rbf {

/** @brief 표현 트리 노드 종류의 와이어 태그(1바이트). */
enum class BinKind : std::uint8_t {
  kScalar = 0,
  kSequence = 1,
  kMapping = 2,
  kSet = 3,
};

/** @brief 스트림 선두 매직("BRF1" = Bedrock Reference Format v1). */
inline constexpr std::array<std::uint8_t, 4> kBinMagic{
    {0x42, 0x52, 0x46, 0x31}};

}  // namespace bedrock::archive::rbf
