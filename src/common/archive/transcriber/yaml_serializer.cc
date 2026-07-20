#include "common/archive/transcriber/yaml_serializer.h"

#include <format>
#include <limits>
#include <ostream>
#include <string>

#include "common/archive/transcriber/transciber.h"
#include "common/i18n/locales.h"
#include "common/util/base64.h"

namespace bedrock::archive::transcriber {

YAMLSerializer::~YAMLSerializer() { Flush(); };

Status YAMLSerializer::Flush() {
  if (_status.failed()) {
    return _status;
  }
  if (!_objects.empty()) {
    std::string fmt(GetI18nString(locale::StringKey::kStatusCorruptedData,
                                  locale::ISO639_1::kKO,
                                  locale::ISO3166_1::kKR));
    return Status(TranscriberError::kCorrupted, fmt);
  }
  if (!_root_object) {
    std::string fmt(GetI18nString(locale::StringKey::kStatusCorruptedData,
                                  locale::ISO639_1::kKO,
                                  locale::ISO3166_1::kKR));
    return Status(TranscriberError::kCorrupted, fmt);
  }
  if (!_output_stream.good()) {
    std::string fmt(GetI18nString(locale::StringKey::kStatusNullStream,
                                  locale::ISO639_1::kKO,
                                  locale::ISO3166_1::kKR));
    return Status(TranscriberError::kCorrupted, fmt);
  }

  struct Frame {
    Object* node;
    bool entered;  // false=진입 전, true=자식 처리 끝, 이제 닫기
  };

  std::stack<Frame> stack;
  stack.push({_root_object.get(), false});

  while (!stack.empty()) {
    Frame& f = stack.top();
    if (!f.entered) {
      _output_stream << f.node->begin_token;  // 진입 시
      f.entered = true;
      for (auto it = f.node->children.rbegin(); it != f.node->children.rend();
           ++it) {
        stack.push({&*it, false});
      }
      for (auto& byte : f.node->value) {
        _output_stream << static_cast<char>(byte);
      }
      // 주의: 위에서 push하면서 참조 f가 무효화될 수 있으니 f 재사용 금지
    } else {
      _output_stream << f.node->end_token;  // 자식 다 끝난 뒤
      stack.pop();
    }
  }

  return _status;
}

// YAML 1.2 c-printable 집합. 이 밖의 코드포인트(C0/DEL/C1 제어, 비문자,
// 서로게이트)는 블록 스칼라에 담을 수 없어 걸러낸다. PyYAML의 허용 집합과 동일.
static bool IsYamlPrintable(char32_t cp) {
  return cp == 0x09 || cp == 0x0A || cp == 0x0D ||  // tab, LF, CR
         (0x20 <= cp && cp <= 0x7E) ||              // 인쇄 ASCII
         cp == 0x85 ||                              // NEL
         (0xA0 <= cp && cp <= 0xD7FF) ||            // BMP (서로게이트 전)
         (0xE000 <= cp && cp <= 0xFFFD) ||          // BMP (서로게이트 후)
         (0x10000 <= cp && cp <= 0x10FFFF);         // astral
}
// UTF-8 한 시퀀스(2~4바이트)를 디코드한다. 성공 시 cp/len을 채우고 true.
// 잘못된 리드 바이트나 잘린 시퀀스면 false.
static bool DecodeUtf8(const std::vector<std::byte>& in, std::size_t i,
                       char32_t& cp, std::size_t& len) {
  unsigned char c = static_cast<unsigned char>(in[i]);
  if ((c & 0xE0) == 0xC0) {
    len = 2;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    len = 3;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    len = 4;
    cp = c & 0x07;
  } else {
    return false;  // 잘못된 리드 바이트
  }
  if (i + len > in.size()) {
    return false;  // 잘린 시퀀스
  }
  for (std::size_t k = 1; k < len; ++k) {
    cp = (cp << 6) | (static_cast<unsigned char>(in[i + k]) & 0x3F);
  }
  return true;
}

// YAML(1.1)이 줄바꿈으로 취급하는 문자: LF/CR/NEL/LS/PS.
static bool IsYamlLineBreak(char32_t cp) {
  return cp == 0x0A || cp == 0x0D || cp == 0x85 || cp == 0x2028 || cp == 0x2029;
}

// 입력 바이트열을 UTF-8 코드포인트 단위로 순회한다. ASCII(1바이트)와 멀티바이트
// 분기·디코드를 한곳에 모아, 각 이스케이프 함수가 루프를 중복 구현하지 않게
// 한다. emit(cp, bytes, len, valid):
//   valid=true  → 정상 코드포인트 (bytes/len은 원본 UTF-8 바이트)
//   valid=false → 깨진/잘린 시퀀스 (bytes[0] 한 바이트, cp는 무의미)
template <typename F>
static void ForEachUtf8Char(const std::vector<std::byte>& in, F&& emit) {
  for (std::size_t i = 0; i < in.size();) {
    unsigned char c = static_cast<unsigned char>(in[i]);
    if (c < 0x80) {
      emit(static_cast<char32_t>(c), &in[i], std::size_t{1}, true);
      ++i;
    } else {
      char32_t cp;
      std::size_t len;
      if (DecodeUtf8(in, i, cp, len)) {
        emit(cp, &in[i], len, true);
        i += len;
      } else {
        emit(char32_t{0}, &in[i], std::size_t{1}, false);
        ++i;
      }
    }
  }
}

// YAML 이중 인용(double-quoted) 스칼라용 이스케이프.
// \ 와 " 및 비인쇄 문자만 이스케이프한다. double-quoted는
// \xNN/\uNNNN/\UNNNNNNNN 으로 모든 코드포인트를 표현할 수 있으므로, |블록과
// 달리 버리지 않고 이스케이프해 왕복을 보장한다. NEL/LS/PS도 줄 접힘을 피하려
// 이스케이프한다.
static std::vector<std::byte> EscapeForDoubleQuoted(
    const std::vector<std::byte>& in) {
  std::string out;
  out.reserve(in.size());
  ForEachUtf8Char(in, [&](char32_t cp, const std::byte* bytes, std::size_t len,
                          bool valid) {
    if (!valid) {  // 깨진 바이트도 이스케이프로 보존
      out += std::format("\\x{:02X}", static_cast<unsigned char>(bytes[0]));
      return;
    }
    switch (cp) {  // 짧은 이스케이프
      case '\\':
        out += "\\\\";
        return;
      case '"':
        out += "\\\"";
        return;
      case '\n':
        out += "\\n";
        return;
      case '\t':
        out += "\\t";
        return;
      case '\r':
        out += "\\r";
        return;
    }
    if (IsYamlPrintable(cp) && !IsYamlLineBreak(cp)) {
      for (std::size_t k = 0; k < len; ++k) {  // 인쇄 문자는 원본 UTF-8 그대로
        out += static_cast<char>(bytes[k]);
      }
    } else if (cp <= 0x7F) {
      out += std::format("\\x{:02X}", static_cast<std::uint32_t>(cp));
    } else if (cp <= 0xFFFF) {
      out += std::format("\\u{:04X}", static_cast<std::uint32_t>(cp));
    } else {
      out += std::format("\\U{:08X}", static_cast<std::uint32_t>(cp));
    }
  });
  auto bytes = std::as_bytes(std::span{out});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}

// YAML 리터럴 블록(|)용. 이스케이프가 통하지 않는 스타일이라 비인쇄 문자는
// 버리고, 줄바꿈(LF/CR/NEL/LS/PS)은 실제 줄바꿈으로 두되 뒤에 들여쓰기를 붙여
// 블록을 유지.
static std::vector<std::byte> EscapeForPipeBlock(
    const std::vector<std::byte>& in, std::size_t indent_level,
    std::size_t indent_size) {
  std::string indent(indent_size * (indent_level + 1), ' ');
  std::string out;
  out.reserve(in.size() * 2);
  out += indent;
  ForEachUtf8Char(in, [&](char32_t cp, const std::byte* bytes, std::size_t len,
                          bool valid) {
    if (!valid || !IsYamlPrintable(cp)) {
      return;  // 깨진 바이트·비인쇄 코드포인트(C0/DEL/C1/비문자) 제거
    }
    for (std::size_t k = 0; k < len; ++k) {
      out += static_cast<char>(bytes[k]);
    }
    if (IsYamlLineBreak(cp)) {
      out += indent;
    }
  });
  auto bytes = std::as_bytes(std::span{out});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}

// base64 문자열을 width자마다 개행하고 각 줄을 들여쓰기해 !!binary | 블록
// 본문을 만든다. base64는 전부 인쇄 가능 문자라 이스케이프/필터가 필요 없다.
// 블록 안의 개행은 base64 디코더가 무시하므로 임의 폭으로 감싸도 왕복에 영향이
// 없다. 마지막 줄에는 개행을 붙이지 않고 end_token이 블록을 종료한다.
static std::vector<std::byte> WrapBase64Block(const std::vector<std::byte>& in,
                                              std::size_t indent_level,
                                              std::size_t indent_size,
                                              std::size_t width = 76) {
  std::string indent(indent_size * (indent_level + 1), ' ');
  std::string out;
  for (std::size_t i = 0; i < in.size(); i += width) {
    if (i != 0) {
      out += '\n';
    }
    out += indent;
    std::size_t n = (in.size() - i < width) ? in.size() - i : width;
    for (std::size_t k = 0; k < n; ++k) {
      out += static_cast<char>(in[i + k]);
    }
  }
  auto bytes = std::as_bytes(std::span{out});
  return std::vector<std::byte>(bytes.begin(), bytes.end());
}

void YAMLSerializer::InjectAppropriateToken(Object& obj) {
  std::size_t indent_level = _objects.size() + _root_indent_level;
  std::size_t indent_len = indent_level * _indent_size;
  std::string indent(indent_len, ' ');
  if (obj.object_type == ObjectType::kRoot && obj.name == "") {
    obj.begin_token = "";
    obj.end_token = "";
    _root_indent_level = -1;
  } else if (obj.object_type == ObjectType::kObject ||
             obj.object_type == ObjectType::kRoot) {
    obj.begin_token = std::format("{}{}:\n", indent, obj.name);
    obj.end_token = "\n";
  } else if (obj.object_type == ObjectType::kValue) {
    if (HasFlag(obj.value_type, ValueType::kSequence)) {
      if (HasFlag(obj.value_type, ValueType::kString)) {
        obj.begin_token = std::format("{}- \"", indent, obj.name);
        obj.value = EscapeForDoubleQuoted(obj.value);
        obj.end_token = "\"\n";
      } else if (HasFlag(obj.value_type, ValueType::kBinary)) {
        obj.begin_token = std::format("{}- !!binary |\n", indent, obj.name);
        obj.value = WrapBase64Block(obj.value, indent_level, _indent_size);
        obj.end_token = "\n";
      } else {
        obj.begin_token = std::format("{}- ", indent, obj.name);
        obj.end_token = "\n";
      }
    } else if (obj.value_type == ValueType::kString) {
      obj.begin_token = std::format("{}{}: \"", indent, obj.name);
      obj.value = EscapeForDoubleQuoted(obj.value);
      obj.end_token = "\"\n";
    } else if (obj.value_type == ValueType::kBinary) {
      obj.begin_token = std::format("{}{}: !!binary |\n", indent, obj.name);
      obj.value = WrapBase64Block(obj.value, indent_level, _indent_size);
      obj.end_token = "\n";
    } else {
      obj.begin_token = std::format("{}{}: ", indent, obj.name);
      obj.end_token = "\n";
    }
  }
}

void YAMLSerializer::OnRootBegin(std::string_view name) {
  if (_status.failed()) {
    return;
  }
  Object obj{.id = Snowflake::Generate(_machine_id),
             .object_type = ObjectType::kRoot,
             .value_type = ValueType::kString,
             .name = std::string(name),
             .begin_token = "",
             .children = {},
             .value = {},
             .end_token = ""};

  InjectAppropriateToken(obj);

  _objects.push(obj);
  PushState(kField);  // 루트 자식 = 필드 컨텍스트

  return;
}
void YAMLSerializer::OnRootEnd() {
  if (_status.failed()) {
    return;
  }
  if (_objects.size() != 1) {
    std::string_view fmt =
        GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                      locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
    _status = Status(TranscriberError::kCorrupted,
                     std::vformat(fmt, std::make_format_args("root object")));
    return;
  }
  _root_object = std::make_unique<Object>(std::move(_objects.top()));
  _objects.pop();
  PopState();  // 루트 컨텍스트 종료
  return;
}

void YAMLSerializer::OnObjectBegin(std::string_view name) {
  if (_status.failed()) {
    return;
  }
  BeginContainer(name, ValueType::kNull, kField);  // 객체 자식 = 필드
}
void YAMLSerializer::OnObjectEnd() {
  if (_status.failed() || _objects.empty()) {
    return;
  }
  auto object_popped = _objects.top();
  _objects.pop();
  _objects.top().children.push_back(object_popped);
  PopState();  // 자식 컨텍스트 종료
}

std::size_t YAMLSerializer::OnSeqBegin(std::string_view name,
                                       std::size_t count) {
  if (_status.failed()) {
    return std::numeric_limits<std::size_t>::max();
  }
  BeginContainer(name, ValueType::kSequence, kSeqItem);  // 자식 = 시퀀스 원소
  return count;
}
void YAMLSerializer::OnSeqEnd() { return OnObjectEnd(); }

std::size_t YAMLSerializer::OnMapBegin(std::string_view name,
                                       std::size_t count) {
  if (_status.failed()) {
    return std::numeric_limits<std::size_t>::max();
  }
  BeginContainer(name, ValueType::kMap, kMapKey);  // 자식 = 키(→값→키…)
  return count;
}
void YAMLSerializer::OnMapEnd() { return OnObjectEnd(); }

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

// ── State 싱글턴 (무상태) ──
const YAMLSerializer::FieldState YAMLSerializer::kField{};
const YAMLSerializer::SeqItemState YAMLSerializer::kSeqItem{};
const YAMLSerializer::MapKeyState YAMLSerializer::kMapKey{};
const YAMLSerializer::MapValueState YAMLSerializer::kMapValue{};

// 객체/루트 필드: name: value 그대로. (페이로드는 EmitScalar가 stash한 멤버에서)
void YAMLSerializer::FieldState::OnScalar(Visitor& v) const {
  auto& s = static_cast<YAMLSerializer&>(v);
  s.EmitValueNode(s._scalar_name, s._scalar_str, s._scalar_type);
}
std::string YAMLSerializer::FieldState::ResolveName(Visitor&,
                                                    std::string_view name) const {
  return std::string(name);
}

// 시퀀스 원소: 이름 무시, kSequence 플래그로 '- value'.
void YAMLSerializer::SeqItemState::OnScalar(Visitor& v) const {
  auto& s = static_cast<YAMLSerializer&>(v);
  s.EmitValueNode("", s._scalar_str, s._scalar_type | ValueType::kSequence);
}
std::string YAMLSerializer::SeqItemState::ResolveName(Visitor&,
                                                      std::string_view) const {
  return "";  // 시퀀스 원소 컨테이너 (중첩은 후속)
}

// map 키: 렌더 문자열을 캡처하고 값 상태로 전이.
void YAMLSerializer::MapKeyState::OnScalar(Visitor& v) const {
  auto& s = static_cast<YAMLSerializer&>(v);
  s._pending_key = s._scalar_str;
  s.TransitionTo(kMapValue);
}
std::string YAMLSerializer::MapKeyState::ResolveName(Visitor&,
                                                     std::string_view name) const {
  return std::string(name);  // 복합 키는 후속
}

// map 값: 대기 키를 name으로 하여 'key: value', 키 상태로 전이.
void YAMLSerializer::MapValueState::OnScalar(Visitor& v) const {
  auto& s = static_cast<YAMLSerializer&>(v);
  s.EmitValueNode(s._pending_key, s._scalar_str, s._scalar_type);
  s.TransitionTo(kMapKey);
}
std::string YAMLSerializer::MapValueState::ResolveName(
    Visitor& v, std::string_view) const {
  auto& s = static_cast<YAMLSerializer&>(v);
  std::string key = s._pending_key;  // 컨테이너 값도 키를 이름으로
  s.TransitionTo(kMapKey);
  return key;
}

// 컨테이너 진입 공통: 이름 결정 → 노드 생성/push → 자식용 상태 push.
void YAMLSerializer::BeginContainer(std::string_view name,
                                    ValueType container_type,
                                    const State& child_state) {
  std::string real_name = CurrentState().ResolveName(*this, name);
  Object obj{.id = Snowflake::Generate(_machine_id),
             .object_type = ObjectType::kObject,
             .value_type = container_type,
             .name = std::move(real_name),
             .begin_token = "",
             .children = {},
             .value = {},
             .end_token = ""};
  InjectAppropriateToken(obj);
  _objects.push(std::move(obj));
  PushState(child_state);
}

// 스칼라 값 노드 하나를 만들어 현재 부모의 children에 추가.
void YAMLSerializer::EmitValueNode(std::string_view name, std::string str,
                                   ValueType type) {
  auto bytes_view = std::as_bytes(std::span{str});
  Object obj{
      .id = Snowflake::Generate(_machine_id),
      .object_type = ObjectType::kValue,
      .value_type = type,
      .name = std::string(name),
      .begin_token = "",
      .children = {},
      .value = std::vector<std::byte>(bytes_view.begin(), bytes_view.end()),
      .end_token = ""};
  InjectAppropriateToken(obj);
  _objects.top().children.push_back(std::move(obj));
}

Status YAMLSerializer::EmitScalar(std::string_view name, std::string value_str,
                                  ValueType type) {
  if (_status.failed()) {
    return _status;
  }
  if (_objects.empty()) {
    std::string_view fmt =
        GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                      locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
    return Status(TranscriberError::kCorrupted,
                  std::vformat(fmt, std::make_format_args(name)));
  }
  // 페이로드를 멤버에 stash한 뒤 현재 상태에 위임 (필드/시퀀스/맵키/맵값).
  // 베이스 State 인터페이스는 ValueType를 모르므로 파생 상태가 여기서 읽는다.
  _scalar_name = std::string(name);
  _scalar_str = std::move(value_str);
  _scalar_type = type;
  CurrentState().OnScalar(*this);
  return _status;
}

YAMLSerializer& YAMLSerializer::Visit(std::string_view name, bool& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name, std::byte& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::int8_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::uint8_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::int16_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::uint16_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::int32_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::uint32_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::int64_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::uint64_t& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name, float& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name, double& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::vector<std::byte>& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
YAMLSerializer& YAMLSerializer::Visit(std::string_view name,
                                      std::string& value) {
  auto [value_str, type] = RenderOrdinaryValueString(value);
  _status = EmitScalar(name, std::move(value_str), type);
  return *this;
}
}  // namespace bedrock::archive::transcriber
