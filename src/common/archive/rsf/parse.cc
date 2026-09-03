/**
 * @file parse.cc
 * @brief RSF Parse(텍스트 -> 이벤트) 구현부.
 */
#include "archive/rsf/parse.h"

#include <cctype>
#include <cstdint>

namespace bedrock::archive::rsf {

namespace {

/** @brief 문자열을 훑으며 이벤트를 방출하는 재귀 하강 파서. */
struct Parser {
  std::span<const char> s;
  std::size_t pos = 0;
  std::vector<Event> events;
  bool ok = true;
  std::string error;

  static constexpr int kMaxDepth = 100;

  void Fail(const char* message) {
    if (ok) {
      ok = false;
      error = std::string("rsf: ") + message;
    }
  }
  [[nodiscard]] bool Eof() const { return pos >= s.size(); }
  [[nodiscard]] char Peek() const { return pos < s.size() ? s[pos] : '\0'; }
  void SkipWs() {
    while (pos < s.size()) {
      const auto character = static_cast<unsigned char>(s[pos]);
      if (character == ' ' || character == '\t' || character == '\n' ||
          character == '\r') {
        ++pos;
      } else {
        break;
      }
    }
  }
  /** @brief 16진 한 자리 -> 값(실패 시 -1). */
  static int HexVal(char character) {
    if (character >= '0' && character <= '9') {
      return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
      return 10 + (character - 'a');
    }
    if (character >= 'A' && character <= 'F') {
      return 10 + (character - 'A');
    }
    return -1;
  }
  /** @brief 여는 따옴표 다음부터 닫는 따옴표까지 읽어 이스케이프를 해제한다. */
  std::string ReadQuoted() {
    std::string out;
    if (Peek() != '"') {
      Fail("expected opening quote");
      return out;
    }
    ++pos;
    while (pos < s.size()) {
      const char character = s[pos++];
      if (character == '"') {
        return out;
      }
      if (character == '\\') {
        if (pos >= s.size()) {
          break;
        }
        const char event = s[pos++];
        if (event == '\\') {
          out.push_back('\\');
        } else if (event == '"') {
          out.push_back('"');
        } else if (event == 'x') {
          if (pos + 1 >= s.size()) {
            Fail("bad \\x escape");
            return out;
          }
          const int high_nibble = HexVal(s[pos]);
          const int low_nibble = HexVal(s[pos + 1]);
          if (high_nibble < 0 || low_nibble < 0) {
            Fail("bad hex digit");
            return out;
          }
          pos += 2;
          out.push_back(static_cast<char>((high_nibble << 4) | low_nibble));
        } else {
          Fail("unknown escape");
          return out;
        }
      } else {
        out.push_back(character);
      }
    }
    Fail("unterminated string");
    return out;
  }

  void ParseValue(int depth) {
    if (!ok) {
      return;
    }
    if (depth > kMaxDepth) {
      Fail("max depth");
      return;
    }
    SkipWs();
    if (Eof()) {
      Fail("unexpected end");
      return;
    }
    const char character = Peek();
    if (character == '[' || character == '(' || character == '{') {
      ++pos;
      const bool is_seq = character == '[';
      const bool is_set = character == '(';
      char close = '}';
      EventKind start_kind = EventKind::kMapStart;
      EventKind end_kind = EventKind::kMapEnd;
      if (is_seq) {
        close = ']';
        start_kind = EventKind::kSeqStart;
        end_kind = EventKind::kSeqEnd;
      } else if (is_set) {
        close = ')';
        start_kind = EventKind::kSetStart;
        end_kind = EventKind::kSetEnd;
      }
      events.push_back(
          Event{start_kind, false, transcriber::ValueType::kNull, {}});
      for (;;) {
        if (!ok) {
          return;
        }
        SkipWs();
        if (Eof()) {
          Fail("unterminated container");
          return;
        }
        if (Peek() == close) {
          ++pos;
          break;
        }
        ParseValue(depth + 1);
      }
      events.push_back(
          Event{end_kind, false, transcriber::ValueType::kNull, {}});
      return;
    }
    // 스칼라
    Event event;
    event.kind = EventKind::kScalar;
    if (character == 'Z') {
      ++pos;
      event.null = true;
      event.vtype = transcriber::ValueType::kNull;
      events.push_back(std::move(event));
      return;
    }
    transcriber::ValueType value_type = transcriber::ValueType::kNull;
    if (character == 'P') {
      value_type = transcriber::ValueType::kNull;
    } else if (character == 'N') {
      value_type = transcriber::ValueType::kNumber;
    } else if (character == 'S') {
      value_type = transcriber::ValueType::kString;
    } else if (character == 'B') {
      value_type = transcriber::ValueType::kBoolean;
    } else if (character == 'Y') {
      value_type = transcriber::ValueType::kBinary;
    } else {
      Fail("unexpected token");
      return;
    }
    ++pos;
    event.null = false;
    event.vtype = value_type;
    event.scalar = ReadQuoted();
    if (!ok) {
      return;
    }
    events.push_back(std::move(event));
  }
};

}  // namespace

ParseResult Parse(std::span<const char> text) {
  ParseResult out;
  Parser position{text};
  position.ParseValue(0);
  if (position.ok) {
    position.SkipWs();
    if (!position.Eof()) {
      position.Fail("trailing data");
    }
  }
  if (!position.ok) {
    out.error = std::move(position.error);
    return out;
  }
  out.ok = true;
  out.events = std::move(position.events);
  return out;
}

}  // namespace bedrock::archive::rsf
