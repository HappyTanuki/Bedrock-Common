/**
 * @file serializer.cc
 * @brief 베이스 Serializer 구현부(Represent 단계 — 포맷 독립).
 *
 * Schema 방문 결과를 표현 트리(Node)로 구성한다. 순회 컨텍스트는 읽기
 * 쪽(Construct)과 대칭인 프레임 스택으로 추적한다 — 읽기가 필드 조회/
 * 원소·키값 교대 "소비"라면, 쓰기는 같은 컨텍스트에서의 "생산"이다.
 * 텍스트 렌더링(Present)은 포맷 훅 PresentDocument의 몫이다.
 */
#include <format>
#include <limits>
#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "common/archive/transciber.h"
#include "common/i18n/locales.h"
#include "common/util/base64.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief 실패 센티널(구조 훅의 "순회하지 말 것" 신호). */
constexpr std::size_t kFail = std::numeric_limits<std::size_t>::max();

/** @brief "…를 파싱하는 동안 손상" 상태(대상 이름 포함). */
Status CorruptedWhile(std::string_view what) {
  const std::string_view fmt =
      GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                    locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
  return Status(TranscriberError::kCorrupted,
                std::vformat(fmt, std::make_format_args(what)));
}

/** @brief "데이터 손상" 상태. */
Status CorruptedData() {
  return Status(
      TranscriberError::kCorrupted,
      std::string(GetI18nString(locale::StringKey::kStatusCorruptedData,
                                locale::ISO639_1::kKO,
                                locale::ISO3166_1::kKR)));
}

/**
 * @brief 스칼라 값을 텍스트 표현과 ValueType으로 변환한다.
 *
 * 값→문자열 렌더링은 포맷 독립으로 본다(바이너리는 base64 — 텍스트
 * 포맷들의 사실상 표준). 포맷별로 달라지는 것은 그 문자열을 어떻게
 * 감싸 제시하느냐(인용/블록 표기)뿐이며, 그것은 vtype 힌트를 보고
 * PresentDocument가 결정한다.
 * @tparam T 스칼라 값의 타입.
 * @param value 변환할 값.
 * @return (렌더링된 문자열, 값의 세부 타입) 쌍.
 */
template <typename T>
std::tuple<std::string, ValueType> RenderOrdinaryValueString(T& value) {
  if constexpr (std::is_same_v<T, std::string>) {
    return {std::format("{}", value), ValueType::kString};
  } else if constexpr (std::is_same_v<T, bool>) {
    return {std::format("{}", value), ValueType::kBoolean};
  } else if constexpr (std::is_same_v<T, std::byte>) {
    return {std::format("{:02X}", std::to_integer<int>(value)),
            ValueType::kNumber};
  } else if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
    return {util::Base64Encode(value), ValueType::kBinary};
  } else if constexpr (std::is_arithmetic_v<T>) {
    return {std::format("{}", value), ValueType::kNumber};
  } else {
    return {std::format("{}", value), ValueType::kNull};
  }
}

}  // namespace

// ── Represent 헬퍼 ──────────────────────────────────────────────────

Node Serializer::MakeScalarNode(std::string str, ValueType type) {
  Node n;
  n.kind = Node::Kind::kScalar;
  n.vtype = type;
  n.scalar = std::move(str);
  return n;
}

Node Serializer::MakePlainKey(std::string_view name) {
  Node k;
  k.kind = Node::Kind::kScalar;
  k.vtype = ValueType::kNull;  // plain으로 그대로 출력
  k.scalar = std::string(name);
  return k;
}

void Serializer::AttachToTop(Node&& key, bool keyed, Node&& n) {
  if (_building.empty()) {
    _status = CorruptedData();
    return;
  }
  Node& top = _building.back().node;
  if (top.kind == Node::Kind::kSequence || top.kind == Node::Kind::kSet) {
    top.items.push_back(std::move(n));
    return;
  }
  if (top.kind == Node::Kind::kMapping && keyed) {
    top.pairs.push_back({std::move(key), std::move(n)});
    return;
  }
  _status = CorruptedData();
}

bool Serializer::ResolveEntryKey(std::string_view name, Node& key,
                                 bool& keyed) {
  if (_building.empty()) {
    _status = CorruptedData();
    return false;
  }
  BuildFrame& f = _building.back();
  switch (f.ctx) {
    case BuildFrame::Ctx::kFields:
      key = MakePlainKey(name);
      keyed = true;
      return true;
    case BuildFrame::Ctx::kItem:
      keyed = false;
      return true;
    case BuildFrame::Ctx::kMap:
      if (f.value_turn) {
        key = std::move(f.pending_key);
        f.value_turn = false;
        keyed = true;
        return true;
      }
      // 키 위치의 컨테이너 = 복합 키 — 아직 지원하지 않는다
      _status = CorruptedWhile("complex key");
      return false;
  }
  return false;  // 도달 불가
}

void Serializer::BeginContainer(std::string_view name, Node::Kind kind,
                                BuildFrame::Ctx child_ctx) {
  BuildFrame f;
  f.node.kind = kind;
  f.ctx = child_ctx;
  if (!_status.failed()) {
    ResolveEntryKey(name, f.key, f.keyed);  // 실패해도 프레임은 쌓아 균형 유지
  }
  _building.push_back(std::move(f));
}

void Serializer::EndContainer() {
  if (_building.empty()) {
    return;
  }
  BuildFrame f = std::move(_building.back());
  _building.pop_back();
  if (!_status.failed()) {
    AttachToTop(std::move(f.key), f.keyed, std::move(f.node));
  }
}

Status Serializer::EmitScalar(std::string_view name, std::string value_str,
                              ValueType type) {
  if (_status.failed()) {
    return _status;
  }
  if (_building.empty()) {
    return CorruptedWhile(name);
  }
  BuildFrame& f = _building.back();
  Node n = MakeScalarNode(std::move(value_str), type);
  switch (f.ctx) {
    case BuildFrame::Ctx::kFields:
      f.node.pairs.push_back({MakePlainKey(name), std::move(n)});
      break;
    case BuildFrame::Ctx::kItem:
      f.node.items.push_back(std::move(n));
      break;
    case BuildFrame::Ctx::kMap:
      if (!f.value_turn) {
        f.pending_key = std::move(n);  // 키 캡처 — 값과 짝지을 때까지
        f.value_turn = true;
      } else {
        f.node.pairs.push_back({std::move(f.pending_key), std::move(n)});
        f.value_turn = false;
      }
      break;
  }
  return _status;
}

// ── 구조 훅 ─────────────────────────────────────────────────────────

void Serializer::OnRootBegin(std::string_view name) {
  _root_name = std::string(name);
  BuildFrame f;
  f.node.kind = Node::Kind::kMapping;  // 루트 자식 = 필드들
  f.ctx = BuildFrame::Ctx::kFields;
  _building.push_back(std::move(f));
}

void Serializer::OnRootEnd() {
  if (!_status.failed() && _building.size() != 1) {
    _status = CorruptedWhile("root object");
  }
  if (!_building.empty()) {
    Node built = std::move(_building.back().node);
    _building.pop_back();
    if (!_status.failed()) {
      if (_root_name.empty()) {
        _root = std::move(built);
      } else {
        // 이름 있는 루트: {name: {…}}로 감싼다
        Node wrap;
        wrap.kind = Node::Kind::kMapping;
        wrap.pairs.push_back({MakePlainKey(_root_name), std::move(built)});
        _root = std::move(wrap);
      }
      _root_done = true;
    }
  }
}

void Serializer::OnObjectBegin(std::string_view name) {
  BeginContainer(name, Node::Kind::kMapping, BuildFrame::Ctx::kFields);
}
void Serializer::OnObjectEnd() { EndContainer(); }

std::size_t Serializer::OnSeqBegin(std::string_view name, std::size_t count) {
  if (_status.failed()) {
    return kFail;
  }
  BeginContainer(name, Node::Kind::kSequence, BuildFrame::Ctx::kItem);
  return count;
}
void Serializer::OnSeqEnd() { EndContainer(); }

std::size_t Serializer::OnMapBegin(std::string_view name, std::size_t count) {
  if (_status.failed()) {
    return kFail;
  }
  BeginContainer(name, Node::Kind::kMapping, BuildFrame::Ctx::kMap);
  return count;
}
void Serializer::OnMapEnd() { EndContainer(); }

std::size_t Serializer::OnSetBegin(std::string_view name, std::size_t count) {
  if (_status.failed()) {
    return kFail;
  }
  BeginContainer(name, Node::Kind::kSet, BuildFrame::Ctx::kItem);
  return count;
}
void Serializer::OnSetEnd() { EndContainer(); }

// ── Present 진입(골격) ──────────────────────────────────────────────

Status Serializer::Flush() {
  if (_status.failed()) {
    return _status;
  }
  if (!_building.empty()) {
    return CorruptedData();
  }
  if (!_root_done) {
    return CorruptedData();
  }
  if (!_output_stream.good()) {
    return Status(
        TranscriberError::kCorrupted,
        std::string(GetI18nString(locale::StringKey::kStatusNullStream,
                                  locale::ISO639_1::kKO,
                                  locale::ISO3166_1::kKR)));
  }
  _status = PresentDocument(_root);  // 포맷 훅: 트리 → 텍스트
  _root_done = false;  // 소비 — 중복 Flush가 중복 출력하지 않게
  _root = Node{};
  return _status;
}

// ── 스칼라 Visit들 ──────────────────────────────────────────────────

Serializer& Serializer::Visit(std::string_view name, bool& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::byte& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::int8_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::uint8_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::int16_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::uint16_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::int32_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::uint32_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::int64_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::uint64_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, float& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, double& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name,
                              std::vector<std::byte>& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
Serializer& Serializer::Visit(std::string_view name, std::string& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}

}  // namespace bedrock::archive::transcriber
