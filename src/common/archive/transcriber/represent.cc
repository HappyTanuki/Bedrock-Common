/**
 * @file serializer.cc
 * @brief 베이스 RepresentCore 구현부(Represent 단계 — 포맷 독립).
 *
 * Schema 방문 결과를 표현 트리(Node)로 구성한다. 순회 컨텍스트는 읽기
 * 쪽(Construct)과 대칭인 프레임 스택으로 추적한다 — 읽기가 필드 조회/
 * 원소·키값 교대 "소비"라면, 쓰기는 같은 컨텍스트에서의 "생산"이다.
 * 텍스트 렌더링(Present)은 포맷 훅 DumpRepresentation의 몫이다.
 */
#include <format>
#include <limits>
#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "archive/transcriber.h"
#include "common/i18n/locales.h"
#include "common/util/base64.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief 실패 sentinel(구조 훅의 "순회하지 말 것" 신호). */
constexpr std::size_t kFail = std::numeric_limits<std::size_t>::max();

/** @brief "…를 파싱하는 동안 손상" 상태(대상 이름 포함). */
OwnedStatus CorruptedWhile(std::string_view what) {
  const std::string_view fmt =
      GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                    locale::IsO6391::kKO, locale::IsO31661::kKR);
  return {TranscriberError::kCorrupted,
          std::vformat(fmt, std::make_format_args(what))};
}

/** @brief "데이터 손상" 상태. */
OwnedStatus CorruptedData() {
  return {
      TranscriberError::kCorrupted,
      std::string(GetI18nString(locale::StringKey::kStatusCorruptedData,
                                locale::IsO6391::kKO, locale::IsO31661::kKR))};
}

/**
 * @brief 스칼라 값을 텍스트 표현과 ValueType으로 변환한다.
 *
 * 값->문자열 렌더링은 포맷 독립으로 본다(바이너리는 base64 — 텍스트
 * 포맷들의 사실상 표준). 포맷별로 달라지는 것은 그 문자열을 어떻게
 * 감싸 제시하느냐(인용/블록 표기)뿐이며, 그것은 vtype 힌트를 보고
 * DumpRepresentation가 결정한다.
 * @tparam T 스칼라 값의 타입.
 * @param value 변환할 값.
 * @return (렌더링된 문자열, 값의 세부 타입) 쌍.
 */
template <typename T>
std::tuple<std::string, ValueType> RenderOrdinaryValueString(T& value) {
  if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::string_view>) {
    return {std::string(value.data(), value.size()), ValueType::kString};
  } else if constexpr (std::is_same_v<T, bool>) {
    return {std::format("{}", value), ValueType::kBoolean};
  } else if constexpr (std::is_same_v<T, std::byte>) {
    return {std::format("{:02X}", std::to_integer<int>(value)),
            ValueType::kNumber};
  } else if constexpr (std::is_same_v<std::remove_cvref_t<T>,
                                      std::span<const std::byte>>) {
    return {util::Base64Encode(value), ValueType::kBinary};
  } else if constexpr (std::is_arithmetic_v<T>) {
    return {std::format("{}", value), ValueType::kNumber};
  } else {
    static_assert(std::is_arithmetic_v<std::remove_cvref_t<T>>,
                  "Unsupported scalar type");
  }
}

}  // namespace

// ── Represent 헬퍼 ──────────────────────────────────────────────────

Node RepresentCore::MakeScalarNode(std::string str, ValueType type) {
  Node node;
  node.kind = Node::Kind::kScalar;
  node.vtype = type;
  node.scalar = std::move(str);
  return node;
}

Node RepresentCore::MakePlainKey(std::string_view name) {
  Node key;
  key.kind = Node::Kind::kScalar;
  key.vtype = ValueType::kNull;  // plain으로 그대로 출력
  key.scalar = std::string(name);
  return key;
}

void RepresentCore::AttachToTop(Node&& key, bool keyed, Node&& node) {
  if (building_.empty()) {
    status_ = CorruptedData();
    return;
  }
  Node& top = building_.back().node;
  if (top.kind == Node::Kind::kSequence || top.kind == Node::Kind::kSet) {
    top.items.push_back(std::move(node));
    return;
  }
  if (top.kind == Node::Kind::kMapping && keyed) {
    top.pairs.push_back({std::move(key), std::move(node)});
    return;
  }
  status_ = CorruptedData();
}

bool RepresentCore::ResolveEntryKey(std::string_view name, Node& key,
                                    bool& keyed) {
  if (building_.empty()) {
    status_ = CorruptedData();
    return false;
  }
  BuildFrame& frame = building_.back();
  switch (frame.ctx) {
    case BuildFrame::Ctx::kFields:
      key = MakePlainKey(name);
      keyed = true;
      return true;
    case BuildFrame::Ctx::kItem:
      keyed = false;
      return true;
    case BuildFrame::Ctx::kMap:
      if (frame.value_turn) {
        key = std::move(frame.pending_key);
        frame.value_turn = false;
        keyed = true;
        return true;
      }
      // 키 위치의 컨테이너 = 복합 키 — 아직 지원하지 않는다
      status_ = CorruptedWhile("complex key");
      return false;
  }
  return false;  // 도달 불가
}

void RepresentCore::BeginContainer(std::string_view name, Node::Kind kind,
                                   BuildFrame::Ctx child_ctx) {
  BuildFrame frame;
  frame.node.kind = kind;
  frame.ctx = child_ctx;
  if (!status_.Failed()) {
    ResolveEntryKey(name, frame.key,
                    frame.keyed);  // 실패해도 프레임은 쌓아 균형 유지
  }
  building_.push_back(std::move(frame));
}

void RepresentCore::EndContainer() {
  if (building_.empty()) {
    return;
  }
  BuildFrame frame = std::move(building_.back());
  building_.pop_back();
  if (!status_.Failed()) {
    AttachToTop(std::move(frame.key), frame.keyed, std::move(frame.node));
  }
}

OwnedStatus RepresentCore::EmitScalar(std::string_view name,
                                      std::string value_str, ValueType type) {
  if (status_.Failed()) {
    return status_;
  }
  if (building_.empty()) {
    return CorruptedWhile(name);
  }
  BuildFrame& frame = building_.back();
  Node node = MakeScalarNode(std::move(value_str), type);
  switch (frame.ctx) {
    case BuildFrame::Ctx::kFields:
      frame.node.pairs.push_back({MakePlainKey(name), std::move(node)});
      break;
    case BuildFrame::Ctx::kItem:
      frame.node.items.push_back(std::move(node));
      break;
    case BuildFrame::Ctx::kMap:
      if (!frame.value_turn) {
        frame.pending_key = std::move(node);  // 키 캡처 — 값과 짝지을 때까지
        frame.value_turn = true;
      } else {
        frame.node.pairs.push_back(
            {std::move(frame.pending_key), std::move(node)});
        frame.value_turn = false;
      }
      break;
  }
  return status_;
}

// ── 구조 훅 ─────────────────────────────────────────────────────────

void RepresentCore::OnRootBegin(const Field& name) {
  root_name_ = std::string(name.name);
  BuildFrame frame;
  frame.node.kind = Node::Kind::kMapping;  // 루트 자식 = 필드들
  frame.ctx = BuildFrame::Ctx::kFields;
  building_.push_back(std::move(frame));
}

void RepresentCore::OnRootEnd() {
  if (!status_.Failed() && building_.size() != 1) {
    status_ = CorruptedWhile("root object");
  }
  if (!building_.empty()) {
    Node built = std::move(building_.back().node);
    building_.pop_back();
    if (!status_.Failed()) {
      if (root_name_.empty()) {
        root_ = std::move(built);
      } else {
        // 이름 있는 루트: {name: {…}}로 감싼다
        Node wrap;
        wrap.kind = Node::Kind::kMapping;
        wrap.pairs.push_back({MakePlainKey(root_name_), std::move(built)});
        root_ = std::move(wrap);
      }
      root_done_ = true;
    }
  }
}

void RepresentCore::OnObjectBegin(const Field& name) {
  BeginContainer(name.name, Node::Kind::kMapping, BuildFrame::Ctx::kFields);
}
void RepresentCore::OnObjectEnd() { EndContainer(); }

std::size_t RepresentCore::OnSeqBegin(const Field& name, std::size_t count) {
  if (status_.Failed()) {
    return kFail;
  }
  BeginContainer(name.name, Node::Kind::kSequence, BuildFrame::Ctx::kItem);
  return count;
}
void RepresentCore::OnSeqEnd() { EndContainer(); }

std::size_t RepresentCore::OnMapBegin(const Field& name, std::size_t count) {
  if (status_.Failed()) {
    return kFail;
  }
  BeginContainer(name.name, Node::Kind::kMapping, BuildFrame::Ctx::kMap);
  return count;
}
void RepresentCore::OnMapEnd() { EndContainer(); }

std::size_t RepresentCore::OnSetBegin(const Field& name, std::size_t count) {
  if (status_.Failed()) {
    return kFail;
  }
  BeginContainer(name.name, Node::Kind::kSet, BuildFrame::Ctx::kItem);
  return count;
}
void RepresentCore::OnSetEnd() { EndContainer(); }

// ── Represent / Dump 진입 ──────────────────────────────────────────

OwnedStatus RepresentCore::Represent(Schema& schema, std::string_view name) {
  if (!status_.Failed()) {
    this->operator()(schema, name);
  }
  return status_;
}

OwnedStatus RepresentCore::Dump() {
  if (status_.Failed()) {
    return status_;
  }
  if (!building_.empty()) {
    return CorruptedData();
  }
  if (!root_done_) {
    return CorruptedData();
  }
  if (!output_stream_->good()) {
    return {TranscriberError::kCorrupted,
            std::string(GetI18nString(locale::StringKey::kStatusNullStream,
                                      locale::IsO6391::kKO,
                                      locale::IsO31661::kKR))};
  }
  status_ = DumpRepresentation(root_);  // 포맷 훅: 트리 -> 텍스트
  root_done_ = false;                   // 소비 — 중복 Dump가 중복 출력하지 않게
  root_ = Node{};
  return status_;
}

// ── 스칼라 Visit들 ──────────────────────────────────────────────────

RepresentCore& RepresentCore::Visit(const Field& name, bool& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::byte& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::int8_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::uint8_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::int16_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::uint16_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::int32_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::uint32_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::int64_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, std::uint64_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, float& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name, double& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name,
                                    std::span<const std::byte>& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}
RepresentCore& RepresentCore::Visit(const Field& name,
                                    std::string_view& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  status_ = EmitScalar(name.name, std::move(value_str), type);
  return *this;
}

}  // namespace bedrock::archive::transcriber
