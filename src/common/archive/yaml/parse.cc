/**
 * @file parse.cc
 * @brief YAML presentation stream을 serialization tree로 Parse한다.
 */
#include "archive/yaml/parse.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "archive/yaml/event.h"
#include "archive/yaml/grammar.h"
#include "common/util/unicode.h"

namespace bedrock::archive::yaml {
namespace {

/** @brief 줄 나눔(CRLF|CR|LF)의 길이. 아니면 0. */
std::size_t BreakLen(std::span<const std::uint32_t> cps, std::size_t index) {
  if (index >= cps.size()) {
    return 0;
  }
  if (cps[index] == '\r') {
    return (index + 1 < cps.size() && cps[index + 1] == '\n') ? 2 : 1;
  }
  return cps[index] == '\n' ? 1 : 0;
}

/** @brief 공백류(스페이스/탭)인지. */
bool IsWhite(std::uint32_t character) {
  return character == ' ' || character == '\t';
}

/** @brief 원문 한 행 — 본문 구간과 뒤따르는 줄 나눔 존재 여부. */
struct RawLine {
  std::span<const std::uint32_t> text;
  bool had_break;
};

/** @brief 구간을 행 단위로 분할한다(줄 나눔은 본문에서 제외). */
std::vector<RawLine> SplitLines(std::span<const std::uint32_t> cps) {
  std::vector<RawLine> lines;
  std::size_t start = 0;
  std::size_t index = 0;
  while (index < cps.size()) {
    const std::size_t byte_value = BreakLen(cps, index);
    if (byte_value != 0U) {
      lines.push_back({cps.subspan(start, index - start), true});
      index += byte_value;
      start = index;
    } else {
      index++;
    }
  }
  lines.push_back({cps.subspan(start), false});
  return lines;
}

/** @brief 앞뒤 공백류를 잘라낸 구간(선두는 strip_lead일 때만). */
std::span<const std::uint32_t> Trim(std::span<const std::uint32_t> scalar,
                                    bool strip_lead) {
  std::size_t byte_value = 0;
  std::size_t event = scalar.size();
  if (strip_lead) {
    while (byte_value < event && IsWhite(scalar[byte_value])) {
      byte_value++;
    }
  }
  while (event > byte_value && IsWhite(scalar[event - 1])) {
    event--;
  }
  return scalar.subspan(byte_value, event - byte_value);
}

/**
 * @brief plain/홑따옴표 스칼라 디코드 — 접힘 + (홑따옴표) '' 축약.
 *
 * 행별로 경계 공백을 제거한 뒤 접는다: 내용 행 사이 빈 행 k개는 \n k개,
 * 빈 행이 없으면 공백 하나.
 */
std::string DecodeFlowFolded(std::span<const std::uint32_t> cps,
                             bool single_quoted) {
  const std::vector<RawLine> lines = SplitLines(cps);
  std::string out;
  auto append_line = [&](std::span<const std::uint32_t> length) {
    for (std::size_t index = 0; index < length.size();) {
      if (single_quoted && length[index] == '\'' && index + 1 < length.size() &&
          length[index + 1] == '\'') {
        out += '\'';
        index += 2;
        continue;
      }
      util::AppendUtf8(out, static_cast<char32_t>(length[index]));
      index++;
    }
  };
  bool first = true;
  std::size_t pending_empties = 0;
  for (const auto& line : lines) {
    const std::span<const std::uint32_t> length = Trim(line.text, !first);
    if (first) {
      append_line(length);
      first = false;
      continue;
    }
    if (length.empty()) {
      pending_empties++;
      continue;
    }
    if (pending_empties != 0U) {
      out.append(pending_empties, '\n');
      pending_empties = 0;
    } else {
      out += ' ';
    }
    append_line(length);
  }
  return out;  // 구간은 내용으로 끝나므로 후행 빈 행은 없다
}

/** @brief 16진 이스케이프(xNN/uNNNN/UNNNNNNNN)의 값을 읽는다. */
char32_t ReadHex(std::span<const std::uint32_t> cps, std::size_t index,
                 std::size_t digits) {
  std::uint32_t value = 0;
  for (std::size_t key = 0; key < digits && index + key < cps.size(); key++) {
    const std::uint32_t character = cps[index + key];
    std::uint32_t digit = 0;
    if ('0' <= character && character <= '9') {
      digit = character - '0';
    } else if ('A' <= character && character <= 'F') {
      digit = character - 'A' + 10;
    } else if ('a' <= character && character <= 'f') {
      digit = character - 'a' + 10;
    }
    value = (value << 4) | digit;
  }
  return static_cast<char32_t>(value);
}

/**
 * @brief 겹따옴표 스칼라 디코드 — 전체 이스케이프 집합([42]~[62]) 해석
 * 후 접힘. 이스케이프된 줄바꿈은 아무것도 내지 않고(조인), 뒤따르는
 * 빈 행들은 각각 줄바꿈이 된다.
 */
std::string DecodeDouble(std::span<const std::uint32_t> cps) {
  std::string out;
  out.reserve(cps.size());
  std::size_t raw_ws = 0;  // 현재 행 끝의 원시 공백 바이트 수
  std::size_t breaks = 0;  // 마지막 내용 이후 원시 줄바꿈 수
  bool joined = false;     // 이스케이프된 줄바꿈을 봤는가
  bool at_line_start = false;
  auto flush_sep = [&] {
    if (breaks == 0) {
      return;
    }
    if (joined) {
      out.append(breaks, '\n');  // 조인 뒤의 빈 행들은 각각 줄바꿈
    } else if (breaks == 1) {
      out += ' ';
    } else {
      out.append(breaks - 1, '\n');
    }
    breaks = 0;
    joined = false;
  };
  auto append = [&](char32_t code_point, bool escaped) {
    const bool raw_white =
        !escaped && IsWhite(static_cast<std::uint32_t>(code_point));
    if (!escaped && code_point == U'\n') {
      out.resize(out.size() - raw_ws);  // 원시 후행 공백 제거
      raw_ws = 0;
      breaks++;
      at_line_start = true;
      return;
    }
    if (at_line_start && raw_white) {
      return;  // 행 선두 원시 공백은 들여쓰기 — 제거
    }
    at_line_start = false;
    flush_sep();
    util::AppendUtf8(out, code_point);
    raw_ws = raw_white ? raw_ws + 1 : 0;
  };

  for (std::size_t index = 0; index < cps.size();) {
    if (const std::size_t block_length = BreakLen(cps, index)) {
      append(U'\n', false);
      index += block_length;
      continue;
    }
    if (cps[index] != '\\') {
      append(static_cast<char32_t>(cps[index]), false);
      index++;
      continue;
    }
    const std::uint32_t first_character =
        index + 1 < cps.size() ? cps[index + 1] : 0;
    if (BreakLen(cps, index + 1) != 0U) {  // 이스케이프된 줄바꿈
      // 앞의 원시 공백은 내용으로 보존하고 다음 행의 들여쓰기는 제거한다.
      raw_ws = 0;
      joined = true;
      at_line_start = true;
      index += 1 + BreakLen(cps, index + 1);
      continue;
    }
    char32_t code_point = 0;
    std::size_t len = 2;
    switch (first_character) {
      case '0':
        code_point = 0x00;
        break;
      case 'a':
        code_point = 0x07;
        break;
      case 'b':
        code_point = 0x08;
        break;
      case 't':
      case '\t':
        code_point = 0x09;
        break;
      case 'n':
        code_point = 0x0A;
        break;
      case 'v':
        code_point = 0x0B;
        break;
      case 'f':
        code_point = 0x0C;
        break;
      case 'r':
        code_point = 0x0D;
        break;
      case 'e':
        code_point = 0x1B;
        break;
      case ' ':
        code_point = 0x20;
        break;
      case '"':
        code_point = 0x22;
        break;
      case '/':
        code_point = 0x2F;
        break;
      case '\\':
        code_point = 0x5C;
        break;
      case 'N':
        code_point = 0x85;
        break;
      case '_':
        code_point = 0xA0;
        break;
      case 'L':
        code_point = 0x2028;
        break;
      case 'P':
        code_point = 0x2029;
        break;
      case 'x':
        code_point = ReadHex(cps, index + 2, 2);
        len = 4;
        break;
      case 'u':
        code_point = ReadHex(cps, index + 2, 4);
        len = 6;
        break;
      case 'U':
        code_point = ReadHex(cps, index + 2, 8);
        len = 10;
        break;
      default:
        code_point = static_cast<char32_t>(first_character);
        break;  // 문법상 도달 불가
    }
    append(code_point, true);
    index += len;
  }
  if ((breaks != 0U) || joined) {
    flush_sep();  // 내용이 줄바꿈으로 끝난 경우(예: "a\n" -> "a ")
  }
  return out;
}

/**
 * @brief 블록 스칼라(| / >) 디코드 — 들여쓰기 제거, (폴디드) 접힘, chomping.
 *
 * 내용 행은 들여쓰기 indent 이상이거나 공백뿐인 행이다. 그 조건을 깨는
 * 첫 행(후행 주석 등)에서 내용이 끝난다.
 */
std::string DecodeBlock(std::span<const std::uint32_t> cps,
                        std::ptrdiff_t indent, ChompKind chomp, bool folded) {
  const std::size_t ind =
      indent > 0 ? static_cast<std::size_t>(indent) : std::size_t{0};
  const std::vector<RawLine> raw = SplitLines(cps);
  // 행 분류: 들여쓰기 제거 본문 + 내용 경계 판정
  struct Line {
    std::string text;  // 들여쓰기 제거 후(UTF-8)
    bool had_break;
  };
  std::vector<Line> lines;
  for (const RawLine& remaining_length : raw) {
    std::size_t lead = 0;
    while (lead < remaining_length.text.size() &&
           remaining_length.text[lead] == ' ') {
      lead++;
    }
    const bool all_ws = lead == remaining_length.text.size();
    if (all_ws && remaining_length.text.size() <= ind) {
      lines.push_back({std::string(), remaining_length.had_break});  // 빈 행
      continue;
    }
    if (lead < ind && !all_ws) {
      break;  // 들여쓰기 미달 내용 행 — 후행 주석/chomping 밖
    }
    // 내용 행(공백만이라도 indent 초과분은 내용)
    lines.push_back({util::EncodeUtf8(remaining_length.text.subspan(ind)),
                     remaining_length.had_break});
  }
  // 마지막 원소가 "빈 행 + 줄바꿈 없음"이면 구간 끝의 자투리 — 내용 아님
  if (!lines.empty() && lines.back().text.empty() && !lines.back().had_break) {
    lines.pop_back();
  }
  // 후행 빈 행 수(t)와 내용 행 수(k)
  std::size_t key = lines.size();
  while (key > 0 && lines[key - 1].text.empty()) {
    key--;
  }
  const std::size_t trailing = lines.size() - key;
  std::string body;
  if (!folded) {
    for (std::size_t index = 0; index < key; index++) {
      body += lines[index].text;
      if (index + 1 < key) {
        body += '\n';
      }
    }
  } else {
    // 폴디드 접힘: 보통 행끼리 이웃하면 공백, 빈 행 k개는 \n k개,
    // 더 들여쓴(공백/탭 시작) 행 경계는 줄바꿈 그대로
    int prev = 0;  // 0=없음, 1=보통, 2=더 들여씀
    std::size_t pending_empties = 0;
    for (std::size_t index = 0; index < key; index++) {
      const std::string& tag = lines[index].text;
      if (tag.empty()) {
        pending_empties++;
        continue;
      }
      const int kind = (tag[0] == ' ' || tag[0] == '\t') ? 2 : 1;
      if (prev != 0) {
        if (pending_empties != 0U) {
          body.append(pending_empties, '\n');
        } else if (prev == 1 && kind == 1) {
          body += ' ';
        } else {
          body += '\n';
        }
      }
      pending_empties = 0;
      body += tag;
      prev = kind;
    }
  }
  // chomping
  const bool last_had_break =
      key > 0 && (trailing > 0 || lines[key - 1].had_break);
  if (chomp == ChompKind::kStrip) {
    return body;
  }
  if (chomp == ChompKind::kClip) {
    if (!body.empty() && last_had_break) {
      body += '\n';
    }
    return body;
  }
  // KEEP: 마지막 내용 행의 줄바꿈 + 후행 빈 행들을 전부 보존
  if (key > 0) {
    if (lines[key - 1].had_break || trailing > 0) {
      body += '\n';
    }
  }
  for (std::size_t index = key; index < lines.size(); index++) {
    if (lines[index].had_break) {
      body += '\n';
    }
  }
  return body;
}

/** @brief 이벤트 구간의 원문을 UTF-8로 뽑는다. */
std::string SpanText(std::span<const std::uint32_t> buf, const Event& event) {
  return util::EncodeUtf8(buf.subspan(event.begin, event.end - event.begin));
}

struct ParsedScalar {
  bool null = false;
  transcriber::ValueType value_type = transcriber::ValueType::kNull;
  std::string value;
};

ParsedScalar ParseScalar(std::span<const std::uint32_t> buffer,
                         const Event& presentation_event) {
  ParsedScalar parsed;
  const std::span<const std::uint32_t> body =
      buffer.subspan(presentation_event.begin,
                     presentation_event.end - presentation_event.begin);
  if (presentation_event.style == ScalarStyle::kPlain) {
    parsed.null = body.empty();
    parsed.value = parsed.null ? std::string() : DecodeFlowFolded(body, false);
  } else if (presentation_event.style == ScalarStyle::kSingleQuoted) {
    parsed.value_type = transcriber::ValueType::kString;
    parsed.value = DecodeFlowFolded(body, true);
  } else if (presentation_event.style == ScalarStyle::kDoubleQuoted) {
    parsed.value_type = transcriber::ValueType::kString;
    parsed.value = DecodeDouble(body);
  } else {
    parsed.value_type = transcriber::ValueType::kString;
    parsed.value =
        DecodeBlock(body, presentation_event.indent, presentation_event.chomp,
                    presentation_event.style == ScalarStyle::kFolded);
  }
  return parsed;
}

struct SerializationBuild {
  SerializationNode node;
  SerializationNode key;
  bool has_key = false;
};

bool AttachSerialization(std::vector<SerializationBuild>& stack,
                         SerializationNode& root, bool& have_root,
                         SerializationNode&& node) {
  if (stack.empty()) {
    root = std::move(node);
    have_root = true;
    return true;
  }
  SerializationBuild& top = stack.back();
  if (top.node.kind == SerializationNode::Kind::kSequence) {
    top.node.items.push_back(std::move(node));
    return true;
  }
  if (top.node.kind == SerializationNode::Kind::kMapping) {
    if (!top.has_key) {
      top.key = std::move(node);
      top.has_key = true;
    } else {
      top.node.pairs.push_back({std::move(top.key), std::move(node)});
      top.has_key = false;
    }
    return true;
  }
  return false;
}

std::string EventText(std::span<const std::uint32_t> buffer,
                      const Event& event) {
  return util::EncodeUtf8(buffer.subspan(event.begin, event.end - event.begin));
}

/** @brief decoded serialization event를 materialized tree로 조립한다. */
class SerializationTreeSink final : public SerializationSink {
 public:
  explicit SerializationTreeSink(ParseResult& result) : result_(result) {}

  bool OnEvent(SerializationEvent&& event) final {
    switch (event.kind) {
      case SerializationEventKind::kDocStart:
        have_root_ = false;
        return true;
      case SerializationEventKind::kDocEnd:
        if (!stack_.empty()) {
          return Fail("container remains open at document end");
        }
        if (!have_root_) {
          root_ = SerializationNode{};
          root_.null = true;
        }
        result_.serialization.documents.push_back(std::move(root_));
        root_ = SerializationNode{};
        have_root_ = false;
        return true;
      case SerializationEventKind::kNode:
        if (!AttachSerialization(stack_, root_, have_root_,
                                 std::move(event.node))) {
          return Fail("node has no parent");
        }
        return true;
      case SerializationEventKind::kMapStart:
      case SerializationEventKind::kSeqStart: {
        SerializationBuild build;
        build.node = std::move(event.node);
        stack_.push_back(std::move(build));
        return true;
      }
      case SerializationEventKind::kMapEnd:
      case SerializationEventKind::kSeqEnd: {
        if (stack_.empty()) {
          return Fail("container closes without a matching start");
        }
        SerializationBuild build = std::move(stack_.back());
        stack_.pop_back();
        if (build.has_key) {
          return Fail("mapping key has no value");
        }
        if (!AttachSerialization(stack_, root_, have_root_,
                                 std::move(build.node))) {
          return Fail("container has no parent");
        }
        return true;
      }
    }
    return false;
  }

  bool Finish() {
    if (!stack_.empty()) {
      return Fail("container remains open at stream end");
    }
    result_.ok = true;
    return true;
  }

 private:
  bool Fail(std::string error) {
    result_.error = std::move(error);
    return false;
  }

  ParseResult& result_;
  std::vector<SerializationBuild> stack_;
  SerializationNode root_;
  bool have_root_ = false;
};

/** @brief presentation-aware event를 decoded serialization event로 변환한다. */
class SerializationEventDecoder final : public EventSink {
 public:
  SerializationEventDecoder(std::span<const std::uint32_t> buffer,
                            SerializationSink& sink, std::string& error)
      : buffer_(buffer), sink_(sink), error_(error) {}

  bool OnEvent(const Event& event) final {
    SerializationEvent output;
    switch (event.kind) {
      case EventKind::kDocStart:
        output.kind = SerializationEventKind::kDocStart;
        return sink_.OnEvent(std::move(output));
      case EventKind::kDocEnd:
        output.kind = SerializationEventKind::kDocEnd;
        return sink_.OnEvent(std::move(output));
      case EventKind::kAnchor:
        pending_anchor_ = EventText(buffer_, event);
        return true;
      case EventKind::kTag:
        pending_tag_ = EventText(buffer_, event);
        return true;
      case EventKind::kAlias:
        output.kind = SerializationEventKind::kNode;
        output.node.kind = SerializationNode::Kind::kAlias;
        output.node.alias = EventText(buffer_, event);
        return sink_.OnEvent(std::move(output));
      case EventKind::kScalar: {
        output.kind = SerializationEventKind::kNode;
        output.node.kind = SerializationNode::Kind::kScalar;
        ApplyProperties(output.node);
        ParsedScalar scalar = ParseScalar(buffer_, event);
        output.node.null = scalar.null;
        output.node.value_type = scalar.value_type;
        output.node.scalar = std::move(scalar.value);
        if (output.node.tag == "!!binary") {
          output.node.value_type = transcriber::ValueType::kBinary;
        }
        return sink_.OnEvent(std::move(output));
      }
      case EventKind::kMapStart:
      case EventKind::kSeqStart:
        output.kind = event.kind == EventKind::kMapStart
                          ? SerializationEventKind::kMapStart
                          : SerializationEventKind::kSeqStart;
        output.node.kind = event.kind == EventKind::kMapStart
                               ? SerializationNode::Kind::kMapping
                               : SerializationNode::Kind::kSequence;
        ApplyProperties(output.node);
        return sink_.OnEvent(std::move(output));
      case EventKind::kMapEnd:
        output.kind = SerializationEventKind::kMapEnd;
        return sink_.OnEvent(std::move(output));
      case EventKind::kSeqEnd:
        output.kind = SerializationEventKind::kSeqEnd;
        return sink_.OnEvent(std::move(output));
    }
    error_ = "unknown presentation event";
    return false;
  }

 private:
  void ApplyProperties(SerializationNode& node) {
    node.tag = std::move(pending_tag_);
    node.anchor = std::move(pending_anchor_);
    pending_tag_.clear();
    pending_anchor_.clear();
  }

  std::span<const std::uint32_t> buffer_;
  SerializationSink& sink_;
  std::string& error_;
  std::string pending_tag_;
  std::string pending_anchor_;
};

}  // namespace

ParseResult Parse(std::u32string_view presentation) {
  const std::vector<std::uint32_t> buffer(presentation.begin(),
                                          presentation.end());
  return Parse(buffer);
}

ParseResult Parse(std::span<const std::uint32_t> presentation) {
  ParseResult result;
  SerializationTreeSink sink(result);
  if (!ParseToSerializationSink(presentation, sink, result.error)) {
    return result;
  }
  if (!sink.Finish()) {
    return result;
  }
  return result;
}

bool ParseToSerializationSink(std::span<const std::uint32_t> presentation,
                              SerializationSink& sink, std::string& error) {
  Diag diag;
  Grammar::Arena arena;
  Grammar::ParseState state;
  state.before = presentation.first(0);
  state.cps = presentation;
  state.diag = &diag;
  state.arena = &arena;
  static_cast<void>(Grammar::IslYamlStream(state));
  assert(state.frame == Grammar::kNoArenaEntry);

  if (state.before.size() != presentation.size()) {
    const std::size_t offset = diag.furthest > state.before.size()
                                   ? diag.furthest
                                   : state.before.size();
    const LineCol location = OffsetToLineCol(presentation, offset);
    error =
        std::format("YAML parse failed at {}:{}", location.line, location.col);
    return false;
  }

  SerializationEventDecoder decoder(presentation, sink, error);
  return EmitEvents(Grammar::Materialize(arena, state.roots), decoder);
}

}  // namespace bedrock::archive::yaml
