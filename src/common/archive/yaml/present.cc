/**
 * @file present.cc
 * @brief YAML serialization tree를 character stream으로 Present한다.
 */
#include "archive/yaml/present.h"

#include <cstddef>
#include <format>
#include <sstream>
#include <string>

#include "common/util/unicode.h"

namespace bedrock::archive::yaml {
namespace {

/**
 * @brief 코드포인트가 YAML 1.2 c-printable 집합에 속하는지 검사한다.
 *
 * 이 집합 밖의 코드포인트(C0/DEL/C1 제어 문자, 비문자, 서로게이트)는
 * 이스케이프한다. PyYAML의 허용 집합과 동일하다.
 * @param cp 검사할 유니코드 코드포인트.
 * @return 인쇄 가능하면 true.
 */
bool IsYamlPrintable(char32_t code_point) {
  return code_point == 0x09 || code_point == 0x0A ||
         code_point == 0x0D ||                            // tab, LF, CR
         (0x20 <= code_point && code_point <= 0x7E) ||    // 인쇄 ASCII
         code_point == 0x85 ||                            // NEL
         (0xA0 <= code_point && code_point <= 0xD7FF) ||  // BMP (서로게이트 전)
         (0xE000 <= code_point &&
          code_point <= 0xFFFD) ||  // BMP (서로게이트 후)
         (0x10000 <= code_point && code_point <= 0x10FFFF);  // astral
}

/**
 * @brief YAML(1.1)이 줄바꿈으로 취급하는 문자인지 검사한다.
 * @param cp 검사할 유니코드 코드포인트.
 * @return LF/CR/NEL/LS/PS 중 하나면 true.
 */
bool IsYamlLineBreak(char32_t code_point) {
  return code_point == 0x0A || code_point == 0x0D || code_point == 0x85 ||
         code_point == 0x2028 || code_point == 0x2029;
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
std::string EscapeForDoubleQuoted(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  util::ForEachUtf8Char(input, [&](char32_t code_point, const char* bytes,
                                   std::size_t len, bool valid) {
    if (!valid) {  // 깨진 바이트도 이스케이프로 보존
      out += std::format("\\x{:02X}", static_cast<unsigned char>(bytes[0]));
      return;
    }
    switch (code_point) {  // 짧은 이스케이프
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
    if (IsYamlPrintable(code_point) && !IsYamlLineBreak(code_point)) {
      out.append(bytes, len);  // 인쇄 문자는 원본 UTF-8 그대로
    } else if (code_point <= 0x7F) {
      out += std::format("\\x{:02X}", static_cast<std::uint32_t>(code_point));
    } else if (code_point <= 0xFFFF) {
      out += std::format("\\u{:04X}", static_cast<std::uint32_t>(code_point));
    } else {
      out += std::format("\\U{:08X}", static_cast<std::uint32_t>(code_point));
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

bool IsItemContainer(const SerializationNode& node) {
  return node.kind == SerializationNode::Kind::kSequence;
}

class Presenter {
 public:
  PresentResult Run(const SerializationStream& serialization) {
    if (serialization.documents.empty()) {
      return {false, "serialization stream has no document", {}};
    }
    for (std::size_t index = 0; index < serialization.documents.size();
         ++index) {
      if (index != 0U) {
        output_ << "---\n";
      }
      PresentBlock(serialization.documents[index], 0);
      if (!error_.empty()) {
        return {false, error_, {}};
      }
    }
    return {true, {}, output_.str()};
  }

 private:
  static constexpr std::size_t kIndentSize = 2;

  void PresentKey(const SerializationNode& key) {
    if (key.kind != SerializationNode::Kind::kScalar) {
      error_ = "complex mapping key is not supported";
      return;
    }
    if (transcriber::HasFlag(key.value_type, transcriber::ValueType::kString)) {
      output_ << '"' << EscapeForDoubleQuoted(key.scalar) << '"';
    } else {
      output_ << key.scalar;
    }
  }

  void PresentEntryValue(const SerializationNode& node, std::size_t indent) {
    if (node.kind == SerializationNode::Kind::kAlias) {
      output_ << " *" << node.alias << "\n";
      return;
    }
    if (node.kind == SerializationNode::Kind::kScalar) {
      if (!node.anchor.empty()) {
        output_ << " &" << node.anchor;
      }
      if (node.null) {
        output_ << "\n";
      } else if (transcriber::HasFlag(node.value_type,
                                      transcriber::ValueType::kBinary) ||
                 node.tag == "!!binary") {
        output_ << " !!binary |\n"
                << WrapBase64Block(node.scalar, (indent + 1) * kIndentSize)
                << "\n";
      } else if (transcriber::HasFlag(node.value_type,
                                      transcriber::ValueType::kString)) {
        output_ << " \"" << EscapeForDoubleQuoted(node.scalar) << "\"\n";
      } else {
        output_ << ' ' << node.scalar << "\n";
      }
      return;
    }
    if (!node.anchor.empty()) {
      output_ << " &" << node.anchor;
    }
    if (node.kind == SerializationNode::Kind::kMapping && node.pairs.empty()) {
      output_ << " {}\n";
      return;
    }
    if (IsItemContainer(node) && node.items.empty()) {
      output_ << " []\n";
      return;
    }
    output_ << "\n";
    PresentBlock(node, indent + 1);
  }

  void PresentBlock(const SerializationNode& node, std::size_t indent) {
    const std::string pad(indent * kIndentSize, ' ');
    if (node.kind == SerializationNode::Kind::kMapping) {
      for (const SerializationNode::Pair& pair : node.pairs) {
        output_ << pad;
        PresentKey(pair.key);
        if (!error_.empty()) {
          return;
        }
        output_ << ':';
        PresentEntryValue(pair.value, indent);
      }
      return;
    }
    if (node.kind == SerializationNode::Kind::kSequence) {
      for (const SerializationNode& item : node.items) {
        output_ << pad << '-';
        PresentEntryValue(item, indent);
      }
      return;
    }
    if (node.kind == SerializationNode::Kind::kAlias) {
      output_ << pad << '*' << node.alias << "\n";
    } else if (transcriber::HasFlag(node.value_type,
                                    transcriber::ValueType::kString)) {
      output_ << pad << '"' << EscapeForDoubleQuoted(node.scalar) << "\"\n";
    } else {
      output_ << pad << node.scalar << "\n";
    }
  }

  std::ostringstream output_;
  std::string error_;
};

}  // namespace

PresentResult Present(const SerializationStream& serialization) {
  Presenter presenter;
  return presenter.Run(serialization);
}

}  // namespace bedrock::archive::yaml
