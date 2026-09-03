/**
 * @file
 * @brief YAML 1.2.2 문법 인식기 정의.
 *
 * 두 세계로 나뉜다. [1]~[62]는 순수 문자/토큰 분류기 — 단일 코드포인트를
 * 받는 것은 cp(0=불일치)를, 여러 코드포인트를 보는 것은 남은 입력 span
 * (cps) 하나만 받아 매치 길이(0=불일치)를 반환하고, 위치를 소비하지
 * 않는다. 길이 부족은 span 크기 검사로 처리한다.
 *
 * [63]~[211]은 매처 — ParseState&(cur)를 받아 성공하면 cur를 매치 끝으로
 * 전진시키고 true를, 실패하면 false를 반환한다. 실패 시 cur는 임의
 * 지점까지 전진해 있을 수 있으므로, 백트래킹이 필요한 호출자는 복사한
 * trial에서 호출하고 성공할 때만 cur에 commit한다. 실패한 trial은
 * 폐기한다. 빈 매치는 cur를 전진시키지 않고 true를 반환한다.
 *
 * 스펙 RHS와 다른 lowering, 전역 제약의 분산 적용, 알려진 spec 결함의
 * 처리 근거는 각 production 선언의 Doxygen @details에 기록한다.
 */
#include "archive/yaml/grammar.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace bedrock::archive::yaml {
// State 다형성 앵커(key function) — vtable을 이 TU에 고정, weak-vtable 방지
Grammar::State::~State() = default;

// 컨텍스트 싱글턴 정의 (상태 없음 — vptr만 있으므로 상수 초기화 가능)
constinit Grammar::BlockInState Grammar::block_in_state;
constinit Grammar::BlockOutState Grammar::block_out_state;
constinit Grammar::BlockKeyState Grammar::block_key_state;
constinit Grammar::FlowInState Grammar::flow_in_state;
constinit Grammar::FlowOutState Grammar::flow_out_state;
constinit Grammar::FlowKeyState Grammar::flow_key_state;

// chomping 상태 다형성 앵커(key function)
Grammar::ChompingState::~ChompingState() = default;
Grammar::ClipState::~ClipState() = default;

constinit Grammar::StripState Grammar::strip_state;
constinit Grammar::ClipState Grammar::clip_state;
constinit Grammar::KeepState Grammar::keep_state;

void Grammar::Advance(ParseState& cur, std::size_t repetition_count) noexcept {
  assert(repetition_count <= cur.cps.size());

  const auto new_before_size = cur.before.size() + repetition_count;

  cur.before = {cur.before.data(), new_before_size};
  cur.cps = cur.cps.subspan(repetition_count);
  if (cur.diag != nullptr) {
    cur.diag->furthest = std::max(cur.diag->furthest, cur.before.size());
  }
}
void Grammar::AddNode(ParseState& cur, SyntaxNode node) {
  assert(cur.arena != nullptr);
  if (cur.frame == kNoArenaEntry) {
    cur.arena->node_links.push_back(
        NodeLink{.node = std::move(node), .previous = cur.roots});
    cur.roots = cur.arena->node_links.size() - 1;
    return;
  }

  const Frame frame = cur.arena->frames[cur.frame];
  cur.arena->node_links.push_back(
      NodeLink{.node = std::move(node), .previous = frame.children});
  const std::size_t children = cur.arena->node_links.size() - 1;
  cur.arena->frames.push_back(Frame{.kind = frame.kind,
                                    .begin = frame.begin,
                                    .children = children,
                                    .parent = frame.parent});
  cur.frame = cur.arena->frames.size() - 1;
}

void Grammar::AddEmpty(ParseState& cur) {
  AddNode(cur, {.kind = SyntaxKind::kScalar,
                .begin = cur.before.size(),
                .end = cur.before.size()});
}

void Grammar::OpenNode(ParseState& cur, SyntaxKind kind, std::size_t begin) {
  assert(cur.arena != nullptr);
  cur.arena->frames.push_back(Frame{
      .kind = kind,
      .begin = begin,
      .children = kNoArenaEntry,
      .parent = cur.frame,
  });
  cur.frame = cur.arena->frames.size() - 1;
}

void Grammar::CloseNode(ParseState& cur, std::size_t end) {
  assert(cur.arena != nullptr && cur.frame != kNoArenaEntry);
  const Frame frame = cur.arena->frames[cur.frame];
  SyntaxNode node{.kind = frame.kind,
                  .begin = frame.begin,
                  .end = end,
                  .children = Materialize(*cur.arena, frame.children)};
  cur.frame = frame.parent;
  AddNode(cur, std::move(node));
}

std::vector<SyntaxNode> Grammar::Materialize(const Arena& arena,
                                             std::size_t nodes) {
  std::vector<SyntaxNode> result;
  for (std::size_t node = nodes; node != kNoArenaEntry;
       node = arena.node_links[node].previous) {
    result.push_back(arena.node_links[node].node);
  }
  std::ranges::reverse(result);
  return result;
}
ChompKind Grammar::ChompKindOf(const ChompingState* trial) {
  if (trial == &strip_state) {
    return ChompKind::kStrip;
  }
  if (trial == &keep_state) {
    return ChompKind::kKeep;
  }
  return ChompKind::kClip;
}
LineCol OffsetToLineCol(std::span<const std::uint32_t> buf,
                        std::size_t offset) {
  LineCol line_count{1, 1};
  const std::size_t end = offset < buf.size() ? offset : buf.size();
  for (std::size_t index = 0; index < end; index++) {
    const std::uint32_t character = buf[index];
    if (character == '\n') {
      line_count.line++;
      line_count.col = 1;
    } else if (character == '\r') {
      if (index + 1 < end && buf[index + 1] == '\n') {
        continue;  // CRLF의 중간 — LF가 줄을 넘긴다
      }
      line_count.line++;
      line_count.col = 1;
    } else {
      line_count.col++;
    }
  }
  return line_count;
}
std::uint32_t Grammar::At(std::span<const std::uint32_t> cps,
                          std::size_t repetition_index) noexcept {
  return repetition_index < cps.size() ? cps[repetition_index] : 0;
}
std::span<const std::uint32_t> Grammar::Subspan(
    std::span<const std::uint32_t> cps, std::size_t offset) noexcept {
  // offset이 1이고 size가 1인 경우를 상상해 보면 끝에서 끝을 슬라이싱 해서 {}이
  // 되지만 일단 유효한 subspan임
  return offset <= cps.size() ? cps.subspan(offset)
                              : std::span<const std::uint32_t>{};
}
std::span<const std::uint32_t> Grammar::Subspan(
    std::span<const std::uint32_t> cps, std::size_t offset,
    std::size_t count) noexcept {
  if (offset > cps.size()) {
    return {};
  }
  count = std::min(count, cps.size() - offset);
  // offset <= cps.size() && count <= cps.size() - offset
  // offset == cps.size()라면 count는 0이 된다.
  return cps.subspan(offset, count);
}
bool Grammar::AtEnd(std::span<const std::uint32_t> cps,
                    std::size_t repetition_index) noexcept {
  return repetition_index >= cps.size();
}
bool Grammar::AtLineStart(const ParseState& cur) noexcept {
  // BOM은 줄 내용이 아님([202][211]에서 문서 경계에만 등장) — 건너뛰고 판정
  std::span<const std::uint32_t> byte = cur.before;
  while (!byte.empty() && byte.back() == kCByteOrderMark) {
    // empty가 아니기 때문에 size() - 1이 유효하다.
    // 앞에서부터 size() - 1, 즉 맨 뒤 하나(c-byte-order-mark)를 자른다.
    byte = byte.first(byte.size() - 1);
  }
  if (byte.empty()) {
    // cur 이전이 빈 상황(스트림의 시작 내지는 파일의 시작인 상황)
    return true;
  }
  const std::uint32_t prev = byte.back();
  if (prev == kBLineFeed) {
    // LF(/n) 이후.(자명히 linestart임)
    return true;
  }
  if (prev == kBCarriageReturn) {
    // CR(\r) 바로 뒤가 LF(\n)면 CRLF의 중간이므로 아직 줄이 끝나지 않음
    // CR 바로 뒤가 LF가 아니라면 codepoint != kBLineFeed가 true가 되므로 적절한
    // linestart라고 볼 수 있음. 엣지 케이스는 \r\uFEFF\n인데 바이트 오더가
    // 중간에 오는 경우는 없음.(yaml1.2.2 example 5.2 invalid byte order mark
    // 참조)
    return At(cur.cps, 0) != kBLineFeed;
  }
  return false;
}
std::size_t Grammar::LeadingSpaces(std::span<const std::uint32_t> cps) {
  std::size_t index = 0;
  for (; index < cps.size(); index++) {
    if (cps[index] != kSSpace) {
      break;
    }
  }
  return index;
}
std::ptrdiff_t Grammar::DetectScalarIndentation(
    std::span<const std::uint32_t> cps, const std::ptrdiff_t n) {
  // 8.1.1.1: 첫 비어있지 않은 행의 들여쓰기 w로 m = w - n (m >= 1).
  // 선행 빈 행이 그보다 더 들여쓰여 있으면 에러(0 반환). 내용 행이 없으면
  // 가장 긴 빈 행의 공백 수 기준.
  std::size_t repetition_index = 0;
  std::size_t max_empty = 0;
  while (true) {
    const std::size_t width = LeadingSpaces(cps.subspan(repetition_index));
    const std::size_t pair = repetition_index + width;
    if (AtEnd(cps, pair)) {
      const std::ptrdiff_t indent_width =
          static_cast<std::ptrdiff_t>(max_empty) - n;
      return indent_width < 1 ? 1 : indent_width;
    }
    if (const std::size_t byte = IsbBreak(cps.subspan(pair))) {
      // 공백만 있는 빈 행
      max_empty = std::max(width, max_empty);
      repetition_index = pair + byte;
      continue;
    }
    // 첫 내용 행
    if (max_empty > width) {
      return 0;  // 선행 빈 행이 내용보다 더 들여쓰여짐 -> 에러
    }
    const std::ptrdiff_t indent_width = static_cast<std::ptrdiff_t>(width) - n;
    return indent_width < 1 ? 1 : indent_width;
  }
}
std::uint32_t Grammar::IscPrintable(const std::uint32_t code_point) {
  switch (code_point) {
    case '\t':
    case '\n':
    case '\r':
    case 0x85:
      return code_point;
    default: {
      if (0x20 <= code_point && code_point <= 0x7E) {
        return code_point;
      }
      if (0xA0 <= code_point && code_point <= 0xD7FF) {
        return code_point;
      }
      if (0xE000 <= code_point && code_point <= 0xFFFD) {
        return code_point;
      }
      if (0x010000 <= code_point && code_point <= 0x10FFFF) {
        return code_point;
      }
      break;
    }
  }
  return 0;
}
std::uint32_t Grammar::IsnbJson(const std::uint32_t code_point) {
  switch (code_point) {
    case '\t':
      return code_point;
    default: {
      if (0x20 <= code_point && code_point <= 0x10FFFF) {
        return code_point;
      }
      break;
    }
  }
  return 0;
}
std::uint32_t Grammar::IscReserved(const std::uint32_t code_point) {
  switch (code_point) {
    case '@':
    case '`':
      return code_point;
    default:
      break;
  }
  return 0;
}
std::uint32_t Grammar::IscIndicator(const std::uint32_t code_point) {
  switch (code_point) {
    case kCSequenceEntry:
    case kCMappingKey:
    case kCMappingValue:
    case kCCollectEntry:
    case kCSequenceStart:
    case kCSequenceEnd:
    case kCMappingStart:
    case kCMappingEnd:
    case kCComment:
    case kCAnchor:
    case kCAlias:
    case kCTag:
    case kCLiteral:
    case kCFolded:
    case kCSingleQuote:
    case kCDoubleQuote:
    case kCDirective:
      return code_point;
    default: {
      if (IscReserved(code_point) != 0U) {
        return code_point;
      }
      break;
    }
  }
  return 0;
}
std::uint32_t Grammar::IscFlowIndicator(const std::uint32_t code_point) {
  switch (code_point) {
    case kCCollectEntry:
    case kCSequenceStart:
    case kCSequenceEnd:
    case kCMappingStart:
    case kCMappingEnd:
      return code_point;
    default:
      break;
  }
  return 0;
}
std::uint32_t Grammar::IsbChar(const std::uint32_t code_point) {
  switch (code_point) {
    case kBLineFeed:
    case kBCarriageReturn:
      return code_point;
    default:
      break;
  }
  return 0;
}
std::uint32_t Grammar::IsnbChar(const std::uint32_t code_point) {
  switch (code_point) {
    case kCByteOrderMark:
      return 0;
    default: {
      // b-char인 경우임. 반환값이 0이라면 b-char가 아닌 경우이기 때문.
      if (IsbChar(code_point) != 0U) {
        return 0;
      }
      // c-printable이 아닌 경우.
      if (IscPrintable(code_point) == 0U) {
        return 0;
      }
      return code_point;
    }
  }
}
std::size_t Grammar::IsbBreak(std::span<const std::uint32_t> cps) {
  switch (At(cps, 0)) {
    case kBCarriageReturn:
      // (b-carriage-return | b-line-feed)인지 그냥 b-carriage-return인지 판별
      return (At(cps, 1) == kBLineFeed) ? 2 : 1;
    case kBLineFeed:
      return 1;
    default:
      break;
  }
  return 0;
}
std::size_t Grammar::IsbAsLineFeed(std::span<const std::uint32_t> cps) {
  return IsbBreak(cps);
}
std::size_t Grammar::IsbNonContent(std::span<const std::uint32_t> cps) {
  return IsbBreak(cps);
}
std::uint32_t Grammar::IssWhite(const std::uint32_t code_point) {
  switch (code_point) {
    case kSSpace:
    case kSTab:
      return code_point;
    default:
      break;
  }
  return 0;
}
std::uint32_t Grammar::IsnsChar(const std::uint32_t code_point) {
  // nb-char가 아니라면
  if (IsnbChar(code_point) == 0U) {
    return 0;
  }
  // s-white라면
  if (IssWhite(code_point) != 0U) {
    return 0;
  }
  return code_point;
}
std::uint32_t Grammar::IsnsDecDigit(const std::uint32_t code_point) {
  if ('0' <= code_point && code_point <= '9') {
    return code_point;
  }
  return 0;
}
std::uint32_t Grammar::IsnsHexDigit(const std::uint32_t code_point) {
  // 10진수 숫자만 있다면 무조건 맞음
  if (IsnsDecDigit(code_point) != 0U) {
    return code_point;
  }
  // 16진수 알파벳 범위들
  if ('A' <= code_point && code_point <= 'F') {
    return code_point;
  }
  if ('a' <= code_point && code_point <= 'f') {
    return code_point;
  }
  return 0;
}
std::uint32_t Grammar::IsnsAsciiLetter(const std::uint32_t code_point) {
  // [A-Za-z]
  if ('A' <= code_point && code_point <= 'Z') {
    return code_point;
  }
  if ('a' <= code_point && code_point <= 'z') {
    return code_point;
  }
  return 0;
}
std::uint32_t Grammar::IsnsWordChar(const std::uint32_t code_point) {
  switch (code_point) {
    case '-':
      return code_point;
    default: {
      if (IsnsDecDigit(code_point) != 0U) {
        return code_point;
      }
      if (IsnsAsciiLetter(code_point) != 0U) {
        return code_point;
      }
      return 0;
    }
  }
}
std::size_t Grammar::IsnsUriChar(std::span<const std::uint32_t> cps) {
  if (At(cps, 0) == '%') {
    if ((IsnsHexDigit(At(cps, 1)) != 0U) && (IsnsHexDigit(At(cps, 2)) != 0U)) {
      return 3;
    }
    return 0;
  }
  if (IsnsWordChar(At(cps, 0)) != 0U) {
    return 1;
  }
  switch (At(cps, 0)) {
    // 상수로 이미 정의는 있지만 왜인지 공식문서에 이렇게 되어 있기 때문에
    // 이렇게 하였음.
    case '#':
    case ';':
    case '/':
    case '?':
    case ':':
    case '@':
    case '&':
    case '=':
    case '+':
    case '$':
    case ',':
    case '_':
    case '.':
    case '!':
    case '~':
    case '*':
    case '\'':
    case '(':
    case ')':
    case '[':
    case ']':
      return 1;
    default:
      break;
  }
  return 0;
}
std::size_t Grammar::IsnsTagChar(std::span<const std::uint32_t> cps) {
  const std::size_t ns_uri_char = IsnsUriChar(cps);
  // 일단 ns-uri-char여야만 함
  if (ns_uri_char == 0U) {
    return 0;
  }
  switch (At(cps, 0)) {
    case kCTag:
      return 0;
    default: {
      // 만약 c-flow-indicator라면
      if (IscFlowIndicator(At(cps, 0)) != 0U) {
        return 0;
      }
      break;
    }
  }
  return ns_uri_char;
}
std::uint32_t Grammar::IsnsEscHorizontalTab(const std::uint32_t code_point) {
  switch (code_point) {
    case 't':
    case '\t':
      return code_point;
    default:
      break;
  }
  return 0;
}
std::size_t Grammar::IsnsEsc8Bit(std::span<const std::uint32_t> cps) {
  if (At(cps, 0) != 'x') {
    return 0;
  }
  if ((IsnsHexDigit(At(cps, 1)) != 0U) && (IsnsHexDigit(At(cps, 2)) != 0U)) {
    return 3;
  }
  return 0;
}
std::size_t Grammar::IsnsEsc16Bit(std::span<const std::uint32_t> cps) {
  if (At(cps, 0) != 'u') {
    return 0;
  }
  if ((IsnsHexDigit(At(cps, 1)) != 0U) && (IsnsHexDigit(At(cps, 2)) != 0U) &&
      (IsnsHexDigit(At(cps, 3)) != 0U) && (IsnsHexDigit(At(cps, 4)) != 0U)) {
    return 5;
  }
  return 0;
}
std::size_t Grammar::IsnsEsc32Bit(std::span<const std::uint32_t> cps) {
  if (At(cps, 0) != 'U') {
    return 0;
  }
  if ((IsnsHexDigit(At(cps, 1)) != 0U) && (IsnsHexDigit(At(cps, 2)) != 0U) &&
      (IsnsHexDigit(At(cps, 3)) != 0U) && (IsnsHexDigit(At(cps, 4)) != 0U) &&
      (IsnsHexDigit(At(cps, 5)) != 0U) && (IsnsHexDigit(At(cps, 6)) != 0U) &&
      (IsnsHexDigit(At(cps, 7)) != 0U) && (IsnsHexDigit(At(cps, 8)) != 0U)) {
    return 9;
  }
  return 0;
}
std::size_t Grammar::IscNsEscChar(std::span<const std::uint32_t> cps) {
  if (At(cps, 0) != kCEscape) {
    return 0;
  }
  const std::uint32_t first_character = At(cps, 1);
  switch (first_character) {
    case kNsEscNull:
    case kNsEscBell:
    case kNsEscBackspace:
    case kNsEscLineFeed:
    case kNsEscVerticalTab:
    case kNsEscFormFeed:
    case kNsEscCarriageReturn:
    case kNsEscEscape:
    case kNsEscSpace:
    case kNsEscDoubleQuote:
    case kNsEscSlash:
    case kNsEscBackslash:
    case kNsEscNextLine:
    case kNsEscNonBreakingSpace:
    case kNsEscLineSeparator:
    case kNsEscParagraphSeparator:
      return 2;
    default: {
      if (IsnsEscHorizontalTab(first_character) != 0U) {
        return 2;
      }
      const std::span<const std::uint32_t> rest = Subspan(cps, 1);
      if (const std::size_t event = IsnsEsc8Bit(rest)) {
        return event + 1;
      }
      if (const std::size_t event = IsnsEsc16Bit(rest)) {
        return event + 1;
      }
      if (const std::size_t event = IsnsEsc32Bit(rest)) {
        return event + 1;
      }
      break;
    }
  }
  return 0;
}
bool Grammar::IssIndent(ParseState& cur, const std::ptrdiff_t n) {
  // s-indent(0) = 빈 매치 (음수 n도 관례상 빈 매치로 취급)
  if (n <= 0) {
    return true;
  }
  const auto unescaped_count = static_cast<std::size_t>(n);
  // unescaped_count는 1부터임, 따라서 n=1이라면 s-space가 1번 나오는지
  // 검사한다.
  for (std::size_t repetition_index = 0; repetition_index < unescaped_count;
       repetition_index++) {
    if (At(cur.cps, repetition_index) != kSSpace) {
      return false;
    }
  }
  Advance(cur, unescaped_count);
  return true;
}
bool Grammar::IssIndentLessThan(ParseState& cur, const std::ptrdiff_t n) {
  if (n <= 0) {
    return false;
  }
  const std::ptrdiff_t m = static_cast<std::ptrdiff_t>(LeadingSpaces(cur.cps));
  if (m >= n) {
    return false;
  }
  // [64]는 s-space를 m번 직접 소비한다. [63] s-indent production 호출이 아니다.
  Advance(cur, static_cast<std::size_t>(m));
  return true;
}
bool Grammar::IssIndentLessOrEqual(ParseState& cur, const std::ptrdiff_t n) {
  if (n < 0) {
    return false;
  }
  const std::ptrdiff_t m = static_cast<std::ptrdiff_t>(LeadingSpaces(cur.cps));
  if (m > n) {
    return false;
  }
  // [65]는 s-space를 m번 직접 소비한다. [63] s-indent production 호출이 아니다.
  Advance(cur, static_cast<std::size_t>(m));
  return true;
}
bool Grammar::IssSeparateInLine(ParseState& cur) {
  std::size_t width = 0;
  while (IssWhite(At(cur.cps, width)) != 0U) {
    width++;
  }
  if (width != 0U) {
    Advance(cur, width);
    return true;
  }
  // s-white가 없으면 <start-of-line>에서만 빈 매치
  return AtLineStart(cur);
}
bool Grammar::IssBlockLinePrefix(ParseState& cur, const std::ptrdiff_t n) {
  return IssIndent(cur, n);
}
bool Grammar::IssFlowLinePrefix(ParseState& cur, const std::ptrdiff_t n) {
  if (n < 0) {
    return false;
  }
  if (!IssIndent(cur, n)) {
    // s-indent(n) 실패
    return false;
  }
  // n칸 이후 나머지에서 추가 공백/탭 흡수(s-separate-in-line, 빈 매치 허용)
  // 빈 매치 허용이기 때문에 bool 리턴값은 의도적으로 무시한다.
  // 0일 때는 전진하지 않는 동작이 맞아 떨어지고, 1이상이라면 내부에서 cur을
  // Advance 하기 때문에 읽은 만큼 Advance 하는 현재 흐름에 맞는다.
  IssSeparateInLine(cur);
  return true;
}
bool Grammar::BlockInState::IssLinePrefix(ParseState& cur,
                                          const std::ptrdiff_t n) const {
  return IssBlockLinePrefix(cur, n);
}
bool Grammar::BlockOutState::IssLinePrefix(ParseState& cur,
                                           const std::ptrdiff_t n) const {
  return IssBlockLinePrefix(cur, n);
}
bool Grammar::FlowInState::IssLinePrefix(ParseState& cur,
                                         const std::ptrdiff_t n) const {
  return IssFlowLinePrefix(cur, n);
}
bool Grammar::FlowOutState::IssLinePrefix(ParseState& cur,
                                          const std::ptrdiff_t n) const {
  return IssFlowLinePrefix(cur, n);
}
bool Grammar::State::IslEmpty(ParseState& cur, const std::ptrdiff_t n) const {
  if (n < 0) {
    return false;
  }
  // s-line-prefix b-as-line-feed 시퀀스 테스트.
  {
    ParseState trial = cur;
    if (IssLinePrefix(trial, n)) {
      const auto byte = IsbAsLineFeed(trial.cps);
      if (byte != 0U) {
        Advance(trial, byte);
        cur = trial;
        return true;
      }
    }
  }
  // s-indent-less-than b-as-line-feed 시퀀스 테스트.
  {
    ParseState trial = cur;
    if (IssIndentLessThan(trial, n)) {
      const auto byte = IsbAsLineFeed(trial.cps);
      if (byte != 0U) {
        Advance(trial, byte);
        cur = trial;
        return true;
      }
    }
  }
  return false;
}
bool Grammar::State::IsbLTrimmed(ParseState& cur,
                                 const std::ptrdiff_t n) const {
  const std::size_t byte = IsbNonContent(cur.cps);
  if (byte == 0U) {
    return false;
  }
  Advance(cur, byte);
  std::size_t count = 0;
  while (true) {
    ParseState trial = cur;
    if (!IslEmpty(trial, n)) {
      break;
    }
    cur = trial;
    count++;  // l-empty는 항상 1자 이상 소비
  }
  return count != 0;  // l-empty(n,c)+ — 1회 이상 필요
}
std::size_t Grammar::IsbAsSpace(std::span<const std::uint32_t> cps) {
  return IsbBreak(cps);
}
bool Grammar::State::IsbLFolded(ParseState& cur, const std::ptrdiff_t n) const {
  {
    ParseState trial = cur;
    if (IsbLTrimmed(trial, n)) {
      cur = trial;
      return true;
    }
  }
  const std::size_t byte = IsbAsSpace(cur.cps);
  if (byte != 0U) {
    Advance(cur, byte);
    return true;
  }
  return false;
}
bool Grammar::IssFlowFolded(ParseState& cur, const std::ptrdiff_t n) {
  IssSeparateInLine(cur);
  if (!flow_in_state.IsbLFolded(cur, n)) {
    return false;
  }
  return IssFlowLinePrefix(cur, n);
}
bool Grammar::IscNbCommentText(ParseState& cur) {
  if (At(cur.cps, 0) != kCComment) {
    return false;
  }
  std::size_t repetition_index = 1;
  while (IsnbChar(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  Advance(cur, repetition_index);
  return true;
}
bool Grammar::IsbComment(ParseState& cur) {
  if (AtEnd(cur.cps, 0)) {
    return true;  // <end-of-input>
  }
  const std::size_t byte = IsbNonContent(cur.cps);
  if (byte != 0U) {
    Advance(cur, byte);
    return true;
  }
  return false;
}
bool Grammar::IssBComment(ParseState& cur) {
  // (s-separate-in-line c-nb-comment-text?)? b-comment
  // 그룹을 소비한 뒤 b-comment가 실패하면 그룹 없이 재시도
  {
    ParseState trial = cur;
    if (IssSeparateInLine(trial)) {
      IscNbCommentText(trial);
      if (IsbComment(trial)) {
        cur = trial;
        return true;
      }
    }
  }
  return IsbComment(cur);
}
bool Grammar::IslComment(ParseState& cur) {
  if (!IssSeparateInLine(cur)) {
    return false;
  }
  IscNbCommentText(cur);
  return IsbComment(cur);
}
bool Grammar::IssLComments(ParseState& cur) {
  if (!IssBComment(cur)) {
    if (!AtLineStart(cur)) {
      return false;
    }
    // <start-of-line> 빈 매치
  }
  while (true) {
    ParseState trial = cur;
    if (!IslComment(trial)) {
      break;
    }
    if (trial.before.size() == cur.before.size()) {
      break;  // l-comment는 sol+EOF에서 빈 매치가 가능하므로 진행 없으면 종료
    }
    cur = trial;
  }
  return true;
}
bool Grammar::State::IssSeparate(ParseState& cur,
                                 const std::ptrdiff_t n) const {
  return IssSeparateLines(cur, n);
}
bool Grammar::BlockKeyState::IssSeparate(ParseState& cur,
                                         const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return IssSeparateInLine(cur);
}
bool Grammar::FlowKeyState::IssSeparate(ParseState& cur,
                                        const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return IssSeparateInLine(cur);
}
bool Grammar::IssSeparateLines(ParseState& cur, const std::ptrdiff_t n) {
  {
    ParseState trial = cur;
    if (IssLComments(trial) && IssFlowLinePrefix(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IssSeparateInLine(cur);
}
bool Grammar::IslDirective(ParseState& cur) {
  if (At(cur.cps, 0) != kCDirective) {
    return false;
  }
  Advance(cur, 1);
  // 각 대안은 뒤따르는 s-l-comments까지 성공해야 함 (형식이 어긋난
  // YAML/TAG 지시자는 ns-reserved-directive로 백트래킹됨 — 순수 문법 인식.
  // 알려진 이름의 형식 검증은 의미론 단계의 몫)
  {
    ParseState trial = cur;
    if (IsnsYamlDirective(trial) && IssLComments(trial)) {
      cur = trial;
      return true;
    }
  }
  {
    ParseState trial = cur;
    if (IsnsTagDirective(trial) && IssLComments(trial)) {
      cur = trial;
      return true;
    }
  }
  return IsnsReservedDirective(cur) && IssLComments(cur);
}
bool Grammar::IsnsReservedDirective(ParseState& cur) {
  if (!IsnsDirectiveName(cur)) {
    return false;
  }
  while (true) {
    ParseState trial = cur;
    if (!IssSeparateInLine(trial)) {
      break;
    }
    if (trial.before.size() == cur.before.size()) {
      break;  // 진행 없는 분리
    }
    if (!IsnsDirectiveParameter(trial)) {
      break;
    }
    cur = trial;  // 분리는 파라미터가 따라올 때만 commit
  }
  return true;
}
bool Grammar::IsnsDirectiveName(ParseState& cur) {
  std::size_t repetition_index = 0;
  while (IsnsChar(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  if (repetition_index == 0) {
    return false;
  }
  Advance(cur, repetition_index);
  return true;
}
bool Grammar::IsnsDirectiveParameter(ParseState& cur) {
  std::size_t repetition_index = 0;
  while (IsnsChar(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  if (repetition_index == 0) {
    return false;
  }
  Advance(cur, repetition_index);
  return true;
}
bool Grammar::IsnsYamlDirective(ParseState& cur) {
  if (At(cur.cps, 0) != 'Y' || At(cur.cps, 1) != 'A' || At(cur.cps, 2) != 'M' ||
      At(cur.cps, 3) != 'L') {
    return false;
  }
  Advance(cur, 4);
  if (!IssSeparateInLine(cur)) {
    return false;
  }
  return IsnsYamlVersion(cur);
}
bool Grammar::IsnsYamlVersion(ParseState& cur) {
  std::size_t repetition_index = 0;
  while (IsnsDecDigit(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  if (repetition_index == 0) {
    return false;
  }
  if (At(cur.cps, repetition_index) != '.') {
    return false;
  }
  std::size_t second_repetition_index = repetition_index + 1;
  const std::size_t start2 = second_repetition_index;
  while (IsnsDecDigit(At(cur.cps, second_repetition_index)) != 0U) {
    second_repetition_index++;
  }
  if (second_repetition_index == start2) {
    return false;
  }
  Advance(cur, second_repetition_index);
  return true;
}
bool Grammar::IsnsTagDirective(ParseState& cur) {
  if (At(cur.cps, 0) != 'T' || At(cur.cps, 1) != 'A' || At(cur.cps, 2) != 'G') {
    return false;
  }
  Advance(cur, 3);
  if (!IssSeparateInLine(cur)) {
    return false;
  }
  if (!IscTagHandle(cur)) {
    return false;
  }
  if (!IssSeparateInLine(cur)) {
    return false;
  }
  return IsnsTagPrefix(cur);
}

bool Grammar::IscTagHandle(ParseState& cur) {
  // named("!x!") -> secondary("!!") -> primary("!") 순서가 중요
  if (IscNamedTagHandle(cur)) {
    return true;
  }
  if (IscSecondaryTagHandle(cur)) {
    return true;
  }
  return IscPrimaryTagHandle(cur);
}

bool Grammar::IscPrimaryTagHandle(ParseState& cur) {
  if (At(cur.cps, 0) != kCTag) {
    return false;
  }
  Advance(cur, 1);
  return true;
}

bool Grammar::IscSecondaryTagHandle(ParseState& cur) {
  if (At(cur.cps, 0) != kCTag || At(cur.cps, 1) != kCTag) {
    return false;
  }
  Advance(cur, 2);
  return true;
}

bool Grammar::IscNamedTagHandle(ParseState& cur) {
  if (At(cur.cps, 0) != kCTag) {
    return false;
  }
  std::size_t repetition_index = 1;
  while (IsnsWordChar(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  if (repetition_index == 1) {
    return false;  // ns-word-char+ — 1자 이상
  }
  if (At(cur.cps, repetition_index) != kCTag) {
    return false;
  }
  Advance(cur, repetition_index + 1);
  return true;
}

bool Grammar::IsnsTagPrefix(ParseState& cur) {
  if (IscNsLocalTagPrefix(cur)) {
    return true;
  }
  return IsnsGlobalTagPrefix(cur);
}

bool Grammar::IscNsLocalTagPrefix(ParseState& cur) {
  if (At(cur.cps, 0) != kCTag) {
    return false;
  }
  Advance(cur, 1);
  while (const std::size_t code_unit = IsnsUriChar(cur.cps)) {
    Advance(cur, code_unit);
  }
  return true;
}

bool Grammar::IsnsGlobalTagPrefix(ParseState& cur) {
  const std::size_t trial = IsnsTagChar(cur.cps);
  if (trial == 0U) {
    return false;
  }
  Advance(cur, trial);
  while (const std::size_t code_unit = IsnsUriChar(cur.cps)) {
    Advance(cur, code_unit);
  }
  return true;
}

bool Grammar::State::IscNsProperties(ParseState& cur,
                                     const std::ptrdiff_t n) const {
  // (tag (sep anchor)?) | (anchor (sep tag)?) — 뒤쪽 선택 그룹은
  // 분리까지 성공하고 속성이 실패하면 그룹 없이 성립
  if (IscNsTagProperty(cur)) {
    ParseState trial = cur;
    if (IssSeparate(trial, n) && IscNsAnchorProperty(trial)) {
      cur = trial;
      return true;
    }
    return true;  // 태그만
  }
  if (IscNsAnchorProperty(cur)) {
    ParseState trial = cur;
    if (IssSeparate(trial, n) && IscNsTagProperty(trial)) {
      cur = trial;
      return true;
    }
    return true;  // 앵커만
  }
  return false;
}

bool Grammar::IscNsTagProperty(ParseState& cur) {
  const std::size_t trailing_breaks = cur.before.size();
  {
    ParseState trial = cur;
    if (IscVerbatimTag(trial)) {
      AddNode(trial, {.kind = SyntaxKind::kTag,
                      .begin = trailing_breaks,
                      .end = trial.before.size()});
      cur = trial;
      return true;
    }
  }
  {
    ParseState trial = cur;
    if (IscNsShorthandTag(trial)) {
      AddNode(trial, {.kind = SyntaxKind::kTag,
                      .begin = trailing_breaks,
                      .end = trial.before.size()});
      cur = trial;
      return true;
    }
  }
  if (!IscNonSpecificTag(cur)) {
    return false;
  }
  AddNode(cur, {.kind = SyntaxKind::kTag,
                .begin = trailing_breaks,
                .end = cur.before.size()});
  return true;
}

bool Grammar::IscVerbatimTag(ParseState& cur) {
  if (At(cur.cps, 0) != kCTag || At(cur.cps, 1) != '<') {
    return false;
  }
  Advance(cur, 2);
  std::size_t count = 0;
  while (const std::size_t code_unit = IsnsUriChar(cur.cps)) {
    Advance(cur, code_unit);
    count++;
  }
  if (count == 0U) {
    return false;
  }
  if (At(cur.cps, 0) != '>') {
    return false;
  }
  Advance(cur, 1);
  return true;
}

bool Grammar::IscNsShorthandTag(ParseState& cur) {
  if (!IscTagHandle(cur)) {
    return false;
  }
  std::size_t count = 0;
  while (const std::size_t trial = IsnsTagChar(cur.cps)) {
    Advance(cur, trial);
    count++;
  }
  return count != 0;
}

bool Grammar::IscNonSpecificTag(ParseState& cur) {
  if (At(cur.cps, 0) != kCTag) {
    return false;
  }
  Advance(cur, 1);
  return true;
}

bool Grammar::IscNsAnchorProperty(ParseState& cur) {
  if (At(cur.cps, 0) != kCAnchor) {
    return false;
  }
  Advance(cur, 1);
  const std::size_t non_break_count = cur.before.size();
  if (!IsnsAnchorName(cur)) {
    return false;
  }
  AddNode(cur, {.kind = SyntaxKind::kAnchor,
                .begin = non_break_count,
                .end = cur.before.size()});
  return true;
}

std::uint32_t Grammar::IsnsAnchorChar(const std::uint32_t code_point) {
  if (IsnsChar(code_point) == 0U) {
    return 0;
  }
  if (IscFlowIndicator(code_point) != 0U) {
    return 0;
  }
  return code_point;
}

bool Grammar::IsnsAnchorName(ParseState& cur) {
  std::size_t repetition_index = 0;
  while (IsnsAnchorChar(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  if (repetition_index == 0) {
    return false;
  }
  Advance(cur, repetition_index);
  return true;
}

bool Grammar::IscNsAliasNode(ParseState& cur) {
  if (At(cur.cps, 0) != kCAlias) {
    return false;
  }
  Advance(cur, 1);
  const std::size_t non_break_count = cur.before.size();
  if (!IsnsAnchorName(cur)) {
    return false;
  }
  AddNode(cur, {.kind = SyntaxKind::kAlias,
                .begin = non_break_count,
                .end = cur.before.size()});
  return true;
}

bool Grammar::IseScalar(ParseState& cur) {
  AddEmpty(cur);
  return true;
}

bool Grammar::IseNode(ParseState& cur) { return IseScalar(cur); }

std::size_t Grammar::IsnbDoubleChar(std::span<const std::uint32_t> cps) {
  if (const std::size_t event = IscNsEscChar(cps)) {
    return event;
  }
  const std::uint32_t code_point = At(cps, 0);
  if (IsnbJson(code_point) == 0U) {
    return 0;
  }
  if (code_point == kCEscape || code_point == kCDoubleQuote) {
    return 0;
  }
  return 1;
}

std::size_t Grammar::IsnsDoubleChar(std::span<const std::uint32_t> cps) {
  const std::size_t len = IsnbDoubleChar(cps);
  if (len == 0U) {
    return 0;
  }
  // 이스케이프(길이 2+)는 공백을 나타내더라도 ns로 취급
  if (len == 1 && (IssWhite(At(cps, 0)) != 0U)) {
    return 0;
  }
  return len;
}

bool Grammar::State::IscDoubleQuoted(ParseState& cur,
                                     const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != kCDoubleQuote) {
    return false;
  }
  Advance(cur, 1);
  const std::size_t byte = cur.before.size();
  if (!IsnbDoubleText(cur, n)) {
    return false;
  }
  const std::size_t event = cur.before.size();
  if (At(cur.cps, 0) != kCDoubleQuote) {
    return false;
  }
  Advance(cur, 1);
  AddNode(cur, {.kind = SyntaxKind::kScalar,
                .style = ScalarStyle::kDoubleQuoted,
                .begin = byte,
                .end = event});
  return true;
}

bool Grammar::FlowInState::IsnbDoubleText(ParseState& cur,
                                          const std::ptrdiff_t n) const {
  return IsnbDoubleMultiLine(cur, n);
}

bool Grammar::FlowOutState::IsnbDoubleText(ParseState& cur,
                                           const std::ptrdiff_t n) const {
  return IsnbDoubleMultiLine(cur, n);
}

bool Grammar::BlockKeyState::IsnbDoubleText(ParseState& cur,
                                            const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return IsnbDoubleOneLine(cur);
}

bool Grammar::FlowKeyState::IsnbDoubleText(ParseState& cur,
                                           const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return IsnbDoubleOneLine(cur);
}

bool Grammar::IsnbDoubleOneLine(ParseState& cur) {
  while (const std::size_t digit = IsnbDoubleChar(cur.cps)) {
    Advance(cur, digit);
  }
  return true;
}

bool Grammar::IssDoubleEscaped(ParseState& cur, const std::ptrdiff_t n) {
  std::size_t width = 0;
  while (IssWhite(At(cur.cps, width)) != 0U) {
    width++;
  }
  if (At(cur.cps, width) != kCEscape) {
    return false;
  }
  const std::size_t byte = IsbNonContent(cur.cps.subspan(width + 1));
  if (byte == 0U) {
    return false;
  }
  Advance(cur, width + 1 + byte);
  while (true) {
    ParseState trial = cur;
    if (!flow_in_state.IslEmpty(trial, n)) {
      break;
    }
    cur = trial;
  }
  return IssFlowLinePrefix(cur, n);
}

bool Grammar::IssDoubleBreak(ParseState& cur, const std::ptrdiff_t n) {
  {
    ParseState trial = cur;
    if (IssDoubleEscaped(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IssFlowFolded(cur, n);
}

bool Grammar::IsnbNsDoubleInLine(ParseState& cur) {
  while (true) {
    std::size_t repetition_index = 0;
    while (IssWhite(At(cur.cps, repetition_index)) != 0U) {
      repetition_index++;
    }
    const std::size_t digit = IsnsDoubleChar(cur.cps.subspan(repetition_index));
    if (digit == 0U) {
      break;
    }
    // 공백은 ns 문자가 따라올 때만 함께 소비
    Advance(cur, repetition_index + digit);
  }
  return true;
}

bool Grammar::IssDoubleNextLine(ParseState& cur, const std::ptrdiff_t n) {
  // 스펙의 자기 재귀를 반복으로 전개 (행 수에 비례한 스택 소비 방지)
  ParseState work = cur;
  bool have = false;
  ParseState res = cur;
  while (true) {
    ParseState trial = work;
    if (!IssDoubleBreak(trial, n)) {
      break;  // 다음 재귀 단계 실패 -> 직전 s-white* 폴백이 결과
    }
    // [207] "Excluding c-forbidden content"를 scalar content-line에 내린다.
    // [206] document marker를 scalar 내용으로 소비하지 않게 한다.
    if (IscForbidden(trial)) {
      break;
    }
    const std::size_t digit = IsnsDoubleChar(trial.cps);
    if (digit == 0U) {
      res = trial;  // 선택 그룹 생략 — 줄바꿈까지만 소비하고 종료
      have = true;
      break;
    }
    Advance(trial, digit);
    IsnbNsDoubleInLine(trial);
    ParseState afterw = trial;
    std::size_t wsp = 0;
    while (IssWhite(At(afterw.cps, wsp)) != 0U) {
      wsp++;
    }
    Advance(afterw, wsp);
    res = afterw;  // (재귀 | s-white*)의 s-white* 폴백 지점
    have = true;
    work = trial;  // 성공한 재귀 단계만 commit
  }
  if (!have) {
    return false;
  }
  cur = res;
  return true;
}

bool Grammar::IsnbDoubleMultiLine(ParseState& cur, const std::ptrdiff_t n) {
  // 반환값을 쓰지 않는 이유는 IsnbNsDoubleInLine은 절대 false를 반환하지 않기
  // 때문임.
  IsnbNsDoubleInLine(cur);
  if (IssDoubleNextLine(cur, n)) {
    return true;
  }
  std::size_t width = 0;
  while (IssWhite(At(cur.cps, width)) != 0U) {
    width++;
  }
  Advance(cur, width);
  return true;
}

std::size_t Grammar::IscQuotedQuote(std::span<const std::uint32_t> cps) {
  if (At(cps, 0) == kCSingleQuote && At(cps, 1) == kCSingleQuote) {
    return 2;
  }
  return 0;
}

std::size_t Grammar::IsnbSingleChar(std::span<const std::uint32_t> cps) {
  if (const std::size_t quote = IscQuotedQuote(cps)) {
    return quote;
  }
  const std::uint32_t code_point = At(cps, 0);
  if (IsnbJson(code_point) == 0U) {
    return 0;
  }
  if (code_point == kCSingleQuote) {
    return 0;
  }
  return 1;
}

std::size_t Grammar::IsnsSingleChar(std::span<const std::uint32_t> cps) {
  const std::size_t len = IsnbSingleChar(cps);
  if (len == 0U) {
    return 0;
  }
  if (len == 1 && (IssWhite(At(cps, 0)) != 0U)) {
    return 0;
  }
  return len;
}

bool Grammar::State::IscSingleQuoted(ParseState& cur,
                                     const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != kCSingleQuote) {
    return false;
  }
  Advance(cur, 1);
  const std::size_t byte = cur.before.size();
  if (!IsnbSingleText(cur, n)) {
    return false;
  }
  const std::size_t event = cur.before.size();
  if (At(cur.cps, 0) != kCSingleQuote) {
    return false;
  }
  Advance(cur, 1);
  AddNode(cur, {.kind = SyntaxKind::kScalar,
                .style = ScalarStyle::kSingleQuoted,
                .begin = byte,
                .end = event});
  return true;
}

bool Grammar::FlowInState::IsnbSingleText(ParseState& cur,
                                          const std::ptrdiff_t n) const {
  return IsnbSingleMultiLine(cur, n);
}

bool Grammar::FlowOutState::IsnbSingleText(ParseState& cur,
                                           const std::ptrdiff_t n) const {
  return IsnbSingleMultiLine(cur, n);
}

bool Grammar::BlockKeyState::IsnbSingleText(ParseState& cur,
                                            const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return IsnbSingleOneLine(cur);
}

bool Grammar::FlowKeyState::IsnbSingleText(ParseState& cur,
                                           const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return IsnbSingleOneLine(cur);
}

bool Grammar::IsnbSingleOneLine(ParseState& cur) {
  while (const std::size_t digit = IsnbSingleChar(cur.cps)) {
    Advance(cur, digit);
  }
  return true;
}

bool Grammar::IsnbNsSingleInLine(ParseState& cur) {
  while (true) {
    std::size_t repetition_index = 0;
    while (IssWhite(At(cur.cps, repetition_index)) != 0U) {
      repetition_index++;
    }
    const std::size_t digit = IsnsSingleChar(cur.cps.subspan(repetition_index));
    if (digit == 0U) {
      break;
    }
    Advance(cur, repetition_index + digit);
  }
  return true;
}

bool Grammar::IssSingleNextLine(ParseState& cur, const std::ptrdiff_t n) {
  // 스펙의 자기 재귀를 반복으로 전개 (행 수에 비례한 스택 소비 방지)
  ParseState work = cur;
  bool have = false;
  ParseState res = cur;
  while (true) {
    ParseState trial = work;
    if (!IssFlowFolded(trial, n)) {
      break;
    }
    // [207] "Excluding c-forbidden content"를 scalar content-line에 내린다.
    // [206] document marker를 scalar 내용으로 소비하지 않게 한다.
    if (IscForbidden(trial)) {
      break;
    }
    const std::size_t digit = IsnsSingleChar(trial.cps);
    if (digit == 0U) {
      res = trial;
      have = true;
      break;
    }
    Advance(trial, digit);
    IsnbNsSingleInLine(trial);
    ParseState afterw = trial;
    std::size_t wsp = 0;
    while (IssWhite(At(afterw.cps, wsp)) != 0U) {
      wsp++;
    }
    Advance(afterw, wsp);
    res = afterw;
    have = true;
    work = trial;
  }
  if (!have) {
    return false;
  }
  cur = res;
  return true;
}

bool Grammar::IsnbSingleMultiLine(ParseState& cur, const std::ptrdiff_t n) {
  // 반환값을 쓰지 않는 이유는 IsnbNsSingleInLine은 절대 false를 반환하지 않기
  // 때문임.
  IsnbNsSingleInLine(cur);
  if (IssSingleNextLine(cur, n)) {
    return true;
  }
  std::size_t width = 0;
  while (IssWhite(At(cur.cps, width)) != 0U) {
    width++;
  }
  Advance(cur, width);
  return true;
}

bool Grammar::State::IsnsPlainFirst(ParseState& cur) const {
  // [207] "Excluding c-forbidden content"를 scalar content-line에 내린다.
  // [206] 자체는 매치한 production을 소비하므로 별도 trial로 검사한다.
  ParseState forbidden = cur;
  if (IscForbidden(forbidden)) {
    return false;
  }
  const std::uint32_t code_point = At(cur.cps, 0);
  if ((IsnsChar(code_point) != 0U) && (IscIndicator(code_point) == 0U)) {
    Advance(cur, 1);
    return true;
  }
  switch (code_point) {
    case kCMappingKey:
    case kCMappingValue:
    case kCSequenceEntry:
      // [lookahead = ns-plain-safe(c)]
      if (IsnsPlainSafe(At(cur.cps, 1)) != 0U) {
        Advance(cur, 1);
        return true;
      }
      break;
    default:
      break;
  }
  return false;
}

std::uint32_t Grammar::FlowOutState::IsnsPlainSafe(
    const std::uint32_t code_point) const {
  return IsnsPlainSafeOut(code_point);
}

std::uint32_t Grammar::FlowInState::IsnsPlainSafe(
    const std::uint32_t code_point) const {
  return IsnsPlainSafeIn(code_point);
}

std::uint32_t Grammar::BlockKeyState::IsnsPlainSafe(
    const std::uint32_t code_point) const {
  return IsnsPlainSafeOut(code_point);
}

std::uint32_t Grammar::FlowKeyState::IsnsPlainSafe(
    const std::uint32_t code_point) const {
  return IsnsPlainSafeIn(code_point);
}

std::uint32_t Grammar::IsnsPlainSafeOut(const std::uint32_t code_point) {
  return IsnsChar(code_point);
}

std::uint32_t Grammar::IsnsPlainSafeIn(const std::uint32_t code_point) {
  if (IsnsChar(code_point) == 0U) {
    return 0;
  }
  if (IscFlowIndicator(code_point) != 0U) {
    return 0;
  }
  return code_point;
}

bool Grammar::State::IsnsPlainChar(ParseState& cur) const {
  const std::uint32_t code_point = At(cur.cps, 0);
  if ((IsnsPlainSafe(code_point) != 0U) && code_point != kCMappingValue &&
      code_point != kCComment) {
    Advance(cur, 1);
    return true;
  }
  // [lookbehind = ns-char] '#'
  if (code_point == kCComment && !cur.before.empty() &&
      (IsnsChar(cur.before.back()) != 0U)) {
    Advance(cur, 1);
    return true;
  }
  // ':' [lookahead = ns-plain-safe(c)]
  if (code_point == kCMappingValue && (IsnsPlainSafe(At(cur.cps, 1)) != 0U)) {
    Advance(cur, 1);
    return true;
  }
  return false;
}

bool Grammar::FlowOutState::IsnsPlain(ParseState& cur,
                                      const std::ptrdiff_t n) const {
  return IsnsPlainMultiLine(cur, n);
}

bool Grammar::FlowInState::IsnsPlain(ParseState& cur,
                                     const std::ptrdiff_t n) const {
  return IsnsPlainMultiLine(cur, n);
}

bool Grammar::BlockKeyState::IsnsPlain(ParseState& cur,
                                       const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return IsnsPlainOneLine(cur);
}

bool Grammar::FlowKeyState::IsnsPlain(ParseState& cur,
                                      const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return IsnsPlainOneLine(cur);
}

bool Grammar::State::IsnbNsPlainInLine(ParseState& cur) const {
  while (true) {
    ParseState trial = cur;
    std::size_t width = 0;
    while (IssWhite(At(trial.cps, width)) != 0U) {
      width++;
    }
    Advance(trial, width);
    if (!IsnsPlainChar(trial)) {
      break;
    }
    cur = trial;  // 공백은 plain 문자가 따라올 때만 commit
  }
  return true;
}

bool Grammar::State::IsnsPlainOneLine(ParseState& cur) const {
  if (!IsnsPlainFirst(cur)) {
    return false;
  }
  return IsnbNsPlainInLine(cur);
}

bool Grammar::State::IssNsPlainNextLine(ParseState& cur,
                                        const std::ptrdiff_t n) const {
  if (!IssFlowFolded(cur, n)) {
    return false;
  }
  // [207] "Excluding c-forbidden content"를 plain continuation에 내린다.
  // [206] 자체는 매치한 production을 소비하므로 별도 trial로 검사한다.
  ParseState forbidden = cur;
  if (IscForbidden(forbidden)) {
    return false;
  }
  if (!IsnsPlainChar(cur)) {
    return false;
  }
  return IsnbNsPlainInLine(cur);
}

bool Grammar::State::IsnsPlainMultiLine(ParseState& cur,
                                        const std::ptrdiff_t n) const {
  if (!IsnsPlainOneLine(cur)) {
    return false;
  }
  while (true) {
    ParseState trial = cur;
    if (!IssNsPlainNextLine(trial, n)) {
      break;
    }
    cur = trial;
  }
  return true;
}

const Grammar::State* Grammar::FlowOutState::InFlow() const {
  return &flow_in_state;
}

const Grammar::State* Grammar::FlowInState::InFlow() const {
  return &flow_in_state;
}

const Grammar::State* Grammar::BlockKeyState::InFlow() const {
  return &flow_key_state;
}

const Grammar::State* Grammar::FlowKeyState::InFlow() const {
  return &flow_key_state;
}

bool Grammar::State::IsinFlow(ParseState& cur, const std::ptrdiff_t n) const {
  const State* in_flow = InFlow();
  if (in_flow == nullptr) {
    return false;
  }
  return in_flow->IsnsSFlowSeqEntries(cur, n);
}

bool Grammar::State::IscFlowSequence(ParseState& cur,
                                     const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != kCSequenceStart) {
    return false;
  }
  Advance(cur, 1);
  OpenNode(cur, SyntaxKind::kSequence, cur.before.size());
  IssSeparate(cur, n);
  {
    ParseState trial = cur;
    if (IsinFlow(trial, n)) {
      cur = trial;
    }
  }
  if (At(cur.cps, 0) != kCSequenceEnd) {
    return false;
  }
  Advance(cur, 1);
  CloseNode(cur, cur.before.size());
  return true;
}

bool Grammar::State::IsnsSFlowSeqEntries(ParseState& cur,
                                         const std::ptrdiff_t n) const {
  // 스펙의 꼬리 재귀를 반복으로 전개 (콤마 수에 비례한 스택 소비 방지)
  if (!IsnsFlowSeqEntry(cur, n)) {
    return false;
  }
  IssSeparate(cur, n);
  while (At(cur.cps, 0) == kCCollectEntry) {
    Advance(cur, 1);
    IssSeparate(cur, n);
    {
      ParseState trial = cur;
      if (!IsnsFlowSeqEntry(trial, n)) {
        return true;  // 항목 없는 후행 콤마 허용
      }
      cur = trial;
    }
    IssSeparate(cur, n);
  }
  return true;
}

bool Grammar::State::IsnsFlowSeqEntry(ParseState& cur,
                                      const std::ptrdiff_t n) const {
  // [139] predictive dispatch. 지수 backtracking 회피 근거는 선언에 기록.
  ParseState pair = cur;
  if ((At(cur.cps, 0) == kCMappingKey ||
       At(cur.cps, 0) == kCMappingValue) &&
      IsnsFlowPair(pair, n)) {
    cur = pair;
    return true;
  }

  ParseState node = cur;
  if (!IsnsFlowNode(node, n)) {
    return false;
  }
  ParseState after_node = node;
  IssSeparateInLine(after_node);
  if (At(after_node.cps, 0) == kCMappingValue) {
    pair = cur;
    if (IsnsFlowPair(pair, n)) {
      cur = pair;
      return true;
    }
  }
  cur = node;
  return true;
}

bool Grammar::State::IscFlowMapping(ParseState& cur,
                                    const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != kCMappingStart) {
    return false;
  }
  Advance(cur, 1);
  OpenNode(cur, SyntaxKind::kMapping, cur.before.size());
  IssSeparate(cur, n);
  // ns-s-flow-map-entries(n,in-flow(c))?
  if (const State* in_flow = InFlow()) {
    ParseState trial = cur;
    if (in_flow->IsnsSFlowMapEntries(trial, n)) {
      cur = trial;
    }
  }
  if (At(cur.cps, 0) != kCMappingEnd) {
    return false;
  }
  Advance(cur, 1);
  CloseNode(cur, cur.before.size());
  return true;
}

bool Grammar::State::IsnsSFlowMapEntries(ParseState& cur,
                                         const std::ptrdiff_t n) const {
  // 스펙의 꼬리 재귀를 반복으로 전개 (콤마 수에 비례한 스택 소비 방지)
  if (!IsnsFlowMapEntry(cur, n)) {
    return false;
  }
  IssSeparate(cur, n);
  while (At(cur.cps, 0) == kCCollectEntry) {
    Advance(cur, 1);
    IssSeparate(cur, n);
    {
      ParseState trial = cur;
      if (!IsnsFlowMapEntry(trial, n)) {
        return true;  // 항목 없는 후행 콤마 허용
      }
      cur = trial;
    }
    IssSeparate(cur, n);
  }
  return true;
}

bool Grammar::State::IsnsFlowMapEntry(ParseState& cur,
                                      const std::ptrdiff_t n) const {
  // '?'는 뒤에 분리가 와야 명시적 키 — 아니면 암시적 항목으로 백트래킹
  if (At(cur.cps, 0) == kCMappingKey) {
    ParseState trial = cur;
    Advance(trial, 1);
    if (IssSeparate(trial, n) && IsnsFlowMapExplicitEntry(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IsnsFlowMapImplicitEntry(cur, n);
}

bool Grammar::State::IsnsFlowMapExplicitEntry(ParseState& cur,
                                              const std::ptrdiff_t n) const {
  if (IsnsFlowMapImplicitEntry(cur, n)) {
    return true;
  }
  IseNode(cur);  // e-node e-node — 빈 키 + 빈 값
  IseNode(cur);
  return true;
}

bool Grammar::State::IsnsFlowMapImplicitEntry(ParseState& cur,
                                              const std::ptrdiff_t n) const {
  // 최장 매치 선택: yaml-key 대안이 "속성만 있는 빈 키"(예: {!!str "a": v}의
  // "!!str")로 짧게 성공해 json-key 대안을 가리는 것을 방지 — 스펙의
  // 선언적 알터네이션 의미론 유지
  const ParseState start = cur;
  int winner = -1;
  std::size_t best = 0;
  {
    ParseState trial = start;
    if (IsnsFlowMapYamlKeyEntry(trial, n)) {
      winner = 0;
      best = trial.before.size();
    }
  }
  {
    ParseState trial = start;
    if (IscNsFlowMapEmptyKeyEntry(trial, n) &&
        (winner < 0 || trial.before.size() > best)) {
      winner = 1;
      best = trial.before.size();
    }
  }
  {
    ParseState trial = start;
    if (IscNsFlowMapJsonKeyEntry(trial, n) &&
        (winner < 0 || trial.before.size() > best)) {
      winner = 2;
    }
  }
  if (winner < 0) {
    return false;
  }
  // 승자를 마지막에 다시 실행해 승자 대안의 구문 트리만 남긴다
  switch (winner) {
    case 0:
      return IsnsFlowMapYamlKeyEntry(cur, n);
    case 1:
      return IscNsFlowMapEmptyKeyEntry(cur, n);
    default:
      return IscNsFlowMapJsonKeyEntry(cur, n);
  }
}

bool Grammar::State::IsnsFlowMapYamlKeyEntry(ParseState& cur,
                                             const std::ptrdiff_t n) const {
  if (!IsnsFlowYamlNode(cur, n)) {
    return false;
  }
  {
    ParseState trial = cur;
    IssSeparate(trial, n);
    if (IscNsFlowMapSeparateValue(trial, n)) {
      cur = trial;
      return true;
    }
  }
  // e-node — 값 없는 키
  IseNode(cur);
  return true;
}

bool Grammar::State::IscNsFlowMapEmptyKeyEntry(ParseState& cur,
                                               const std::ptrdiff_t n) const {
  // e-node(빈 키) 뒤 값 — 실패한 caller trial과 구문 트리를 함께 폐기한다.
  IseNode(cur);
  return IscNsFlowMapSeparateValue(cur, n);
}

bool Grammar::State::IscNsFlowMapSeparateValue(ParseState& cur,
                                               const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != kCMappingValue) {
    return false;
  }
  // [lookahead != ns-plain-safe(c)] — ':'가 plain의 일부가 아닐 것
  if (IsnsPlainSafe(At(cur.cps, 1)) != 0U) {
    return false;
  }
  Advance(cur, 1);
  {
    ParseState trial = cur;
    if (IssSeparate(trial, n) && IsnsFlowNode(trial, n)) {
      cur = trial;
      return true;
    }
  }
  IseNode(cur);  // e-node — 빈 값
  return true;
}

bool Grammar::State::IscNsFlowMapJsonKeyEntry(ParseState& cur,
                                              const std::ptrdiff_t n) const {
  if (!IscFlowJsonNode(cur, n)) {
    return false;
  }
  {
    ParseState trial = cur;
    IssSeparate(trial, n);
    if (IscNsFlowMapAdjacentValue(trial, n)) {
      cur = trial;
      return true;
    }
  }
  // e-node — 값 없는 키
  IseNode(cur);
  return true;
}

bool Grammar::State::IscNsFlowMapAdjacentValue(ParseState& cur,
                                               const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != kCMappingValue) {
    return false;
  }
  Advance(cur, 1);
  {
    ParseState trial = cur;
    IssSeparate(trial, n);
    if (IsnsFlowNode(trial, n)) {
      cur = trial;
      return true;
    }
  }
  // e-node — 빈 값
  IseNode(cur);
  return true;
}

bool Grammar::State::IsnsFlowPair(ParseState& cur,
                                  const std::ptrdiff_t n) const {
  // 흐름 시퀀스 안의 단일 쌍은 암시적 단일 항목 매핑이다
  OpenNode(cur, SyntaxKind::kMapping, cur.before.size());
  if (At(cur.cps, 0) == kCMappingKey) {
    ParseState trial = cur;
    Advance(trial, 1);
    if (IssSeparate(trial, n) && IsnsFlowMapExplicitEntry(trial, n)) {
      CloseNode(trial, trial.before.size());
      cur = trial;
      return true;
    }
  }
  if (!IsnsFlowPairEntry(cur, n)) {
    return false;
  }
  CloseNode(cur, cur.before.size());
  return true;
}

bool Grammar::State::IsnsFlowPairEntry(ParseState& cur,
                                       const std::ptrdiff_t n) const {
  const ParseState start = cur;
  int winner = -1;
  std::size_t best = 0;
  {
    ParseState trial = start;
    if (IsnsFlowPairYamlKeyEntry(trial, n)) {
      winner = 0;
      best = trial.before.size();
    }
  }
  {
    ParseState trial = start;
    if (IscNsFlowMapEmptyKeyEntry(trial, n) &&
        (winner < 0 || trial.before.size() > best)) {
      winner = 1;
      best = trial.before.size();
    }
  }
  {
    ParseState trial = start;
    if (IscNsFlowPairJsonKeyEntry(trial, n) &&
        (winner < 0 || trial.before.size() > best)) {
      winner = 2;
    }
  }
  if (winner < 0) {
    return false;
  }
  // 승자를 마지막에 다시 실행해 승자 대안의 구문 트리만 남긴다
  switch (winner) {
    case 0:
      return IsnsFlowPairYamlKeyEntry(cur, n);
    case 1:
      return IscNsFlowMapEmptyKeyEntry(cur, n);
    default:
      return IscNsFlowPairJsonKeyEntry(cur, n);
  }
}

bool Grammar::State::IsnsFlowPairYamlKeyEntry(ParseState& cur,
                                              const std::ptrdiff_t n) const {
  if (!flow_key_state.IsnsSImplicitYamlKey(cur)) {
    return false;
  }
  return IscNsFlowMapSeparateValue(cur, n);
}

bool Grammar::State::IscNsFlowPairJsonKeyEntry(ParseState& cur,
                                               const std::ptrdiff_t n) const {
  if (!flow_key_state.IscSImplicitJsonKey(cur)) {
    return false;
  }
  return IscNsFlowMapAdjacentValue(cur, n);
}

bool Grammar::State::IsnsSImplicitYamlKey(ParseState& cur) const {
  // 스펙의 1024자 제한을 파싱 창으로도 강제해 키 후보 파싱 비용을 제한
  const std::size_t win =
      std::min(cur.cps.size(), static_cast<std::size_t>(1026));
  ParseState sub = cur;  // 제한 창에서 만든 구문 트리/진단 상태를 성공 시 승계
  sub.cps = cur.cps.first(win);
  if (!IsnsFlowYamlNode(sub, 0)) {  // n은 무관(단일 행)
    return false;
  }
  IssSeparateInLine(sub);
  const std::size_t consumed = sub.before.size() - cur.before.size();
  if (consumed > 1024) {
    return false;  // 스펙: 전체 1024자 제한
  }
  Advance(cur, consumed);
  cur.roots = std::move(sub.roots);
  cur.frame = std::move(sub.frame);
  return true;
}

bool Grammar::State::IscSImplicitJsonKey(ParseState& cur) const {
  // 스펙의 1024자 제한을 파싱 창으로도 강제해 키 후보 파싱 비용을 제한
  const std::size_t win =
      std::min(cur.cps.size(), static_cast<std::size_t>(1026));
  ParseState sub = cur;  // 제한 창에서 만든 구문 트리/진단 상태를 성공 시 승계
  sub.cps = cur.cps.first(win);
  if (!IscFlowJsonNode(sub, 0)) {  // n은 무관(단일 행)
    return false;
  }
  IssSeparateInLine(sub);
  const std::size_t consumed = sub.before.size() - cur.before.size();
  if (consumed > 1024) {
    return false;
  }
  Advance(cur, consumed);
  cur.roots = std::move(sub.roots);
  cur.frame = std::move(sub.frame);
  return true;
}

bool Grammar::State::IsnsFlowYamlContent(ParseState& cur,
                                         const std::ptrdiff_t n) const {
  const std::size_t byte = cur.before.size();
  if (!IsnsPlain(cur, n)) {
    return false;
  }
  AddNode(cur, {.kind = SyntaxKind::kScalar,
                .style = ScalarStyle::kPlain,
                .begin = byte,
                .end = cur.before.size()});
  return true;
}

bool Grammar::State::IscFlowJsonContent(ParseState& cur,
                                        const std::ptrdiff_t n) const {
  {
    ParseState trial = cur;
    if (IscFlowSequence(trial, n)) {
      cur = trial;
      return true;
    }
  }
  {
    ParseState trial = cur;
    if (IscFlowMapping(trial, n)) {
      cur = trial;
      return true;
    }
  }
  {
    ParseState trial = cur;
    if (IscSingleQuoted(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IscDoubleQuoted(cur, n);
}

bool Grammar::State::IsnsFlowContent(ParseState& cur,
                                     const std::ptrdiff_t n) const {
  if (IsnsFlowYamlContent(cur, n)) {
    return true;
  }
  return IscFlowJsonContent(cur, n);
}

bool Grammar::State::IsnsFlowYamlNode(ParseState& cur,
                                      const std::ptrdiff_t n) const {
  {
    ParseState trial = cur;
    if (IscNsAliasNode(trial)) {
      cur = trial;
      return true;
    }
  }
  if (IsnsFlowYamlContent(cur, n)) {
    return true;
  }
  if (IscNsProperties(cur, n)) {
    ParseState trial = cur;
    if (IssSeparate(trial, n) && IsnsFlowYamlContent(trial, n)) {
      cur = trial;
      return true;
    }
    // e-scalar — 속성만 있는 빈 스칼라
    IseScalar(cur);
    return true;
  }
  return false;
}

bool Grammar::State::IscFlowJsonNode(ParseState& cur,
                                     const std::ptrdiff_t n) const {
  // (c-ns-properties(n,c) s-separate(n,c))? — 그룹 성공 후 본문이 실패하면
  // 그룹 없이 재시도
  {
    ParseState trial = cur;
    if (IscNsProperties(trial, n) && IssSeparate(trial, n) &&
        IscFlowJsonContent(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IscFlowJsonContent(cur, n);
}

bool Grammar::State::IsnsFlowNode(ParseState& cur,
                                  const std::ptrdiff_t n) const {
  {
    ParseState trial = cur;
    if (IscNsAliasNode(trial)) {
      cur = trial;
      return true;
    }
  }
  {
    ParseState trial = cur;
    if (IsnsFlowContent(trial, n)) {
      cur = trial;
      return true;
    }
  }
  if (IscNsProperties(cur, n)) {
    ParseState trial = cur;
    if (IssSeparate(trial, n) && IsnsFlowContent(trial, n)) {
      cur = trial;
      return true;
    }
    // e-scalar — 속성만 있는 빈 스칼라
    IseScalar(cur);
    return true;
  }
  return false;
}

Grammar::BlockHeader Grammar::IscBBlockHeader(ParseState& cur) {
  // 스펙 1.2.2의 [162][163] 표기는 들여쓰기 지시자를 필수처럼 적고
  // 있으나(알려진 결함), prose 8.1.1.1과 1.2.1의 [163]에 따라 생략 가능하며
  // 생략 시 자동 감지(m = 0으로 표시)로 구현
  std::ptrdiff_t indent_width = 0;
  const ChompingState* trial = &clip_state;
  if (const std::uint32_t digit = IscIndentationIndicator(At(cur.cps, 0))) {
    indent_width = static_cast<std::ptrdiff_t>(digit - '0');
    Advance(cur, 1);
    trial = IscChompingIndicator(cur);
  } else {
    trial = IscChompingIndicator(cur);
    if (const std::uint32_t second_digit =
            IscIndentationIndicator(At(cur.cps, 0))) {
      indent_width = static_cast<std::ptrdiff_t>(second_digit - '0');
      Advance(cur, 1);
    }
  }
  if (!IssBComment(cur)) {
    return {false, 0, nullptr};
  }
  return {true, indent_width, trial};
}

std::uint32_t Grammar::IscIndentationIndicator(const std::uint32_t code_point) {
  if ('1' <= code_point && code_point <= '9') {
    return code_point;
  }
  return 0;
}

const Grammar::ChompingState* Grammar::IscChompingIndicator(ParseState& cur) {
  switch (At(cur.cps, 0)) {
    case '-':
      Advance(cur, 1);
      return &strip_state;
    case '+':
      Advance(cur, 1);
      return &keep_state;
    default:
      break;
  }
  return &clip_state;  // "" — 빈 매치
}
bool Grammar::ChompingState::IsbChompedLast(ParseState& cur) const {
  if (AtEnd(cur.cps, 0)) {
    return true;  // <end-of-input>
  }
  const std::size_t byte = IsbAsLineFeed(cur.cps);
  if (byte != 0U) {
    Advance(cur, byte);
    return true;
  }
  return false;
}
bool Grammar::StripState::IsbChompedLast(ParseState& cur) const {
  if (AtEnd(cur.cps, 0)) {
    return true;
  }
  const std::size_t byte = IsbNonContent(cur.cps);
  if (byte != 0U) {
    Advance(cur, byte);
    return true;
  }
  return false;
}
bool Grammar::ChompingState::IslChompedEmpty(ParseState& cur,
                                             const std::ptrdiff_t n) const {
  return IslStripEmpty(cur, n);
}
bool Grammar::KeepState::IslChompedEmpty(ParseState& cur,
                                         const std::ptrdiff_t n) const {
  return IslKeepEmpty(cur, n);
}

bool Grammar::IslStripEmpty(ParseState& cur, const std::ptrdiff_t n) {
  while (true) {
    ParseState trial = cur;
    if (!IssIndentLessOrEqual(trial, n)) {
      break;
    }
    const std::size_t byte = IsbNonContent(trial.cps);
    if (byte == 0U) {
      break;
    }
    Advance(trial, byte);
    cur = trial;
  }
  {
    ParseState trial = cur;
    if (IslTrailComments(trial, n)) {
      cur = trial;
    }
  }
  return true;
}

bool Grammar::IslKeepEmpty(ParseState& cur, const std::ptrdiff_t n) {
  while (true) {
    ParseState trial = cur;
    if (!block_in_state.IslEmpty(trial, n)) {
      break;
    }
    cur = trial;
  }
  {
    ParseState trial = cur;
    if (IslTrailComments(trial, n)) {
      cur = trial;
    }
  }
  return true;
}

bool Grammar::IslTrailComments(ParseState& cur, const std::ptrdiff_t n) {
  if (!IssIndentLessThan(cur, n)) {
    return false;
  }
  if (!IscNbCommentText(cur)) {
    return false;
  }
  if (!IsbComment(cur)) {
    return false;
  }
  while (true) {
    ParseState trial = cur;
    if (!IslComment(trial)) {
      break;
    }
    if (trial.before.size() == cur.before.size()) {
      break;
    }
    cur = trial;
  }
  return true;
}

bool Grammar::IscLLiteral(ParseState& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != kCLiteral) {
    return false;
  }
  Advance(cur, 1);
  const BlockHeader hex_value = IscBBlockHeader(cur);
  if (!hex_value.ok) {
    return false;
  }
  std::ptrdiff_t indent_width = hex_value.indent_width;
  if (indent_width == 0) {
    const std::ptrdiff_t det = DetectScalarIndentation(cur.cps, n);
    if (det == 0) {
      return false;
    }
    indent_width = det;
  }
  const std::size_t byte = cur.before.size();
  if (!IslLiteralContent(cur, n + indent_width, *hex_value.trial)) {
    return false;
  }
  AddNode(cur, {.kind = SyntaxKind::kScalar,
                .style = ScalarStyle::kLiteral,
                .chomp = ChompKindOf(hex_value.trial),
                .indent = n + indent_width,
                .begin = byte,
                .end = cur.before.size()});
  return true;
}

bool Grammar::IslNbLiteralText(ParseState& cur, const std::ptrdiff_t n) {
  while (true) {
    ParseState trial = cur;
    if (!block_in_state.IslEmpty(trial, n)) {
      break;
    }
    cur = trial;
  }
  if (!IssIndent(cur, n)) {
    return false;
  }
  // [207] "Excluding c-forbidden content"를 literal content line에 내린다.
  // [206] 자체는 매치한 production을 소비하므로 별도 trial로 검사한다.
  ParseState forbidden = cur;
  if (IscForbidden(forbidden)) {
    return false;
  }
  std::size_t repetition_index = 0;
  while (IsnbChar(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  if (repetition_index == 0) {
    return false;  // nb-char+ — 1자 이상
  }
  Advance(cur, repetition_index);
  return true;
}

bool Grammar::IsbNbLiteralNext(ParseState& cur, const std::ptrdiff_t n) {
  const std::size_t byte = IsbAsLineFeed(cur.cps);
  if (byte == 0U) {
    return false;
  }
  Advance(cur, byte);
  return IslNbLiteralText(cur, n);
}

bool Grammar::IslLiteralContent(ParseState& cur, const std::ptrdiff_t n,
                                const ChompingState& trial) {
  // (l-nb-literal-text b-nb-literal-next* b-chomped-last)? l-chomped-empty
  {
    ParseState trial_state = cur;
    if (IslNbLiteralText(trial_state, n)) {
      while (true) {
        ParseState next = trial_state;
        if (!IsbNbLiteralNext(next, n)) {
          break;
        }
        trial_state = next;
      }
      if (trial.IsbChompedLast(trial_state) &&
          trial.IslChompedEmpty(trial_state, n)) {
        cur = trial_state;
        return true;
      }
    }
  }
  return trial.IslChompedEmpty(cur, n);
}

bool Grammar::IscLFolded(ParseState& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != kCFolded) {
    return false;
  }
  Advance(cur, 1);
  const BlockHeader hex_value = IscBBlockHeader(cur);
  if (!hex_value.ok) {
    return false;
  }
  std::ptrdiff_t indent_width = hex_value.indent_width;
  if (indent_width == 0) {
    const std::ptrdiff_t det = DetectScalarIndentation(cur.cps, n);
    if (det == 0) {
      return false;
    }
    indent_width = det;
  }
  const std::size_t byte = cur.before.size();
  if (!IslFoldedContent(cur, n + indent_width, *hex_value.trial)) {
    return false;
  }
  AddNode(cur, {.kind = SyntaxKind::kScalar,
                .style = ScalarStyle::kFolded,
                .chomp = ChompKindOf(hex_value.trial),
                .indent = n + indent_width,
                .begin = byte,
                .end = cur.before.size()});
  return true;
}

bool Grammar::IssNbFoldedText(ParseState& cur, const std::ptrdiff_t n) {
  if (!IssIndent(cur, n)) {
    return false;
  }
  // [207] "Excluding c-forbidden content"를 folded content line에 내린다.
  // [206] 자체는 매치한 production을 소비하므로 별도 trial로 검사한다.
  ParseState forbidden = cur;
  if (IscForbidden(forbidden)) {
    return false;
  }
  if (IsnsChar(At(cur.cps, 0)) == 0U) {
    return false;
  }
  std::size_t repetition_index = 1;
  while (IsnbChar(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  Advance(cur, repetition_index);
  return true;
}

bool Grammar::IslNbFoldedLines(ParseState& cur, const std::ptrdiff_t n) {
  if (!IssNbFoldedText(cur, n)) {
    return false;
  }
  while (true) {
    ParseState trial = cur;
    if (!block_in_state.IsbLFolded(trial, n)) {
      break;
    }
    if (!IssNbFoldedText(trial, n)) {
      break;
    }
    cur = trial;
  }
  return true;
}

bool Grammar::IssNbSpacedText(ParseState& cur, const std::ptrdiff_t n) {
  if (!IssIndent(cur, n)) {
    return false;
  }
  // 첫 문자가 s-white이므로 문서 마커일 수 없음 — c-forbidden 검사 불필요
  if (IssWhite(At(cur.cps, 0)) == 0U) {
    return false;
  }
  std::size_t repetition_index = 1;
  while (IsnbChar(At(cur.cps, repetition_index)) != 0U) {
    repetition_index++;
  }
  Advance(cur, repetition_index);
  return true;
}

bool Grammar::IsbLSpaced(ParseState& cur, const std::ptrdiff_t n) {
  const std::size_t byte = IsbAsLineFeed(cur.cps);
  if (byte == 0U) {
    return false;
  }
  Advance(cur, byte);
  while (true) {
    ParseState trial = cur;
    if (!block_in_state.IslEmpty(trial, n)) {
      break;
    }
    cur = trial;
  }
  return true;
}

bool Grammar::IslNbSpacedLines(ParseState& cur, const std::ptrdiff_t n) {
  if (!IssNbSpacedText(cur, n)) {
    return false;
  }
  while (true) {
    ParseState trial = cur;
    if (!IsbLSpaced(trial, n)) {
      break;
    }
    if (!IssNbSpacedText(trial, n)) {
      break;
    }
    cur = trial;
  }
  return true;
}

bool Grammar::IslNbSameLines(ParseState& cur, const std::ptrdiff_t n) {
  while (true) {
    ParseState trial = cur;
    if (!block_in_state.IslEmpty(trial, n)) {
      break;
    }
    cur = trial;
  }
  {
    ParseState trial = cur;
    if (IslNbFoldedLines(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IslNbSpacedLines(cur, n);
}

bool Grammar::IslNbDiffLines(ParseState& cur, const std::ptrdiff_t n) {
  if (!IslNbSameLines(cur, n)) {
    return false;
  }
  while (true) {
    ParseState trial = cur;
    const std::size_t byte = IsbAsLineFeed(trial.cps);
    if (byte == 0U) {
      break;
    }
    Advance(trial, byte);
    if (!IslNbSameLines(trial, n)) {
      break;
    }
    cur = trial;
  }
  return true;
}

bool Grammar::IslFoldedContent(ParseState& cur, const std::ptrdiff_t n,
                               const ChompingState& trial) {
  // (l-nb-diff-lines b-chomped-last)? l-chomped-empty
  {
    ParseState trial_state = cur;
    if (IslNbDiffLines(trial_state, n) && trial.IsbChompedLast(trial_state) &&
        trial.IslChompedEmpty(trial_state, n)) {
      cur = trial_state;
      return true;
    }
  }
  return trial.IslChompedEmpty(cur, n);
}

bool Grammar::IslBlockSequence(ParseState& cur, const std::ptrdiff_t n) {
  // m 자동 감지: 첫 항목 행의 들여쓰기 w = n+1+m (m >= 0)
  const std::ptrdiff_t entry_indent =
      static_cast<std::ptrdiff_t>(LeadingSpaces(cur.cps));

  // [183]의 자동 감지 m은 entry_indent - (n + 1)이며 m >= 0이어야 한다.
  if (entry_indent < n + 1) {
    return false;
  }
  OpenNode(cur, SyntaxKind::kSequence, cur.before.size());
  std::size_t count = 0;
  while (true) {
    ParseState trial = cur;
    if (!IssIndent(trial, entry_indent)) {
      break;
    }
    if (!IscLBlockSeqEntry(trial, entry_indent)) {
      break;
    }
    cur = trial;
    count++;
  }
  if (count == 0U) {
    return false;  // 실패한 caller trial이 열린 sequence frame도 함께 폐기한다.
  }
  CloseNode(cur, cur.before.size());
  return true;
}

bool Grammar::IscLBlockSeqEntry(ParseState& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != kCSequenceEntry) {
    return false;
  }
  // [lookahead != ns-char] — "-a"는 plain 스칼라의 시작
  if (IsnsChar(At(cur.cps, 1)) != 0U) {
    return false;
  }
  Advance(cur, 1);
  return block_in_state.IssLBlockIndented(cur, n);
}

bool Grammar::State::IssLBlockIndented(ParseState& cur,
                                       const std::ptrdiff_t n) const {
  // [185]의 m 자동 감지: 현 위치의 공백 수.
  const std::ptrdiff_t m = static_cast<std::ptrdiff_t>(LeadingSpaces(cur.cps));
  {
    ParseState compact = cur;
    // [185] s-indent(m): 측정과 소비를 분리해 production을 직접 표현한다.
    if (IssIndent(compact, m)) {
      const std::ptrdiff_t indent = n + 1 + m;
      {
        ParseState trial = compact;
        if (IsnsLCompactSequence(trial, indent)) {
          cur = trial;
          return true;
        }
      }
      {
        ParseState trial = compact;
        if (IsnsLCompactMapping(trial, indent)) {
          cur = trial;
          return true;
        }
      }
    }
  }
  {
    ParseState trial = cur;
    if (IssLBlockNode(trial, n)) {
      cur = trial;
      return true;
    }
  }
  // e-node s-l-comments — 빈 노드
  IseNode(cur);
  if (!IssLComments(cur)) {
    return false;
  }
  return true;
}

bool Grammar::IsnsLCompactSequence(ParseState& cur, const std::ptrdiff_t n) {
  OpenNode(cur, SyntaxKind::kSequence, cur.before.size());
  if (!IscLBlockSeqEntry(cur, n)) {
    return false;
  }
  while (true) {
    ParseState trial = cur;
    if (!IssIndent(trial, n)) {
      break;
    }
    if (!IscLBlockSeqEntry(trial, n)) {
      break;
    }
    cur = trial;
  }
  CloseNode(cur, cur.before.size());
  return true;
}

bool Grammar::IslBlockMapping(ParseState& cur, const std::ptrdiff_t n) {
  // m 자동 감지: 첫 항목 행의 들여쓰기 w = n+1+m (m >= 0)
  const std::ptrdiff_t entry_indent =
      static_cast<std::ptrdiff_t>(LeadingSpaces(cur.cps));

  // [187]의 자동 감지 m은 entry_indent - (n + 1)이며 m >= 0이어야 한다.
  if (entry_indent < n + 1) {
    return false;
  }
  OpenNode(cur, SyntaxKind::kMapping, cur.before.size());
  std::size_t count = 0;
  while (true) {
    ParseState trial = cur;
    if (!IssIndent(trial, entry_indent)) {
      break;
    }
    if (!IsnsLBlockMapEntry(trial, entry_indent)) {
      break;
    }
    cur = trial;
    count++;
  }
  if (count == 0U) {
    return false;  // 실패한 caller trial이 열린 mapping frame도 함께 폐기한다.
  }
  CloseNode(cur, cur.before.size());
  return true;
}

bool Grammar::IsnsLBlockMapEntry(ParseState& cur, const std::ptrdiff_t n) {
  {
    ParseState trial = cur;
    if (IscLBlockMapExplicitEntry(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IsnsLBlockMapImplicitEntry(cur, n);
}

bool Grammar::IscLBlockMapExplicitEntry(ParseState& cur,
                                        const std::ptrdiff_t n) {
  if (!IscLBlockMapExplicitKey(cur, n)) {
    return false;
  }
  {
    ParseState trial = cur;
    if (IslBlockMapExplicitValue(trial, n)) {
      cur = trial;
      return true;
    }
  }
  IseNode(cur);  // e-node — 값 없는 명시적 키
  return true;
}

bool Grammar::IscLBlockMapExplicitKey(ParseState& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != kCMappingKey) {
    return false;
  }
  // '?' 뒤에 ns-char가 붙으면 s-l+block-indented가 자연히 실패하고
  // 암시적 키(plain "?...")로 백트래킹된다
  Advance(cur, 1);
  return block_out_state.IssLBlockIndented(cur, n);
}

bool Grammar::IslBlockMapExplicitValue(ParseState& cur,
                                       const std::ptrdiff_t n) {
  if (!IssIndent(cur, n)) {
    return false;
  }
  if (At(cur.cps, 0) != kCMappingValue) {
    return false;
  }
  Advance(cur, 1);
  return block_out_state.IssLBlockIndented(cur, n);
}

bool Grammar::IsnsLBlockMapImplicitEntry(ParseState& cur,
                                         const std::ptrdiff_t n) {
  {
    ParseState trial = cur;
    if (IsnsSBlockMapImplicitKey(trial) &&
        IscLBlockMapImplicitValue(trial, n)) {
      cur = trial;
      return true;
    }
  }
  // | e-node — 빈 키 (":"로 시작하는 행)
  // 실패한 caller trial과 빈 키 구문 트리를 함께 폐기한다.
  IseNode(cur);
  return IscLBlockMapImplicitValue(cur, n);
}

bool Grammar::IsnsSBlockMapImplicitKey(ParseState& cur) {
  if (block_key_state.IscSImplicitJsonKey(cur)) {
    return true;
  }
  return block_key_state.IsnsSImplicitYamlKey(cur);
}

bool Grammar::IscLBlockMapImplicitValue(ParseState& cur,
                                        const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != kCMappingValue) {
    return false;
  }
  Advance(cur, 1);
  {
    ParseState trial = cur;
    if (block_out_state.IssLBlockNode(trial, n)) {
      cur = trial;
      return true;
    }
  }
  // e-node s-l-comments — 빈 값
  IseNode(cur);
  if (!IssLComments(cur)) {
    return false;
  }
  return true;
}

bool Grammar::IsnsLCompactMapping(ParseState& cur, const std::ptrdiff_t n) {
  OpenNode(cur, SyntaxKind::kMapping, cur.before.size());
  if (!IsnsLBlockMapEntry(cur, n)) {
    return false;
  }
  while (true) {
    ParseState trial = cur;
    if (!IssIndent(trial, n)) {
      break;
    }
    if (!IsnsLBlockMapEntry(trial, n)) {
      break;
    }
    cur = trial;
  }
  CloseNode(cur, cur.before.size());
  return true;
}

bool Grammar::State::IssLBlockNode(ParseState& cur,
                                   const std::ptrdiff_t n) const {
  {
    ParseState trial = cur;
    if (IssLBlockInBlock(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IssLFlowInBlock(cur, n);
}

bool Grammar::IssLFlowInBlock(ParseState& cur, const std::ptrdiff_t n) {
  if (!flow_out_state.IssSeparate(cur, n + 1)) {
    return false;
  }
  if (!flow_out_state.IsnsFlowNode(cur, n + 1)) {
    return false;
  }
  return IssLComments(cur);
}

bool Grammar::State::IssLBlockInBlock(ParseState& cur,
                                      const std::ptrdiff_t n) const {
  {
    ParseState trial = cur;
    if (IssLBlockScalar(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IssLBlockCollection(cur, n);
}

bool Grammar::State::IssLBlockScalar(ParseState& cur,
                                     const std::ptrdiff_t n) const {
  if (!IssSeparate(cur, n + 1)) {
    return false;
  }
  // (c-ns-properties(n+1,c) s-separate(n+1,c))? — 그룹 성공 후 본문이
  // 실패하면 그룹 없이 재시도
  {
    ParseState with_properties = cur;
    if (IscNsProperties(with_properties, n + 1) &&
        IssSeparate(with_properties, n + 1)) {
      {
        ParseState trial = with_properties;
        if (IscLLiteral(trial, n)) {
          cur = trial;
          return true;
        }
      }
      {
        ParseState trial = with_properties;
        if (IscLFolded(trial, n)) {
          cur = trial;
          return true;
        }
      }
    }
  }
  {
    ParseState trial = cur;
    if (IscLLiteral(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IscLFolded(cur, n);
}

bool Grammar::State::IssLBlockCollection(ParseState& cur,
                                         const std::ptrdiff_t n) const {
  // (s-separate(n+1,c) c-ns-properties(n+1,c))? s-l-comments
  // (seq-space(n,c) | l+block-mapping(n)) — 속성 그룹 성공 후 본문이
  // 실패하면 그룹 없이 재시도
  {
    ParseState with_properties = cur;
    if (IssSeparate(with_properties, n + 1) &&
        IscNsProperties(with_properties, n + 1) &&
        IssLComments(with_properties)) {
      {
        ParseState trial = with_properties;
        if (IsseqSpace(trial, n)) {
          cur = trial;
          return true;
        }
      }
      {
        ParseState trial = with_properties;
        if (IslBlockMapping(trial, n)) {
          cur = trial;
          return true;
        }
      }
    }
  }
  if (!IssLComments(cur)) {
    return false;
  }
  {
    ParseState trial = cur;
    if (IsseqSpace(trial, n)) {
      cur = trial;
      return true;
    }
  }
  return IslBlockMapping(cur, n);
}

bool Grammar::BlockOutState::IsseqSpace(ParseState& cur,
                                        const std::ptrdiff_t n) const {
  return IslBlockSequence(cur, n - 1);
}

bool Grammar::BlockInState::IsseqSpace(ParseState& cur,
                                       const std::ptrdiff_t n) const {
  return IslBlockSequence(cur, n);
}

bool Grammar::IslDocumentPrefix(ParseState& cur) {
  if (At(cur.cps, 0) == kCByteOrderMark) {
    Advance(cur, 1);
  }
  while (true) {
    ParseState trial = cur;
    if (!IslComment(trial)) {
      break;
    }
    // l-commant가 없었지만 true, 0칸 전진한 경우
    if (trial.before.size() == cur.before.size()) {
      break;
    }
    cur = trial;
  }
  return true;  // 빈 매치 가능
}

bool Grammar::IscDirectivesEnd(ParseState& cur) {
  if (At(cur.cps, 0) == '-' && At(cur.cps, 1) == '-' && At(cur.cps, 2) == '-') {
    Advance(cur, 3);
    return true;
  }
  return false;
}

bool Grammar::IscDocumentEnd(ParseState& cur) {
  if (At(cur.cps, 0) != '.' || At(cur.cps, 1) != '.' || At(cur.cps, 2) != '.') {
    return false;
  }
  // 스펙 주석: (not followed by non-ws char)
  const std::uint32_t predicate = At(cur.cps, 3);
  if (!AtEnd(cur.cps, 3) && (IssWhite(predicate) == 0U) &&
      (IsbChar(predicate) == 0U)) {
    return false;
  }
  Advance(cur, 3);
  return true;
}

bool Grammar::IslDocumentSuffix(ParseState& cur) {
  if (!IscDocumentEnd(cur)) {
    return false;
  }
  return IssLComments(cur);
}

bool Grammar::IscForbidden(ParseState& cur) {
  if (!AtLineStart(cur)) {
    return false;
  }

  ParseState marker = cur;
  if (!IscDirectivesEnd(marker)) {
    marker = cur;
    if (!IscDocumentEnd(marker)) {
      return false;
    }
  }

  if (AtEnd(marker.cps, 0)) {
    cur = marker;
    return true;
  }
  if ((IsbChar(At(marker.cps, 0)) == 0U) &&
      (IssWhite(At(marker.cps, 0)) == 0U)) {
    return false;
  }
  Advance(marker, 1);
  cur = marker;
  return true;
}

bool Grammar::IslBareDocument(ParseState& cur) {
  ParseState forbidden = cur;
  if (IscForbidden(forbidden)) {
    return false;
  }
  return block_in_state.IssLBlockNode(cur, -1);
}

bool Grammar::IslExplicitDocument(ParseState& cur) {
  if (!IscDirectivesEnd(cur)) {
    return false;
  }
  {
    ParseState trial = cur;
    if (IslBareDocument(trial)) {
      cur = trial;
      return true;
    }
  }
  // e-node s-l-comments — 내용 없는 문서
  IseNode(cur);
  if (!IssLComments(cur)) {
    return false;
  }
  return true;
}

bool Grammar::IslDirectiveDocument(ParseState& cur) {
  std::size_t count = 0;
  while (true) {
    ParseState trial = cur;
    if (!IslDirective(trial)) {
      break;
    }
    cur = trial;
    count++;  // l-directive는 항상 '%'를 소비하므로 진행 보장
  }
  if (count == 0U) {
    return false;  // l-directive+ — 1회 이상
  }
  return IslExplicitDocument(cur);
}

bool Grammar::IslAnyDocument(ParseState& cur) {
  // 문서 구문 트리 경계는 여기서만 만든다(내부 explicit/bare 재사용과 중복
  // 방지).
  OpenNode(cur, SyntaxKind::kDocument, cur.before.size());
  {
    ParseState trial = cur;
    if (IslDirectiveDocument(trial)) {
      CloseNode(trial, trial.before.size());
      cur = trial;
      return true;
    }
  }
  {
    ParseState trial = cur;
    if (IslExplicitDocument(trial)) {
      CloseNode(trial, trial.before.size());
      cur = trial;
      return true;
    }
  }
  if (!IslBareDocument(cur)) {
    return false;
  }
  CloseNode(cur, cur.before.size());
  return true;
}

bool Grammar::IslYamlStream(ParseState& cur) {
  while (true) {  // l-document-prefix* (빈 매치 시 종료)
    ParseState trial = cur;
    IslDocumentPrefix(trial);
    if (trial.before.size() == cur.before.size()) {
      break;
    }
    cur = trial;
  }
  {
    ParseState trial = cur;
    if (IslAnyDocument(trial)) {
      cur = trial;
    }
  }
  while (true) {
    // (l-document-suffix+ l-document-prefix* l-any-document?)
    // | c-byte-order-mark | l-comment | l-explicit-document
    {
      ParseState suffixes = cur;
      if (IslDocumentSuffix(suffixes)) {
        while (true) {
          ParseState trial = suffixes;
          if (!IslDocumentSuffix(trial)) {
            break;  // suffix는 항상 "..."를 소비하므로 진행 보장
          }
          suffixes = trial;
        }
        while (true) {
          ParseState trial = suffixes;
          IslDocumentPrefix(trial);
          if (trial.before.size() == suffixes.before.size()) {
            break;
          }
          suffixes = trial;
        }
        {
          ParseState trial = suffixes;
          if (IslAnyDocument(trial)) {
            suffixes = trial;
          }
        }
        cur = suffixes;
        continue;
      }
    }
    if (At(cur.cps, 0) == kCByteOrderMark) {
      Advance(cur, 1);
      continue;
    }
    {
      ParseState trial = cur;
      if (IslComment(trial) && trial.before.size() > cur.before.size()) {
        cur = trial;
        continue;
      }
    }
    {
      ParseState trial = cur;
      OpenNode(trial, SyntaxKind::kDocument, trial.before.size());
      if (IslExplicitDocument(trial) &&
          trial.before.size() > cur.before.size()) {
        CloseNode(trial, trial.before.size());
        cur = trial;
        continue;
      }
    }
    break;
  }
  return true;
}

}  // namespace bedrock::archive::yaml
