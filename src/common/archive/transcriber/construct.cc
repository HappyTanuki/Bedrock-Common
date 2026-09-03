/**
 * @file construct.cc
 * @brief 베이스 ConstructCore 구현부(Construct 단계 — 포맷 독립).
 *
 * 표현 트리(Node) 위를 프레임 스택으로 탐색하며 목표 C++ 타입으로 값을
 * 해소한다("목표 타입이 곧 스키마"). 포맷 지식은 두 훅으로 격리된다:
 * LoadRepresentation(텍스트->트리)와 IsBinaryScalar(바이너리 표기 판정 —
 * variant(문자열|바이트열) trial의 판별 근거).
 */
#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

#include "archive/transcriber.h"
#include "common/i18n/locales.h"
#include "common/util/base64.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief 실패 sentinel(구조 훅의 "순회하지 말 것" 신호). */
constexpr std::size_t kFail = std::numeric_limits<std::size_t>::max();

/** @brief 성공 상태. */
OwnedStatus Ok() { return {make_error_code(TranscriberError::kSuccess)}; }

/** @brief "…를 파싱하는 동안 손상" 상태(대상 이름 포함). */
OwnedStatus CorruptedAt(std::string_view what) {
  const std::string_view fmt =
      GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                    locale::IsO6391::kKO, locale::IsO31661::kKR);
  return {TranscriberError::kCorrupted,
          std::vformat(fmt, std::make_format_args(what))};
}

/** @brief "대상 없음" 상태(찾던 이름 포함). */
OwnedStatus MissingEntry(std::string_view name) {
  const std::string_view base =
      GetI18nString(locale::StringKey::kStatusNoEnt, locale::IsO6391::kKO,
                    locale::IsO31661::kKR);
  return {TranscriberError::kNoENT, std::format("{} ({})", base, name)};
}

/** @brief 오류 메시지에 쓸 대상 이름(무명 원소는 자리 표시). */
std::string_view NameOr(std::string_view name, std::string_view fallback) {
  return name.empty() ? fallback : name;
}

/** @brief 노드가 원소 컨테이너(시퀀스/집합)인지 — 읽기는 둘을 호환한다. */
bool IsItemContainer(const Node& n) {
  return n.kind == Node::Kind::kSequence || n.kind == Node::Kind::kSet;
}

/**
 * @brief 스칼라 노드를 목표 타입으로 변환한다(타입 지시 해소).
 *
 * null(e-node)은 목표 타입의 기본값으로 관대하게 해석한다(바이너리
 * 제외). 표기 불일치·형식 불일치는 kCorrupted — variant trial이 이
 * 실패를 보고 다음 대안으로 넘어간다.
 * @param binary_scalar 포맷 훅(IsBinaryScalar)의 판정 결과.
 */
template <typename T>
OwnedStatus ConvertScalar(const Node& n, std::string_view name, T& out,
                          bool binary_scalar) {
  if (n.kind != Node::Kind::kScalar) {
    return CorruptedAt(NameOr(name, "scalar"));
  }
  if constexpr (std::is_same_v<T, std::span<const std::byte>>) {
    if (!binary_scalar) {
      return CorruptedAt(NameOr(name, "binary"));
    }
    Node& owner = const_cast<Node&>(n);
    if (owner.binary.empty() && !owner.scalar.empty() &&
        !util::Base64Decode(owner.scalar, owner.binary)) {
      return CorruptedAt(NameOr(name, "base64"));
    }
    out = std::span<const std::byte>(owner.binary.data(), owner.binary.size());
    return Ok();
  } else if constexpr (std::is_same_v<T, std::string_view>) {
    if (binary_scalar) {
      return CorruptedAt(NameOr(name, "string"));  // variant trial 판별
    }
    out = n.null ? std::string_view{} : std::string_view(n.scalar);
    return Ok();
  } else if constexpr (std::is_same_v<T, bool>) {
    if (n.null) {
      out = false;
      return Ok();
    }
    if (n.scalar == "true") {
      out = true;
      return Ok();
    }
    if (n.scalar == "false") {
      out = false;
      return Ok();
    }
    return CorruptedAt(NameOr(name, "bool"));
  } else if constexpr (std::is_same_v<T, std::byte>) {
    // 직렬화기가 {:02X} 16진 두 자리로 기록한다
    if (n.null) {
      out = std::byte{0};
      return Ok();
    }
    unsigned int value = 0;
    const char* const bytes = n.scalar.data();
    const char* const error = bytes + n.scalar.size();
    const auto [pair, ec] = std::from_chars(bytes, error, value, 16);
    if (ec != std::errc{} || pair != error || value > 0xFF) {
      return CorruptedAt(NameOr(name, "byte"));
    }
    out = static_cast<std::byte>(value);
    return Ok();
  } else if constexpr (std::is_integral_v<T>) {
    if (n.null) {
      out = T{};
      return Ok();
    }
    const char* const bytes = n.scalar.data();
    const char* const error = bytes + n.scalar.size();
    T value{};
    const auto [pair, ec] = std::from_chars(bytes, error, value, 10);
    if (ec != std::errc{} || pair != error) {
      return CorruptedAt(NameOr(name, "integer"));
    }
    out = value;
    return Ok();
  } else {
    static_assert(std::is_floating_point_v<T>, "지원하지 않는 스칼라 타입");
    if (n.null) {
      out = T{};
      return Ok();
    }
    const char* const bytes = n.scalar.data();
    const char* const error = bytes + n.scalar.size();
    T value{};
    const auto [pair, ec] = std::from_chars(bytes, error, value);
    if (ec != std::errc{} || pair != error) {
      return CorruptedAt(NameOr(name, "number"));
    }
    out = value;
    return Ok();
  }
}

}  // namespace

OwnedStatus ConstructCore::Load() {
  EnsureLoaded();
  return status_;
}

OwnedStatus ConstructCore::Construct(Schema& schema, std::string_view name) {
  EnsureLoaded();
  if (!status_.Failed()) {
    this->operator()(schema, name);
  }
  return status_;
}

void ConstructCore::EnsureLoaded() {
  if (loaded_) {
    return;
  }
  loaded_ = true;
  if (status_.Failed()) {
    return;
  }
  status_ = LoadRepresentation(input_, doc_);  // 포맷 훅: Parse+Compose
}

const Node* ConstructCore::ResolveChild(std::string_view name) {
  if (status_.Failed() || frames_.empty()) {
    return nullptr;
  }
  Frame& frame = frames_.back();
  if (frame.node == nullptr) {
    return nullptr;  // 상위에서 이미 실패한 더미 프레임
  }
  if (frame.ctx == Frame::Ctx::kFields) {
    if (frame.node->kind != Node::Kind::kMapping) {
      status_ = CorruptedAt(NameOr(name, "object"));
      return nullptr;
    }
    const Node* value = frame.node->Find(name);
    if (value == nullptr) {
      status_ = MissingEntry(name);
      return nullptr;
    }
    return value;
  }
  if (frame.ctx == Frame::Ctx::kItem) {
    if (!IsItemContainer(*frame.node) ||
        frame.idx >= frame.node->items.size()) {
      status_ = CorruptedAt("sequence");
      return nullptr;
    }
    return &frame.node->items[frame.idx++];
  }
  // kMap — 키->값 교대 소비
  if (frame.node->kind != Node::Kind::kMapping ||
      frame.idx >= frame.node->pairs.size()) {
    status_ = CorruptedAt("mapping");
    return nullptr;
  }
  const Node::Pair& pair = frame.node->pairs[frame.idx];
  if (!frame.value_turn) {
    frame.value_turn = true;
    return &pair.key;
  }
  frame.value_turn = false;
  frame.idx++;
  return &pair.value;
}

void ConstructCore::OnRootBegin(const Field& name) {
  EnsureLoaded();
  const Node* root = nullptr;
  if (!status_.Failed()) {
    root = &doc_;
    if (!name.name.empty()) {
      root = doc_.Find(name.name);
      if (root == nullptr) {
        status_ = MissingEntry(name.name);
      }
    }
  }
  frames_.push_back({root, 0, Frame::Ctx::kFields, false});
}
void ConstructCore::OnRootEnd() {
  if (!frames_.empty()) {
    frames_.pop_back();
  }
}

void ConstructCore::OnObjectBegin(const Field& name) {
  const Node* node = ResolveChild(name.name);
  if ((node != nullptr) && node->kind != Node::Kind::kMapping) {
    status_ = CorruptedAt(NameOr(name.name, "object"));
    node = nullptr;
  }
  // 실패해도 프레임을 쌓는다 — OnObjectEnd와 균형을 맞추고,
  // 내부 방문은 더미 프레임에서 전부 no-op이 된다
  frames_.push_back({node, 0, Frame::Ctx::kFields, false});
}
void ConstructCore::OnObjectEnd() {
  if (!frames_.empty()) {
    frames_.pop_back();
  }
}

std::size_t ConstructCore::OnSeqBegin(const Field& name, std::size_t count) {
  static_cast<void>(count);
  const Node* node = ResolveChild(name.name);
  if (node == nullptr) {
    return kFail;
  }
  if (!IsItemContainer(*node)) {
    status_ = CorruptedAt(NameOr(name.name, "sequence"));
    return kFail;
  }
  frames_.push_back({node, 0, Frame::Ctx::kItem, false});
  return node->items.size();
}
void ConstructCore::OnSeqEnd() {
  if (!frames_.empty()) {
    frames_.pop_back();
  }
}

std::size_t ConstructCore::OnMapBegin(const Field& name, std::size_t count) {
  static_cast<void>(count);
  const bool allow_duplicates = std::exchange(duplicate_keys_pending_, false);
  const Node* node = ResolveChild(name.name);
  if (node == nullptr) {
    return kFail;
  }
  if (node->kind != Node::Kind::kMapping) {
    status_ = CorruptedAt(NameOr(name.name, "mapping"));
    return kFail;
  }
  if (!allow_duplicates && !MappingHasUniqueKeys(*node)) {
    status_ = CorruptedAt("duplicate key");
    return kFail;
  }
  frames_.push_back({node, 0, Frame::Ctx::kMap, false});
  return node->pairs.size();
}
void ConstructCore::OnMapEnd() {
  if (!frames_.empty()) {
    frames_.pop_back();
  }
}

void ConstructCore::OnDuplicateKeysBegin() {
  // multimap 등 중복 키가 의미 있는 컨테이너다. 포맷 훅에 알리고,
  // 곧 이어지는 맵 진입에서 중복 키 검증을 건너뛴다.
  duplicate_keys_pending_ = true;
  OnDuplicateKeysNotify();
}

/**
 * @brief 매핑 노드의 키 유일성을 검증한다(§3.1.1 representation 규칙).
 *
 * 스칼라 키는 (태그, 값 종류, 문자열)로 동일성을 판단한다. multimap처럼
 * 중복이 의미 있는 컨테이너는 OnDuplicateKeysBegin를 통해 검증을 건너뛴다.
 */
bool ConstructCore::MappingHasUniqueKeys(const Node& mapping) const {
  for (std::size_t outer = 0; outer < mapping.pairs.size(); ++outer) {
    for (std::size_t inner = outer + 1; inner < mapping.pairs.size(); ++inner) {
      const Node::Pair& left = mapping.pairs[outer];
      const Node::Pair& right = mapping.pairs[inner];
      if (left.key.kind == Node::Kind::kScalar &&
          right.key.kind == Node::Kind::kScalar &&
          left.key.tag == right.key.tag && left.key.vtype == right.key.vtype &&
          left.key.scalar == right.key.scalar) {
        return false;
      }
    }
  }
  return true;
}

std::size_t ConstructCore::OnSetBegin(const Field& name, std::size_t count) {
  // 집합 표기가 없는 포맷은 시퀀스로 제시하므로 kSet/kSequence 모두 허용
  return OnSeqBegin(name, count);
}
void ConstructCore::OnSetEnd() { OnSeqEnd(); }

void ConstructCore::OnTrialBegin() {
  savepoints_.push_back({frames_, status_});
}
bool ConstructCore::OnTrialCommit() {
  Savepoint savepoint = std::move(savepoints_.back());
  savepoints_.pop_back();
  if (savepoint.status.Failed()) {
    return true;  // trial 이전부터 실패 상태 — 첫 대안으로 종료(전파)
  }
  if (status_.Failed()) {
    frames_ = std::move(savepoint.frames);  // 이 대안 실패 -> 되감고 다음 대안
    status_ = savepoint.status;
    return false;
  }
  return true;
}

ConstructCore& ConstructCore::Visit(const Field& name, bool& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::byte& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::int8_t& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::uint8_t& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::int16_t& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::uint16_t& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::int32_t& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::uint32_t& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::int64_t& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, std::uint64_t& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, float& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name, double& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name,
                                    std::span<const std::byte>& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}
ConstructCore& ConstructCore::Visit(const Field& name,
                                    std::string_view& value) {
  if (const Node* node = ResolveChild(name.name)) {
    status_ = ConvertScalar(*node, name.name, value, IsBinaryScalar(*node));
  }
  return *this;
}

}  // namespace bedrock::archive::transcriber
