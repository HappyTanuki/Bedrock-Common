/**
 * @file deserializer.cc
 * @brief 베이스 Deserializer 구현부(Construct 단계 — 포맷 독립).
 *
 * 표현 트리(Node) 위를 프레임 스택으로 탐색하며 목표 C++ 타입으로 값을
 * 해소한다("목표 타입이 곧 스키마"). 포맷 지식은 두 훅으로 격리된다:
 * LoadDocument(텍스트→트리)와 IsBinaryScalar(바이너리 표기 판정 —
 * variant(문자열|바이트열) trial의 판별 근거).
 */
#include <charconv>
#include <format>
#include <limits>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

#include "common/archive/transciber.h"
#include "common/i18n/locales.h"
#include "common/util/base64.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief 실패 센티널(구조 훅의 "순회하지 말 것" 신호). */
constexpr std::size_t kFail = std::numeric_limits<std::size_t>::max();

/** @brief 성공 상태. */
Status Ok() { return Status(make_error_code(TranscriberError::kSuccess)); }

/** @brief "…를 파싱하는 동안 손상" 상태(대상 이름 포함). */
Status CorruptedAt(std::string_view what) {
  const std::string_view fmt =
      GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                    locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
  return Status(TranscriberError::kCorrupted,
                std::vformat(fmt, std::make_format_args(what)));
}

/** @brief "대상 없음" 상태(찾던 이름 포함). */
Status MissingEntry(std::string_view name) {
  const std::string_view base =
      GetI18nString(locale::StringKey::kStatusNoEnt, locale::ISO639_1::kKO,
                    locale::ISO3166_1::kKR);
  return Status(TranscriberError::kNoENT, std::format("{} ({})", base, name));
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
Status ConvertScalar(const Node& n, std::string_view name, T& out,
                     bool binary_scalar) {
  if (n.kind != Node::Kind::kScalar) {
    return CorruptedAt(NameOr(name, "scalar"));
  }
  if constexpr (std::is_same_v<T, std::vector<std::byte>>) {
    if (!binary_scalar) {
      return CorruptedAt(NameOr(name, "binary"));
    }
    if (!util::Base64Decode(n.scalar, out)) {
      return CorruptedAt(NameOr(name, "base64"));
    }
    return Ok();
  } else if constexpr (std::is_same_v<T, std::string>) {
    if (binary_scalar) {
      return CorruptedAt(NameOr(name, "string"));  // variant trial 판별
    }
    out = n.null ? std::string() : n.scalar;
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
    unsigned int v = 0;
    const char* const b = n.scalar.data();
    const char* const e = b + n.scalar.size();
    const auto [p, ec] = std::from_chars(b, e, v, 16);
    if (ec != std::errc{} || p != e || v > 0xFF) {
      return CorruptedAt(NameOr(name, "byte"));
    }
    out = static_cast<std::byte>(v);
    return Ok();
  } else if constexpr (std::is_integral_v<T>) {
    if (n.null) {
      out = T{};
      return Ok();
    }
    const char* const b = n.scalar.data();
    const char* const e = b + n.scalar.size();
    T v{};
    const auto [p, ec] = std::from_chars(b, e, v, 10);
    if (ec != std::errc{} || p != e) {
      return CorruptedAt(NameOr(name, "integer"));
    }
    out = v;
    return Ok();
  } else {
    static_assert(std::is_floating_point_v<T>, "지원하지 않는 스칼라 타입");
    if (n.null) {
      out = T{};
      return Ok();
    }
    const char* const b = n.scalar.data();
    const char* const e = b + n.scalar.size();
    T v{};
    const auto [p, ec] = std::from_chars(b, e, v);
    if (ec != std::errc{} || p != e) {
      return CorruptedAt(NameOr(name, "number"));
    }
    out = v;
    return Ok();
  }
}

}  // namespace

void Deserializer::EnsureLoaded() {
  if (_loaded) {
    return;
  }
  _loaded = true;
  if (status.failed()) {
    return;
  }
  status = LoadDocument(_input_stream, _doc);  // 포맷 훅: Parse+Compose
}

const Node* Deserializer::ResolveChild(std::string_view name) {
  if (status.failed() || _frames.empty()) {
    return nullptr;
  }
  Frame& f = _frames.back();
  if (!f.node) {
    return nullptr;  // 상위에서 이미 실패한 더미 프레임
  }
  if (f.ctx == Frame::Ctx::kFields) {
    if (f.node->kind != Node::Kind::kMapping) {
      status = CorruptedAt(NameOr(name, "object"));
      return nullptr;
    }
    const Node* v = f.node->Find(name);
    if (!v) {
      status = MissingEntry(name);
      return nullptr;
    }
    return v;
  }
  if (f.ctx == Frame::Ctx::kItem) {
    if (!IsItemContainer(*f.node) || f.idx >= f.node->items.size()) {
      status = CorruptedAt("sequence");
      return nullptr;
    }
    return &f.node->items[f.idx++];
  }
  // kMap — 키→값 교대 소비
  if (f.node->kind != Node::Kind::kMapping || f.idx >= f.node->pairs.size()) {
    status = CorruptedAt("mapping");
    return nullptr;
  }
  const Node::Pair& p = f.node->pairs[f.idx];
  if (!f.value_turn) {
    f.value_turn = true;
    return &p.key;
  }
  f.value_turn = false;
  f.idx++;
  return &p.value;
}

void Deserializer::OnRootBegin(std::string_view name) {
  EnsureLoaded();
  const Node* root = nullptr;
  if (!status.failed()) {
    root = &_doc;
    if (!name.empty()) {
      root = _doc.Find(name);
      if (!root) {
        status = MissingEntry(name);
      }
    }
  }
  _frames.push_back({root, 0, Frame::Ctx::kFields, false});
}
void Deserializer::OnRootEnd() {
  if (!_frames.empty()) {
    _frames.pop_back();
  }
}

void Deserializer::OnObjectBegin(std::string_view name) {
  const Node* nd = ResolveChild(name);
  if (nd && nd->kind != Node::Kind::kMapping) {
    status = CorruptedAt(NameOr(name, "object"));
    nd = nullptr;
  }
  // 실패해도 프레임을 쌓는다 — OnObjectEnd와 균형을 맞추고,
  // 내부 방문은 더미 프레임에서 전부 no-op이 된다
  _frames.push_back({nd, 0, Frame::Ctx::kFields, false});
}
void Deserializer::OnObjectEnd() {
  if (!_frames.empty()) {
    _frames.pop_back();
  }
}

std::size_t Deserializer::OnSeqBegin(std::string_view name,
                                     std::size_t count) {
  static_cast<void>(count);
  const Node* nd = ResolveChild(name);
  if (!nd) {
    return kFail;
  }
  if (!IsItemContainer(*nd)) {
    status = CorruptedAt(NameOr(name, "sequence"));
    return kFail;
  }
  _frames.push_back({nd, 0, Frame::Ctx::kItem, false});
  return nd->items.size();
}
void Deserializer::OnSeqEnd() {
  if (!_frames.empty()) {
    _frames.pop_back();
  }
}

std::size_t Deserializer::OnMapBegin(std::string_view name,
                                     std::size_t count) {
  static_cast<void>(count);
  const Node* nd = ResolveChild(name);
  if (!nd) {
    return kFail;
  }
  if (nd->kind != Node::Kind::kMapping) {
    status = CorruptedAt(NameOr(name, "mapping"));
    return kFail;
  }
  _frames.push_back({nd, 0, Frame::Ctx::kMap, false});
  return nd->pairs.size();
}
void Deserializer::OnMapEnd() {
  if (!_frames.empty()) {
    _frames.pop_back();
  }
}

std::size_t Deserializer::OnSetBegin(std::string_view name,
                                     std::size_t count) {
  // 집합 표기가 없는 포맷은 시퀀스로 제시하므로 kSet/kSequence 모두 허용
  return OnSeqBegin(name, count);
}
void Deserializer::OnSetEnd() { OnSeqEnd(); }

void Deserializer::OnTrialBegin() { _savepoints.push_back({_frames, status}); }
bool Deserializer::OnTrialCommit() {
  Savepoint sp = std::move(_savepoints.back());
  _savepoints.pop_back();
  if (sp.status.failed()) {
    return true;  // trial 이전부터 실패 상태 — 첫 대안으로 종료(전파)
  }
  if (status.failed()) {
    _frames = std::move(sp.frames);  // 이 대안 실패 → 되감고 다음 대안
    status = sp.status;
    return false;
  }
  return true;
}

Deserializer& Deserializer::Visit(std::string_view name, bool& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, std::byte& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, std::int8_t& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, std::uint8_t& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, std::int16_t& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name,
                                  std::uint16_t& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, std::int32_t& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name,
                                  std::uint32_t& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, std::int64_t& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name,
                                  std::uint64_t& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, float& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, double& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name,
                                  std::vector<std::byte>& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}
Deserializer& Deserializer::Visit(std::string_view name, std::string& value) {
  if (const Node* nd = ResolveChild(name)) {
    status = ConvertScalar(*nd, name, value, IsBinaryScalar(*nd));
  }
  return *this;
}

}  // namespace bedrock::archive::transcriber
