/**
 * @file yaml_serializer.cc
 * @brief YAMLSerializer 구현부(포맷 훅: Present).
 *
 * 트리→YAML 텍스트 렌더링과 그에 필요한 인용/이스케이프 규칙을 담는다.
 * 제시 규칙이 이 파일에만 존재하므로, 읽기 경로의 디코더
 * (yaml/compose.cc)와 왕복 대칭을 맞출 때 볼 곳이 한 곳으로 고정된다.
 */
#include "common/archive/transcriber/yaml_serializer.h"

#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include "common/i18n/locales.h"
#include "common/util/unicode.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief "…를 파싱하는 동안 손상" 상태(대상 이름 포함). */
Status CorruptedWhile(std::string_view what) {
  const std::string_view fmt =
      GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                    locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
  return Status(TranscriberError::kCorrupted,
                std::vformat(fmt, std::make_format_args(what)));
}

/**
 * @brief 코드포인트가 YAML 1.2 c-printable 집합에 속하는지 검사한다.
 *
 * 이 집합 밖의 코드포인트(C0/DEL/C1 제어 문자, 비문자, 서로게이트)는
 * 이스케이프한다. PyYAML의 허용 집합과 동일하다.
 * @param cp 검사할 유니코드 코드포인트.
 * @return 인쇄 가능하면 true.
 */
bool IsYamlPrintable(char32_t cp) {
  return cp == 0x09 || cp == 0x0A || cp == 0x0D ||  // tab, LF, CR
         (0x20 <= cp && cp <= 0x7E) ||              // 인쇄 ASCII
         cp == 0x85 ||                              // NEL
         (0xA0 <= cp && cp <= 0xD7FF) ||            // BMP (서로게이트 전)
         (0xE000 <= cp && cp <= 0xFFFD) ||          // BMP (서로게이트 후)
         (0x10000 <= cp && cp <= 0x10FFFF);         // astral
}

/**
 * @brief YAML(1.1)이 줄바꿈으로 취급하는 문자인지 검사한다.
 * @param cp 검사할 유니코드 코드포인트.
 * @return LF/CR/NEL/LS/PS 중 하나면 true.
 */
bool IsYamlLineBreak(char32_t cp) {
  return cp == 0x0A || cp == 0x0D || cp == 0x85 || cp == 0x2028 || cp == 0x2029;
}

/**
 * @brief YAML 이중 인용(double-quoted) 스칼라용으로 이스케이프한다.
 *
 * \ 와 " 및 비인쇄 문자만 이스케이프한다. double-quoted는
 * \xNN/\uNNNN/\UNNNNNNNN 으로 모든 코드포인트를 표현할 수 있으므로
 * 버리지 않고 이스케이프해 왕복을 보장한다. NEL/LS/PS도 줄 접힘을
 * 피하려 이스케이프한다.
 * @param in 원본 바이트열(UTF-8).
 * @return 이스케이프된 문자열.
 */
std::string EscapeForDoubleQuoted(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  util::ForEachUtf8Char(in, [&](char32_t cp, const char* bytes,
                                std::size_t len, bool valid) {
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
      default:
        break;
    }
    if (IsYamlPrintable(cp) && !IsYamlLineBreak(cp)) {
      out.append(bytes, len);  // 인쇄 문자는 원본 UTF-8 그대로
    } else if (cp <= 0x7F) {
      out += std::format("\\x{:02X}", static_cast<std::uint32_t>(cp));
    } else if (cp <= 0xFFFF) {
      out += std::format("\\u{:04X}", static_cast<std::uint32_t>(cp));
    } else {
      out += std::format("\\U{:08X}", static_cast<std::uint32_t>(cp));
    }
  });
  return out;
}

/**
 * @brief base64 문자열을 감싸 !!binary 블록(|) 본문을 만든다.
 *
 * width자마다 개행하고 각 줄을 indent_chars칸 들여쓴다. base64는 전부
 * 인쇄 가능 문자라 이스케이프가 필요 없고, 블록 안의 개행은 base64
 * 디코더가 무시하므로 임의 폭으로 감싸도 왕복에 영향이 없다. 마지막
 * 줄에는 개행을 붙이지 않는다(호출자가 블록을 마감).
 * @param b64 base64로 인코딩된 문자열.
 * @param indent_chars 각 줄 앞에 붙일 공백 수.
 * @param width 한 줄에 담을 최대 문자 수.
 * @return 줄바꿈·들여쓰기가 적용된 블록 본문.
 */
std::string WrapBase64Block(std::string_view b64, std::size_t indent_chars,
                            std::size_t width = 76) {
  const std::string indent(indent_chars, ' ');
  std::string out;
  for (std::size_t i = 0; i < b64.size(); i += width) {
    if (i != 0) {
      out += '\n';
    }
    out += indent;
    out += b64.substr(i, width);
  }
  return out;
}

/** @brief 원소 컨테이너(시퀀스/집합)인지 — YAML은 집합을 시퀀스로 제시. */
bool IsItemContainer(const Node& n) {
  return n.kind == Node::Kind::kSequence || n.kind == Node::Kind::kSet;
}

}  // namespace

YAMLSerializer::~YAMLSerializer() { Flush(); }

Status YAMLSerializer::PresentDocument(const Node& root) {
  PresentBlock(root, 0);
  return _status;
}

void YAMLSerializer::PresentKey(const Node& key) {
  if (key.kind != Node::Kind::kScalar) {
    _status = CorruptedWhile("complex key");
    return;
  }
  if (HasFlag(key.vtype, ValueType::kString)) {
    // 문자열 키는 인용 — 임의 키(공백/콜론 포함)도 안전하게
    _output_stream << '"' << EscapeForDoubleQuoted(key.scalar) << '"';
    return;
  }
  _output_stream << key.scalar;  // 필드명/숫자/불리언 키는 plain
}

void YAMLSerializer::PresentEntryValue(const Node& n, std::size_t indent) {
  if (n.kind == Node::Kind::kScalar) {
    if (n.null) {
      _output_stream << "\n";  // 빈 값: "key:" 만
      return;
    }
    if (HasFlag(n.vtype, ValueType::kBinary)) {
      _output_stream << " !!binary |\n"
                     << WrapBase64Block(n.scalar, (indent + 1) * _indent_size)
                     << "\n";
      return;
    }
    if (HasFlag(n.vtype, ValueType::kString)) {
      _output_stream << " \"" << EscapeForDoubleQuoted(n.scalar) << "\"\n";
      return;
    }
    _output_stream << ' ' << n.scalar << "\n";  // 숫자/불리언 등 plain
    return;
  }
  // 빈 컨테이너는 블록 표기가 없으므로 플로우로
  if (n.kind == Node::Kind::kMapping && n.pairs.empty()) {
    _output_stream << " {}\n";
    return;
  }
  if (IsItemContainer(n) && n.items.empty()) {
    _output_stream << " []\n";
    return;
  }
  _output_stream << "\n";
  PresentBlock(n, indent + 1);
}

void YAMLSerializer::PresentBlock(const Node& n, std::size_t indent) {
  const std::string pad(indent * _indent_size, ' ');
  if (n.kind == Node::Kind::kMapping) {
    for (const Node::Pair& p : n.pairs) {
      if (_status.failed()) {
        return;
      }
      _output_stream << pad;
      PresentKey(p.key);
      _output_stream << ":";
      PresentEntryValue(p.value, indent);
    }
    return;
  }
  if (IsItemContainer(n)) {  // 집합은 시퀀스로 제시(!!set 표기는 후속)
    for (const Node& item : n.items) {
      if (_status.failed()) {
        return;
      }
      _output_stream << pad << "-";
      PresentEntryValue(item, indent);
    }
    return;
  }
  // 스칼라 루트(현재 Represent는 만들지 않음) — 한 줄로
  if (HasFlag(n.vtype, ValueType::kString)) {
    _output_stream << pad << '"' << EscapeForDoubleQuoted(n.scalar) << "\"\n";
  } else {
    _output_stream << pad << n.scalar << "\n";
  }
}

}  // namespace bedrock::archive::transcriber
