/**
 * @file
 * @brief YAML 1.2.2 문법 인식기 정의.
 *
 * 두 세계로 나뉜다. [1]~[62]는 순수 문자/토큰 분류기 — 단일 코드포인트를
 * 받는 것은 cp(0=불일치)를, 여러 코드포인트를 보는 것은 남은 입력 span
 * (cps) 하나만 받아 매치 길이(0=불일치)를 반환하고, 위치를 소비하지
 * 않는다. 길이 부족은 span 크기 검사로 처리한다.
 *
 * [63]~[211]은 매처 — Cursor&(cur)를 받아 성공하면 cur를 매치 끝으로
 * 전진시키고 true를, 실패하면 false를 반환한다. 실패 시 cur는 임의
 * 지점까지 전진해 있을 수 있으므로, 백트래킹이 필요한 호출자가 호출
 * 전에 Cursor를 복사해 두었다가 실패 시 되돌린다. 빈 매치는 cur를
 * 전진시키지 않고 true를 반환한다.
 *
 * c-forbidden([206]) 배제: 베어 문서의 내용 행이 "---"/"..." 문서
 * 마커로 시작하지 못하게 하는 규칙([207])은 별도 프로덕션으로 두지
 * 않고, plain([126][134]), quoted([115][124]), 블록 스칼라([171]
 * [175]) 등 각 내용 프로덕션의 행 시작 지점에서 개별적으로 검사해
 * 구현한다.
 *
 * [139] ns-flow-seq-entry는 스펙 그대로(pair | node) 구현하면 pair
 * 시도가 서브트리 전체를 파싱한 뒤 실패하고 node가 같은 서브트리를
 * 다시 파싱하게 되어, 중첩 시퀀스에서 깊이에 대해 2^depth 지수
 * 시간이 된다. 언어 동치 변환으로 node를 한 번만 파싱한 뒤 그 끝
 * 바로 뒤에 ':'가 따라올 때만 pair로 재해석하여 이를 회피한다.
 */
#include "common/archive/yaml/grammar.h"

#include <algorithm>

namespace bedrock::archive::yaml {
// State 다형성 앵커(key function) — vtable을 이 TU에 고정, weak-vtable 방지
Grammar::State::~State() = default;

// 컨텍스트 싱글턴 정의 (상태 없음 — vptr만 있으므로 상수 초기화 가능)
constinit Grammar::BLOCK_IN_state Grammar::_block_in_state;
constinit Grammar::BLOCK_OUT_state Grammar::_block_out_state;
constinit Grammar::BLOCK_KEY_state Grammar::_block_key_state;
constinit Grammar::FLOW_IN_state Grammar::_flow_in_state;
constinit Grammar::FLOW_OUT_state Grammar::_flow_out_state;
constinit Grammar::FLOW_KEY_state Grammar::_flow_key_state;

// chomping 상태 다형성 앵커(key function)
Grammar::ChompingState::~ChompingState() = default;
Grammar::CLIP_state::~CLIP_state() = default;

constinit Grammar::STRIP_state Grammar::_strip_state;
constinit Grammar::CLIP_state Grammar::_clip_state;
constinit Grammar::KEEP_state Grammar::_keep_state;

// ── 헬퍼 ──────────────────────────────────────────────────────────
void Grammar::Advance(Cursor& cur, std::size_t k) {
  cur.before = std::span<const std::uint32_t>(cur.before.data(),
                                              cur.before.size() + k);
  cur.cps = cur.cps.subspan(k);
  if (cur.diag && cur.before.size() > cur.diag->furthest) {
    cur.diag->furthest = cur.before.size();
  }
}
void Grammar::Emit(Cursor& cur, const Event& e) {
  if (!cur.events) {
    return;
  }
  // event_len까지 잘라 백트래킹으로 죽은 분기의 이벤트를 폐기한 뒤 추가
  cur.events->list.resize(cur.event_len);
  cur.events->list.push_back(e);
  cur.event_len = cur.events->list.size();
}
void Grammar::EmitEmpty(Cursor& cur) {
  Emit(cur, {.kind = EventKind::kScalar,
             .begin = cur.before.size(),
             .end = cur.before.size()});
}
ChompKind Grammar::ChompKindOf(const ChompingState* t) {
  if (t == &_strip_state) {
    return ChompKind::kStrip;
  }
  if (t == &_keep_state) {
    return ChompKind::kKeep;
  }
  return ChompKind::kClip;
}
LineCol OffsetToLineCol(std::span<const std::uint32_t> buf,
                        std::size_t offset) {
  LineCol lc{1, 1};
  const std::size_t end = offset < buf.size() ? offset : buf.size();
  for (std::size_t i = 0; i < end; i++) {
    const std::uint32_t c = buf[i];
    if (c == '\n') {
      lc.line++;
      lc.col = 1;
    } else if (c == '\r') {
      if (i + 1 < end && buf[i + 1] == '\n') {
        continue;  // CRLF의 중간 — LF가 줄을 넘긴다
      }
      lc.line++;
      lc.col = 1;
    } else {
      lc.col++;
    }
  }
  return lc;
}
std::uint32_t Grammar::At(std::span<const std::uint32_t> cps, std::size_t k) {
  return k < cps.size() ? cps[k] : 0;
}
bool Grammar::AtEnd(std::span<const std::uint32_t> cps, std::size_t k) {
  return k >= cps.size();
}
bool Grammar::AtLineStart(const Cursor& cur) {
  // BOM은 줄 내용이 아님([202][211]에서 문서 경계에만 등장) — 건너뛰고 판정
  std::span<const std::uint32_t> b = cur.before;
  while (!b.empty() && b.back() == c_byte_order_mark) {
    b = b.first(b.size() - 1);
  }
  if (b.empty()) {
    return true;
  }
  const std::uint32_t prev = b.back();
  if (prev == b_line_feed) {
    return true;
  }
  if (prev == b_carrige_return) {
    // CR 바로 뒤가 LF면 CRLF의 중간이므로 아직 줄이 끝나지 않음
    return At(cur.cps, 0) != b_line_feed;
  }
  return false;
}
std::size_t Grammar::LeadingSpaces(std::span<const std::uint32_t> cps) {
  std::size_t i = 0;
  for (; i < cps.size(); i++) {
    if (cps[i] != s_space) {
      break;
    }
  }
  return i;
}
std::ptrdiff_t Grammar::DetectScalarIndentation(std::span<const std::uint32_t> cps,
                                             const std::ptrdiff_t n) {
  // 8.1.1.1: 첫 비어있지 않은 행의 들여쓰기 w로 m = w - n (m ≥ 1).
  // 선행 빈 행이 그보다 더 들여쓰여 있으면 에러(0 반환). 내용 행이 없으면
  // 가장 긴 빈 행의 공백 수 기준.
  std::size_t k = 0;
  std::size_t max_empty = 0;
  while (true) {
    const std::size_t w = LeadingSpaces(cps.subspan(k));
    const std::size_t p = k + w;
    if (AtEnd(cps, p)) {
      const std::ptrdiff_t m = static_cast<std::ptrdiff_t>(max_empty) - n;
      return m < 1 ? 1 : m;
    }
    if (const std::size_t b = Isb_break(cps.subspan(p))) {
      // 공백만 있는 빈 행
      if (w > max_empty) {
        max_empty = w;
      }
      k = p + b;
      continue;
    }
    // 첫 내용 행
    if (max_empty > w) {
      return 0;  // 선행 빈 행이 내용보다 더 들여쓰여짐 → 에러
    }
    const std::ptrdiff_t m = static_cast<std::ptrdiff_t>(w) - n;
    return m < 1 ? 1 : m;
  }
}
std::uint32_t Grammar::Isc_printable(const std::uint32_t cp) {
  switch (cp) {
    case '\t':
    case '\n':
    case '\r':
    case 0x85:
      return cp;
    default: {
      if (0x20 <= cp && cp <= 0x7E) {
        return cp;
      }
      if (0xA0 <= cp && cp <= 0xD7FF) {
        return cp;
      }
      if (0xE000 <= cp && cp <= 0xFFFD) {
        return cp;
      }
      if (0x010000 <= cp && cp <= 0x10FFFF) {
        return cp;
      }
      break;
    }
  }
  return 0;
}
std::uint32_t Grammar::Isnb_json(const std::uint32_t cp) {
  switch (cp) {
    case '\t':
      return cp;
    default: {
      if (0x20 <= cp && cp <= 0x10FFFF) {
        return cp;
      }
      break;
    }
  }
  return 0;
}
std::uint32_t Grammar::Isc_reserved(const std::uint32_t cp) {
  switch (cp) {
    case '@':
    case '`':
      return cp;
    default:
      break;
  }
  return 0;
}
std::uint32_t Grammar::Isc_indicator(const std::uint32_t cp) {
  switch (cp) {
    case c_sequence_entry:
    case c_mapping_key:
    case c_mapping_value:
    case c_collect_entry:
    case c_sequence_start:
    case c_sequence_end:
    case c_mapping_start:
    case c_mapping_end:
    case c_comment:
    case c_anchor:
    case c_alias:
    case c_tag:
    case c_literal:
    case c_folded:
    case c_single_quote:
    case c_double_quote:
    case c_directive:
      return cp;
    default: {
      if (Isc_reserved(cp)) {
        return cp;
      }
      break;
    }
  }
  return 0;
}
std::uint32_t Grammar::Isc_flow_indicator(const std::uint32_t cp) {
  switch (cp) {
    case c_collect_entry:
    case c_sequence_start:
    case c_sequence_end:
    case c_mapping_start:
    case c_mapping_end:
      return cp;
    default:
      break;
  }
  return 0;
}
std::uint32_t Grammar::Isb_char(const std::uint32_t cp) {
  switch (cp) {
    case b_line_feed:
    case b_carrige_return:
      return cp;
    default:
      break;
  }
  return 0;
}
std::uint32_t Grammar::Isnb_char(const std::uint32_t cp) {
  switch (cp) {
    case c_byte_order_mark:
      return 0;
    default: {
      if (Isb_char(cp)) {
        return 0;
      }
      if (!Isc_printable(cp)) {
        return 0;
      }
      return cp;
    }
  }
}
std::size_t Grammar::Isb_break(std::span<const std::uint32_t> cps) {
  if (cps.empty()) {
    return 0;
  }
  switch (cps[0]) {
    case b_carrige_return:
      return (cps.size() >= 2 && cps[1] == b_line_feed) ? 2 : 1;
    case b_line_feed:
      return 1;
    default:
      break;
  }
  return 0;
}
std::size_t Grammar::Isb_as_line_feed(std::span<const std::uint32_t> cps) {
  return Isb_break(cps);
}
std::size_t Grammar::Isb_non_content(std::span<const std::uint32_t> cps) {
  return Isb_break(cps);
}
std::uint32_t Grammar::Iss_white(const std::uint32_t cp) {
  switch (cp) {
    case s_space:
    case s_tab:
      return cp;
    default:
      break;
  }
  return 0;
}
std::uint32_t Grammar::Isns_char(const std::uint32_t cp) {
  if (!Isnb_char(cp)) {
    return 0;
  }
  if (Iss_white(cp)) {
    return 0;
  }
  return cp;
}
std::uint32_t Grammar::Isns_dec_digit(const std::uint32_t cp) {
  if ('0' <= cp && cp <= '9') {
    return cp;
  }
  return 0;
}
std::uint32_t Grammar::Isns_hex_digit(const std::uint32_t cp) {
  if (Isns_dec_digit(cp)) {
    return cp;
  }
  if ('A' <= cp && cp <= 'F') {
    return cp;
  }
  if ('a' <= cp && cp <= 'f') {
    return cp;
  }
  return 0;
}
std::uint32_t Grammar::Isns_ascii_letter(const std::uint32_t cp) {
  if ('A' <= cp && cp <= 'Z') {
    return cp;
  }
  if ('a' <= cp && cp <= 'z') {
    return cp;
  }
  return 0;
}
std::uint32_t Grammar::Isns_word_char(const std::uint32_t cp) {
  switch (cp) {
    case '-':
      return cp;
    default: {
      if (Isns_dec_digit(cp)) {
        return cp;
      }
      if (Isns_ascii_letter(cp)) {
        return cp;
      }
      return 0;
    }
  }
}
std::size_t Grammar::Isns_uri_char(std::span<const std::uint32_t> cps) {
  if (cps.empty()) {
    return 0;
  }
  if (cps[0] == '%') {
    if (cps.size() >= 3 && Isns_hex_digit(cps[1]) && Isns_hex_digit(cps[2])) {
      return 3;
    }
    return 0;
  }
  if (Isns_word_char(cps[0])) {
    return 1;
  }
  switch (cps[0]) {
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
std::size_t Grammar::Isns_tag_char(std::span<const std::uint32_t> cps) {
  const std::size_t ns_uri_char = Isns_uri_char(cps);
  if (!ns_uri_char) {
    return 0;
  }
  switch (cps[0]) {
    case c_tag:
      return 0;
    default: {
      if (Isc_flow_indicator(cps[0])) {
        return 0;
      }
      break;
    }
  }
  return ns_uri_char;
}
std::uint32_t Grammar::Isns_esc_horizontal_tab(const std::uint32_t cp) {
  switch (cp) {
    case 't':
    case '\t':
      return cp;
    default:
      break;
  }
  return 0;
}
std::size_t Grammar::Isns_esc_8_bit(std::span<const std::uint32_t> cps) {
  if (cps.empty() || cps[0] != 'x') {
    return 0;
  }
  if (cps.size() >= 3 && Isns_hex_digit(cps[1]) && Isns_hex_digit(cps[2])) {
    return 3;
  }
  return 0;
}
std::size_t Grammar::Isns_esc_16_bit(std::span<const std::uint32_t> cps) {
  if (cps.empty() || cps[0] != 'u') {
    return 0;
  }
  if (cps.size() >= 5 && Isns_hex_digit(cps[1]) && Isns_hex_digit(cps[2]) &&
      Isns_hex_digit(cps[3]) && Isns_hex_digit(cps[4])) {
    return 5;
  }
  return 0;
}
std::size_t Grammar::Isns_esc_32_bit(std::span<const std::uint32_t> cps) {
  if (cps.empty() || cps[0] != 'U') {
    return 0;
  }
  if (cps.size() >= 9 && Isns_hex_digit(cps[1]) && Isns_hex_digit(cps[2]) &&
      Isns_hex_digit(cps[3]) && Isns_hex_digit(cps[4]) &&
      Isns_hex_digit(cps[5]) && Isns_hex_digit(cps[6]) &&
      Isns_hex_digit(cps[7]) && Isns_hex_digit(cps[8])) {
    return 9;
  }
  return 0;
}
std::size_t Grammar::Isc_ns_esc_char(std::span<const std::uint32_t> cps) {
  if (cps.empty() || cps[0] != c_escape) {
    return 0;
  }
  const std::uint32_t c1 = At(cps, 1);
  switch (c1) {
    case ns_esc_null:
    case ns_esc_bell:
    case ns_esc_backspace:
    case ns_esc_line_feed:
    case ns_esc_vertical_tab:
    case ns_esc_form_feed:
    case ns_esc_carriage_return:
    case ns_esc_escape:
    case ns_esc_space:
    case ns_esc_double_quote:
    case ns_esc_slash:
    case ns_esc_backslash:
    case ns_esc_next_line:
    case ns_esc_non_breaking_space:
    case ns_esc_line_separator:
    case ns_esc_paragraph_separator:
      return 2;
    default: {
      if (Isns_esc_horizontal_tab(c1)) {
        return 2;
      }
      const std::span<const std::uint32_t> rest = cps.subspan(1);
      if (const std::size_t e = Isns_esc_8_bit(rest)) {
        return e + 1;
      }
      if (const std::size_t e = Isns_esc_16_bit(rest)) {
        return e + 1;
      }
      if (const std::size_t e = Isns_esc_32_bit(rest)) {
        return e + 1;
      }
      break;
    }
  }
  return 0;
}
bool Grammar::Iss_indent(Cursor& cur, const std::ptrdiff_t n) {
  if (n <= 0) {
    return true;  // s-indent(0) = 빈 매치 (음수 n도 관례상 빈 매치로 취급)
  }
  const std::size_t un = static_cast<std::size_t>(n);
  if (cur.cps.size() < un) {
    return false;
  }
  for (std::size_t k = 0; k < un; k++) {
    if (cur.cps[k] != s_space) {
      return false;
    }
  }
  Advance(cur, un);
  return true;
}
bool Grammar::Iss_indent_less_than(Cursor& cur, const std::ptrdiff_t n) {
  if (n <= 0) {
    return false;
  }
  const std::size_t m = LeadingSpaces(cur.cps);
  if (static_cast<std::ptrdiff_t>(m) < n) {
    Advance(cur, m);
    return true;
  }
  return false;
}
bool Grammar::Iss_indent_less_or_equal(Cursor& cur, const std::ptrdiff_t n) {
  if (n < 0) {
    return false;
  }
  const std::size_t m = LeadingSpaces(cur.cps);
  if (static_cast<std::ptrdiff_t>(m) <= n) {
    Advance(cur, m);
    return true;
  }
  return false;
}
bool Grammar::Iss_separate_in_line(Cursor& cur) {
  std::size_t w = 0;
  while (w < cur.cps.size() && Iss_white(cur.cps[w])) {
    w++;
  }
  if (w) {
    Advance(cur, w);
    return true;
  }
  return AtLineStart(cur);  // s-white가 없으면 <start-of-line>에서만 빈 매치
}
bool Grammar::Iss_block_line_prefix(Cursor& cur, const std::ptrdiff_t n) {
  return Iss_indent(cur, n);
}
bool Grammar::Iss_flow_line_prefix(Cursor& cur, const std::ptrdiff_t n) {
  if (n < 0) {
    return false;
  }
  if (!Iss_indent(cur, n)) {
    return false;  // s-indent(n) 실패
  }
  // n칸 이후 나머지에서 추가 공백/탭 흡수(s-separate-in-line, 빈 매치 허용)
  std::size_t w = 0;
  while (w < cur.cps.size() && Iss_white(cur.cps[w])) {
    w++;
  }
  Advance(cur, w);
  return true;
}
bool Grammar::BLOCK_IN_state::Iss_line_prefix(Cursor& cur,
                                           const std::ptrdiff_t n) const {
  return Iss_block_line_prefix(cur, n);
}
bool Grammar::BLOCK_OUT_state::Iss_line_prefix(Cursor& cur,
                                            const std::ptrdiff_t n) const {
  return Iss_block_line_prefix(cur, n);
}
bool Grammar::FLOW_IN_state::Iss_line_prefix(Cursor& cur,
                                          const std::ptrdiff_t n) const {
  return Iss_flow_line_prefix(cur, n);
}
bool Grammar::FLOW_OUT_state::Iss_line_prefix(Cursor& cur,
                                           const std::ptrdiff_t n) const {
  return Iss_flow_line_prefix(cur, n);
}
bool Grammar::State::Isl_empty(Cursor& cur, const std::ptrdiff_t n) const {
  if (n < 0) {
    return false;
  }
  const Cursor start = cur;
  // 1) 대안 선택 (하나만): line-prefix 우선, 실패(또는 n>0에서 빈 소비)하면
  //    indent-less-than
  const bool took = Iss_line_prefix(cur, n);
  if (n > 0 && (!took || cur.before.size() == start.before.size())) {
    cur = start;
    const std::size_t m = LeadingSpaces(cur.cps);
    if (static_cast<std::ptrdiff_t>(m) < n) {
      Advance(cur, m);
    }
    // m>=n이면 전진하지 않음 → 아래 b-as-line-feed가 공백에서 실패
  }
  // 2) b-as-line-feed
  const std::size_t b = Isb_as_line_feed(cur.cps);
  if (b == 0) {
    return false;  // 줄바꿈 없음 → l-empty 실패
  }
  Advance(cur, b);
  return true;
}
bool Grammar::State::Isb_l_trimmed(Cursor& cur, const std::ptrdiff_t n) const {
  const std::size_t b = Isb_non_content(cur.cps);
  if (!b) {
    return false;
  }
  Advance(cur, b);
  std::size_t count = 0;
  while (true) {
    const Cursor cp = cur;
    if (!Isl_empty(cur, n)) {
      cur = cp;
      break;
    }
    count++;  // l-empty는 항상 1자 이상 소비
  }
  return count != 0;  // l-empty(n,c)+ — 1회 이상 필요
}
std::size_t Grammar::Isb_as_space(std::span<const std::uint32_t> cps) {
  return Isb_break(cps);
}
bool Grammar::State::Isb_l_folded(Cursor& cur, const std::ptrdiff_t n) const {
  {
    const Cursor cp = cur;
    if (Isb_l_trimmed(cur, n)) {
      return true;
    }
    cur = cp;
  }
  const std::size_t b = Isb_as_space(cur.cps);
  if (b) {
    Advance(cur, b);
    return true;
  }
  return false;
}
bool Grammar::Iss_flow_folded(Cursor& cur, const std::ptrdiff_t n) {
  {
    const Cursor cp = cur;
    if (!Iss_separate_in_line(cur)) {
      cur = cp;
    }
  }
  if (!_flow_in_state.Isb_l_folded(cur, n)) {
    return false;
  }
  return Iss_flow_line_prefix(cur, n);
}
bool Grammar::Isc_nb_comment_text(Cursor& cur) {
  if (At(cur.cps, 0) != c_comment) {
    return false;
  }
  std::size_t k = 1;
  while (Isnb_char(At(cur.cps, k))) {
    k++;
  }
  Advance(cur, k);
  return true;
}
bool Grammar::Isb_comment(Cursor& cur) {
  if (AtEnd(cur.cps, 0)) {
    return true;  // <end-of-input>
  }
  const std::size_t b = Isb_non_content(cur.cps);
  if (b) {
    Advance(cur, b);
    return true;
  }
  return false;
}
bool Grammar::Iss_b_comment(Cursor& cur) {
  // (s-separate-in-line c-nb-comment-text?)? b-comment
  // 그룹을 소비한 뒤 b-comment가 실패하면 그룹 없이 재시도
  const Cursor cp = cur;
  if (Iss_separate_in_line(cur)) {
    {
      const Cursor cp2 = cur;
      if (!Isc_nb_comment_text(cur)) {
        cur = cp2;
      }
    }
    if (Isb_comment(cur)) {
      return true;
    }
  }
  cur = cp;
  return Isb_comment(cur);
}
bool Grammar::Isl_comment(Cursor& cur) {
  if (!Iss_separate_in_line(cur)) {
    return false;
  }
  {
    const Cursor cp = cur;
    if (!Isc_nb_comment_text(cur)) {
      cur = cp;
    }
  }
  return Isb_comment(cur);
}
bool Grammar::Iss_l_comments(Cursor& cur) {
  {
    const Cursor cp = cur;
    if (!Iss_b_comment(cur)) {
      cur = cp;
      if (!AtLineStart(cur)) {
        return false;
      }
      // <start-of-line> 빈 매치
    }
  }
  while (true) {
    const Cursor cp = cur;
    if (!Isl_comment(cur)) {
      cur = cp;
      break;
    }
    if (cur.before.size() == cp.before.size()) {
      cur = cp;
      break;  // l-comment는 sol+EOF에서 빈 매치가 가능하므로 진행 없으면 종료
    }
  }
  return true;
}
bool Grammar::State::Iss_separate(Cursor& cur, const std::ptrdiff_t n) const {
  return Iss_separate_lines(cur, n);
}
bool Grammar::BLOCK_KEY_state::Iss_separate(Cursor& cur,
                                         const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return Iss_separate_in_line(cur);
}
bool Grammar::FLOW_KEY_state::Iss_separate(Cursor& cur,
                                        const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return Iss_separate_in_line(cur);
}
bool Grammar::Iss_separate_lines(Cursor& cur, const std::ptrdiff_t n) {
  {
    const Cursor cp = cur;
    if (Iss_l_comments(cur) && Iss_flow_line_prefix(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Iss_separate_in_line(cur);
}
bool Grammar::Isl_directive(Cursor& cur) {
  if (At(cur.cps, 0) != c_directive) {
    return false;
  }
  Advance(cur, 1);
  // 각 대안은 뒤따르는 s-l-comments까지 성공해야 함 (형식이 어긋난
  // YAML/TAG 지시자는 ns-reserved-directive로 백트래킹됨 — 순수 문법 인식.
  // 알려진 이름의 형식 검증은 의미론 단계의 몫)
  {
    const Cursor cp = cur;
    if (Isns_yaml_directive(cur) && Iss_l_comments(cur)) {
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isns_tag_directive(cur) && Iss_l_comments(cur)) {
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isns_reserved_directive(cur) && Iss_l_comments(cur)) {
      return true;
    }
    cur = cp;
  }
  return false;
}
bool Grammar::Isns_reserved_directive(Cursor& cur) {
  if (!Isns_directive_name(cur)) {
    return false;
  }
  while (true) {
    const Cursor cp = cur;
    if (!Iss_separate_in_line(cur)) {
      cur = cp;
      break;
    }
    if (cur.before.size() == cp.before.size()) {
      cur = cp;
      break;  // 진행 없는 분리
    }
    if (!Isns_directive_parameter(cur)) {
      cur = cp;  // 분리는 파라미터가 따라올 때만 소비
      break;
    }
  }
  return true;
}
bool Grammar::Isns_directive_name(Cursor& cur) {
  std::size_t k = 0;
  while (Isns_char(At(cur.cps, k))) {
    k++;
  }
  if (k == 0) {
    return false;
  }
  Advance(cur, k);
  return true;
}
bool Grammar::Isns_directive_parameter(Cursor& cur) {
  std::size_t k = 0;
  while (Isns_char(At(cur.cps, k))) {
    k++;
  }
  if (k == 0) {
    return false;
  }
  Advance(cur, k);
  return true;
}
bool Grammar::Isns_yaml_directive(Cursor& cur) {
  if (At(cur.cps, 0) != 'Y' || At(cur.cps, 1) != 'A' || At(cur.cps, 2) != 'M' ||
      At(cur.cps, 3) != 'L') {
    return false;
  }
  Advance(cur, 4);
  if (!Iss_separate_in_line(cur)) {
    return false;
  }
  return Isns_yaml_version(cur);
}
bool Grammar::Isns_yaml_version(Cursor& cur) {
  std::size_t k = 0;
  while (Isns_dec_digit(At(cur.cps, k))) {
    k++;
  }
  if (k == 0) {
    return false;
  }
  if (At(cur.cps, k) != '.') {
    return false;
  }
  std::size_t k2 = k + 1;
  const std::size_t start2 = k2;
  while (Isns_dec_digit(At(cur.cps, k2))) {
    k2++;
  }
  if (k2 == start2) {
    return false;
  }
  Advance(cur, k2);
  return true;
}
bool Grammar::Isns_tag_directive(Cursor& cur) {
  if (At(cur.cps, 0) != 'T' || At(cur.cps, 1) != 'A' || At(cur.cps, 2) != 'G') {
    return false;
  }
  Advance(cur, 3);
  if (!Iss_separate_in_line(cur)) {
    return false;
  }
  if (!Isc_tag_handle(cur)) {
    return false;
  }
  if (!Iss_separate_in_line(cur)) {
    return false;
  }
  return Isns_tag_prefix(cur);
}

bool Grammar::Isc_tag_handle(Cursor& cur) {
  // named("!x!") → secondary("!!") → primary("!") 순서가 중요
  {
    const Cursor cp = cur;
    if (Isc_named_tag_handle(cur)) {
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isc_secondary_tag_handle(cur)) {
      return true;
    }
    cur = cp;
  }
  return Isc_primary_tag_handle(cur);
}

bool Grammar::Isc_primary_tag_handle(Cursor& cur) {
  if (At(cur.cps, 0) != c_tag) {
    return false;
  }
  Advance(cur, 1);
  return true;
}

bool Grammar::Isc_secondary_tag_handle(Cursor& cur) {
  if (At(cur.cps, 0) != c_tag || At(cur.cps, 1) != c_tag) {
    return false;
  }
  Advance(cur, 2);
  return true;
}

bool Grammar::Isc_named_tag_handle(Cursor& cur) {
  if (At(cur.cps, 0) != c_tag) {
    return false;
  }
  std::size_t k = 1;
  while (Isns_word_char(At(cur.cps, k))) {
    k++;
  }
  if (k == 1) {
    return false;  // ns-word-char+ — 1자 이상
  }
  if (At(cur.cps, k) != c_tag) {
    return false;
  }
  Advance(cur, k + 1);
  return true;
}

bool Grammar::Isns_tag_prefix(Cursor& cur) {
  {
    const Cursor cp = cur;
    if (Isc_ns_local_tag_prefix(cur)) {
      return true;
    }
    cur = cp;
  }
  return Isns_global_tag_prefix(cur);
}

bool Grammar::Isc_ns_local_tag_prefix(Cursor& cur) {
  if (At(cur.cps, 0) != c_tag) {
    return false;
  }
  Advance(cur, 1);
  while (const std::size_t u = Isns_uri_char(cur.cps)) {
    Advance(cur, u);
  }
  return true;
}

bool Grammar::Isns_global_tag_prefix(Cursor& cur) {
  const std::size_t t = Isns_tag_char(cur.cps);
  if (!t) {
    return false;
  }
  Advance(cur, t);
  while (const std::size_t u = Isns_uri_char(cur.cps)) {
    Advance(cur, u);
  }
  return true;
}

bool Grammar::State::Isc_ns_properties(Cursor& cur, const std::ptrdiff_t n) const {
  // (tag (sep anchor)?) | (anchor (sep tag)?) — 뒤쪽 선택 그룹은
  // 분리까지 성공하고 속성이 실패하면 그룹 없이 성립
  if (Isc_ns_tag_property(cur)) {
    const Cursor cp = cur;
    if (Iss_separate(cur, n) && Isc_ns_anchor_property(cur)) {
      return true;
    }
    cur = cp;
    return true;  // 태그만
  }
  if (Isc_ns_anchor_property(cur)) {
    const Cursor cp = cur;
    if (Iss_separate(cur, n) && Isc_ns_tag_property(cur)) {
      return true;
    }
    cur = cp;
    return true;  // 앵커만
  }
  return false;
}

bool Grammar::Isc_ns_tag_property(Cursor& cur) {
  const std::size_t tb = cur.before.size();
  {
    const Cursor cp = cur;
    if (Isc_verbatim_tag(cur)) {
      Emit(cur, {.kind = EventKind::kTag, .begin = tb, .end = cur.before.size()});
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isc_ns_shorthand_tag(cur)) {
      Emit(cur, {.kind = EventKind::kTag, .begin = tb, .end = cur.before.size()});
      return true;
    }
    cur = cp;
  }
  if (!Isc_non_specific_tag(cur)) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kTag, .begin = tb, .end = cur.before.size()});
  return true;
}

bool Grammar::Isc_verbatim_tag(Cursor& cur) {
  if (At(cur.cps, 0) != c_tag || At(cur.cps, 1) != '<') {
    return false;
  }
  Advance(cur, 2);
  std::size_t count = 0;
  while (const std::size_t u = Isns_uri_char(cur.cps)) {
    Advance(cur, u);
    count++;
  }
  if (!count) {
    return false;
  }
  if (At(cur.cps, 0) != '>') {
    return false;
  }
  Advance(cur, 1);
  return true;
}

bool Grammar::Isc_ns_shorthand_tag(Cursor& cur) {
  if (!Isc_tag_handle(cur)) {
    return false;
  }
  std::size_t count = 0;
  while (const std::size_t t = Isns_tag_char(cur.cps)) {
    Advance(cur, t);
    count++;
  }
  if (!count) {
    return false;
  }
  return true;
}

bool Grammar::Isc_non_specific_tag(Cursor& cur) {
  if (At(cur.cps, 0) != c_tag) {
    return false;
  }
  Advance(cur, 1);
  return true;
}

bool Grammar::Isc_ns_anchor_property(Cursor& cur) {
  if (At(cur.cps, 0) != c_anchor) {
    return false;
  }
  Advance(cur, 1);
  const std::size_t nb = cur.before.size();
  if (!Isns_anchor_name(cur)) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kAnchor, .begin = nb, .end = cur.before.size()});
  return true;
}

std::uint32_t Grammar::Isns_anchor_char(const std::uint32_t cp) {
  if (!Isns_char(cp)) {
    return 0;
  }
  if (Isc_flow_indicator(cp)) {
    return 0;
  }
  return cp;
}

bool Grammar::Isns_anchor_name(Cursor& cur) {
  std::size_t k = 0;
  while (Isns_anchor_char(At(cur.cps, k))) {
    k++;
  }
  if (k == 0) {
    return false;
  }
  Advance(cur, k);
  return true;
}

bool Grammar::Isc_ns_alias_node(Cursor& cur) {
  if (At(cur.cps, 0) != c_alias) {
    return false;
  }
  Advance(cur, 1);
  const std::size_t nb = cur.before.size();
  if (!Isns_anchor_name(cur)) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kAlias, .begin = nb, .end = cur.before.size()});
  return true;
}

bool Grammar::Ise_scalar(Cursor& cur) {
  static_cast<void>(cur);
  return true;  // "" — 빈 매치로 항상 성공
}

bool Grammar::Ise_node(Cursor& cur) {
  return Ise_scalar(cur);
}

std::size_t Grammar::Isnb_double_char(std::span<const std::uint32_t> cps) {
  if (const std::size_t e = Isc_ns_esc_char(cps)) {
    return e;
  }
  const std::uint32_t cp = At(cps, 0);
  if (!Isnb_json(cp)) {
    return 0;
  }
  if (cp == c_escape || cp == c_double_quote) {
    return 0;
  }
  return 1;
}

std::size_t Grammar::Isns_double_char(std::span<const std::uint32_t> cps) {
  const std::size_t len = Isnb_double_char(cps);
  if (!len) {
    return 0;
  }
  // 이스케이프(길이 2+)는 공백을 나타내더라도 ns로 취급
  if (len == 1 && Iss_white(At(cps, 0))) {
    return 0;
  }
  return len;
}

bool Grammar::State::Isc_double_quoted(Cursor& cur, const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != c_double_quote) {
    return false;
  }
  Advance(cur, 1);
  const std::size_t b = cur.before.size();
  if (!Isnb_double_text(cur, n)) {
    return false;
  }
  const std::size_t e = cur.before.size();
  if (At(cur.cps, 0) != c_double_quote) {
    return false;
  }
  Advance(cur, 1);
  Emit(cur, {.kind = EventKind::kScalar,
             .style = ScalarStyle::kDoubleQuoted,
             .begin = b,
             .end = e});
  return true;
}

bool Grammar::FLOW_IN_state::Isnb_double_text(Cursor& cur,
                                           const std::ptrdiff_t n) const {
  return Isnb_double_multi_line(cur, n);
}

bool Grammar::FLOW_OUT_state::Isnb_double_text(Cursor& cur,
                                            const std::ptrdiff_t n) const {
  return Isnb_double_multi_line(cur, n);
}

bool Grammar::BLOCK_KEY_state::Isnb_double_text(Cursor& cur,
                                             const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return Isnb_double_one_line(cur);
}

bool Grammar::FLOW_KEY_state::Isnb_double_text(Cursor& cur,
                                            const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return Isnb_double_one_line(cur);
}

bool Grammar::Isnb_double_one_line(Cursor& cur) {
  while (const std::size_t d = Isnb_double_char(cur.cps)) {
    Advance(cur, d);
  }
  return true;
}

bool Grammar::Iss_double_escaped(Cursor& cur, const std::ptrdiff_t n) {
  std::size_t w = 0;
  while (Iss_white(At(cur.cps, w))) {
    w++;
  }
  if (At(cur.cps, w) != c_escape) {
    return false;
  }
  const std::size_t b = Isb_non_content(cur.cps.subspan(w + 1));
  if (!b) {
    return false;
  }
  Advance(cur, w + 1 + b);
  while (true) {
    const Cursor cp = cur;
    if (!_flow_in_state.Isl_empty(cur, n)) {
      cur = cp;
      break;
    }
  }
  return Iss_flow_line_prefix(cur, n);
}

bool Grammar::Iss_double_break(Cursor& cur, const std::ptrdiff_t n) {
  {
    const Cursor cp = cur;
    if (Iss_double_escaped(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Iss_flow_folded(cur, n);
}

bool Grammar::Isnb_ns_double_in_line(Cursor& cur) {
  while (true) {
    std::size_t k = 0;
    while (Iss_white(At(cur.cps, k))) {
      k++;
    }
    const std::size_t d = Isns_double_char(cur.cps.subspan(k));
    if (!d) {
      break;  // 공백은 ns 문자가 따라올 때만 함께 소비
    }
    Advance(cur, k + d);
  }
  return true;
}

bool Grammar::Iss_double_next_line(Cursor& cur, const std::ptrdiff_t n) {
  // 스펙의 자기 재귀를 반복으로 전개 (행 수에 비례한 스택 소비 방지)
  Cursor work = cur;
  bool have = false;
  Cursor res = cur;
  while (true) {
    const Cursor cp = work;
    if (!Iss_double_break(work, n)) {
      work = cp;
      break;  // 다음 재귀 단계 실패 → 직전 s-white* 폴백이 결과
    }
    // 베어 문서의 내용 행은 문서 마커로 시작할 수 없음 ([206][207])
    if (AtLineStart(work) && Isc_forbidden(work)) {
      break;
    }
    const std::size_t d = Isns_double_char(work.cps);
    if (!d) {
      res = work;  // 선택 그룹 생략 — 줄바꿈까지만 소비하고 종료
      have = true;
      break;
    }
    Advance(work, d);
    Isnb_ns_double_in_line(work);
    Cursor afterw = work;
    std::size_t wsp = 0;
    while (Iss_white(At(afterw.cps, wsp))) {
      wsp++;
    }
    Advance(afterw, wsp);
    res = afterw;  // (재귀 | s-white*)의 s-white* 폴백 지점
    have = true;
    // 다음 반복은 work(= ns-double-in-line 끝)에서 이어짐
  }
  if (!have) {
    return false;
  }
  cur = res;
  return true;
}

bool Grammar::Isnb_double_multi_line(Cursor& cur, const std::ptrdiff_t n) {
  Isnb_ns_double_in_line(cur);
  {
    const Cursor cp = cur;
    if (Iss_double_next_line(cur, n)) {
      return true;
    }
    cur = cp;
  }
  std::size_t w = 0;
  while (Iss_white(At(cur.cps, w))) {
    w++;
  }
  Advance(cur, w);
  return true;
}

std::size_t Grammar::Isc_quoted_quote(std::span<const std::uint32_t> cps) {
  if (At(cps, 0) == c_single_quote && At(cps, 1) == c_single_quote) {
    return 2;
  }
  return 0;
}

std::size_t Grammar::Isnb_single_char(std::span<const std::uint32_t> cps) {
  if (const std::size_t q = Isc_quoted_quote(cps)) {
    return q;
  }
  const std::uint32_t cp = At(cps, 0);
  if (!Isnb_json(cp)) {
    return 0;
  }
  if (cp == c_single_quote) {
    return 0;
  }
  return 1;
}

std::size_t Grammar::Isns_single_char(std::span<const std::uint32_t> cps) {
  const std::size_t len = Isnb_single_char(cps);
  if (!len) {
    return 0;
  }
  if (len == 1 && Iss_white(At(cps, 0))) {
    return 0;
  }
  return len;
}

bool Grammar::State::Isc_single_quoted(Cursor& cur, const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != c_single_quote) {
    return false;
  }
  Advance(cur, 1);
  const std::size_t b = cur.before.size();
  if (!Isnb_single_text(cur, n)) {
    return false;
  }
  const std::size_t e = cur.before.size();
  if (At(cur.cps, 0) != c_single_quote) {
    return false;
  }
  Advance(cur, 1);
  Emit(cur, {.kind = EventKind::kScalar,
             .style = ScalarStyle::kSingleQuoted,
             .begin = b,
             .end = e});
  return true;
}

bool Grammar::FLOW_IN_state::Isnb_single_text(Cursor& cur,
                                           const std::ptrdiff_t n) const {
  return Isnb_single_multi_line(cur, n);
}

bool Grammar::FLOW_OUT_state::Isnb_single_text(Cursor& cur,
                                            const std::ptrdiff_t n) const {
  return Isnb_single_multi_line(cur, n);
}

bool Grammar::BLOCK_KEY_state::Isnb_single_text(Cursor& cur,
                                             const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return Isnb_single_one_line(cur);
}

bool Grammar::FLOW_KEY_state::Isnb_single_text(Cursor& cur,
                                            const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return Isnb_single_one_line(cur);
}

bool Grammar::Isnb_single_one_line(Cursor& cur) {
  while (const std::size_t d = Isnb_single_char(cur.cps)) {
    Advance(cur, d);
  }
  return true;
}

bool Grammar::Isnb_ns_single_in_line(Cursor& cur) {
  while (true) {
    std::size_t k = 0;
    while (Iss_white(At(cur.cps, k))) {
      k++;
    }
    const std::size_t d = Isns_single_char(cur.cps.subspan(k));
    if (!d) {
      break;
    }
    Advance(cur, k + d);
  }
  return true;
}

bool Grammar::Iss_single_next_line(Cursor& cur, const std::ptrdiff_t n) {
  // 스펙의 자기 재귀를 반복으로 전개 (행 수에 비례한 스택 소비 방지)
  Cursor work = cur;
  bool have = false;
  Cursor res = cur;
  while (true) {
    const Cursor cp = work;
    if (!Iss_flow_folded(work, n)) {
      work = cp;
      break;
    }
    if (AtLineStart(work) && Isc_forbidden(work)) {
      break;
    }
    const std::size_t d = Isns_single_char(work.cps);
    if (!d) {
      res = work;
      have = true;
      break;
    }
    Advance(work, d);
    Isnb_ns_single_in_line(work);
    Cursor afterw = work;
    std::size_t wsp = 0;
    while (Iss_white(At(afterw.cps, wsp))) {
      wsp++;
    }
    Advance(afterw, wsp);
    res = afterw;
    have = true;
  }
  if (!have) {
    return false;
  }
  cur = res;
  return true;
}

bool Grammar::Isnb_single_multi_line(Cursor& cur, const std::ptrdiff_t n) {
  Isnb_ns_single_in_line(cur);
  {
    const Cursor cp = cur;
    if (Iss_single_next_line(cur, n)) {
      return true;
    }
    cur = cp;
  }
  std::size_t w = 0;
  while (Iss_white(At(cur.cps, w))) {
    w++;
  }
  Advance(cur, w);
  return true;
}

bool Grammar::State::Isns_plain_first(Cursor& cur) const {
  // 베어 문서의 내용 행은 문서 마커로 시작할 수 없음 ([206][207])
  if (AtLineStart(cur) && Isc_forbidden(cur)) {
    return false;
  }
  const std::uint32_t cp = At(cur.cps, 0);
  if (Isns_char(cp) && !Isc_indicator(cp)) {
    Advance(cur, 1);
    return true;
  }
  switch (cp) {
    case c_mapping_key:
    case c_mapping_value:
    case c_sequence_entry:
      // [lookahead = ns-plain-safe(c)]
      if (Isns_plain_safe(At(cur.cps, 1))) {
        Advance(cur, 1);
        return true;
      }
      break;
    default:
      break;
  }
  return false;
}

std::uint32_t Grammar::FLOW_OUT_state::Isns_plain_safe(
    const std::uint32_t cp) const {
  return Isns_plain_safe_out(cp);
}

std::uint32_t Grammar::FLOW_IN_state::Isns_plain_safe(
    const std::uint32_t cp) const {
  return Isns_plain_safe_in(cp);
}

std::uint32_t Grammar::BLOCK_KEY_state::Isns_plain_safe(
    const std::uint32_t cp) const {
  return Isns_plain_safe_out(cp);
}

std::uint32_t Grammar::FLOW_KEY_state::Isns_plain_safe(
    const std::uint32_t cp) const {
  return Isns_plain_safe_in(cp);
}

std::uint32_t Grammar::Isns_plain_safe_out(const std::uint32_t cp) {
  return Isns_char(cp);
}

std::uint32_t Grammar::Isns_plain_safe_in(const std::uint32_t cp) {
  if (!Isns_char(cp)) {
    return 0;
  }
  if (Isc_flow_indicator(cp)) {
    return 0;
  }
  return cp;
}

bool Grammar::State::Isns_plain_char(Cursor& cur) const {
  const std::uint32_t cp = At(cur.cps, 0);
  if (Isns_plain_safe(cp) && cp != c_mapping_value && cp != c_comment) {
    Advance(cur, 1);
    return true;
  }
  // [lookbehind = ns-char] '#'
  if (cp == c_comment && !cur.before.empty() && Isns_char(cur.before.back())) {
    Advance(cur, 1);
    return true;
  }
  // ':' [lookahead = ns-plain-safe(c)]
  if (cp == c_mapping_value && Isns_plain_safe(At(cur.cps, 1))) {
    Advance(cur, 1);
    return true;
  }
  return false;
}

bool Grammar::FLOW_OUT_state::Isns_plain(Cursor& cur,
                                      const std::ptrdiff_t n) const {
  return Isns_plain_multi_line(cur, n);
}

bool Grammar::FLOW_IN_state::Isns_plain(Cursor& cur,
                                     const std::ptrdiff_t n) const {
  return Isns_plain_multi_line(cur, n);
}

bool Grammar::BLOCK_KEY_state::Isns_plain(Cursor& cur,
                                       const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return Isns_plain_one_line(cur);
}

bool Grammar::FLOW_KEY_state::Isns_plain(Cursor& cur,
                                      const std::ptrdiff_t n) const {
  static_cast<void>(n);
  return Isns_plain_one_line(cur);
}

bool Grammar::State::Isnb_ns_plain_in_line(Cursor& cur) const {
  while (true) {
    const Cursor cp = cur;
    std::size_t w = 0;
    while (Iss_white(At(cur.cps, w))) {
      w++;
    }
    Advance(cur, w);
    if (!Isns_plain_char(cur)) {
      cur = cp;  // 공백은 plain 문자가 따라올 때만 소비
      break;
    }
  }
  return true;
}

bool Grammar::State::Isns_plain_one_line(Cursor& cur) const {
  if (!Isns_plain_first(cur)) {
    return false;
  }
  return Isnb_ns_plain_in_line(cur);
}

bool Grammar::State::Iss_ns_plain_next_line(Cursor& cur,
                                         const std::ptrdiff_t n) const {
  if (!Iss_flow_folded(cur, n)) {
    return false;
  }
  // 베어 문서의 내용 행은 문서 마커로 시작할 수 없음 ([206][207])
  if (AtLineStart(cur) && Isc_forbidden(cur)) {
    return false;
  }
  if (!Isns_plain_char(cur)) {
    return false;
  }
  return Isnb_ns_plain_in_line(cur);
}

bool Grammar::State::Isns_plain_multi_line(Cursor& cur,
                                        const std::ptrdiff_t n) const {
  if (!Isns_plain_one_line(cur)) {
    return false;
  }
  while (true) {
    const Cursor cp = cur;
    if (!Iss_ns_plain_next_line(cur, n)) {
      cur = cp;
      break;
    }
  }
  return true;
}

const Grammar::State* Grammar::FLOW_OUT_state::InFlow() const {
  return &_flow_in_state;
}

const Grammar::State* Grammar::FLOW_IN_state::InFlow() const {
  return &_flow_in_state;
}

const Grammar::State* Grammar::BLOCK_KEY_state::InFlow() const {
  return &_flow_key_state;
}

const Grammar::State* Grammar::FLOW_KEY_state::InFlow() const {
  return &_flow_key_state;
}

bool Grammar::State::Isin_flow(Cursor& cur, const std::ptrdiff_t n) const {
  const State* mapped = InFlow();
  if (!mapped) {
    return false;  // BLOCK-IN/OUT에는 정의 없음
  }
  return mapped->Isns_s_flow_seq_entries(cur, n);
}

bool Grammar::State::Isc_flow_sequence(Cursor& cur, const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != c_sequence_start) {
    return false;
  }
  Advance(cur, 1);
  Emit(cur, {.kind = EventKind::kSeqStart, .begin = cur.before.size()});
  {
    const Cursor cp = cur;
    if (!Iss_separate(cur, n)) {
      cur = cp;
    }
  }
  {
    const Cursor cp = cur;
    if (!Isin_flow(cur, n)) {
      cur = cp;
    }
  }
  if (At(cur.cps, 0) != c_sequence_end) {
    return false;
  }
  Advance(cur, 1);
  Emit(cur, {.kind = EventKind::kSeqEnd, .begin = cur.before.size()});
  return true;
}

bool Grammar::State::Isns_s_flow_seq_entries(Cursor& cur,
                                          const std::ptrdiff_t n) const {
  // 스펙의 꼬리 재귀를 반복으로 전개 (콤마 수에 비례한 스택 소비 방지)
  if (!Isns_flow_seq_entry(cur, n)) {
    return false;
  }
  {
    const Cursor cp = cur;
    if (!Iss_separate(cur, n)) {
      cur = cp;
    }
  }
  while (At(cur.cps, 0) == c_collect_entry) {
    Advance(cur, 1);
    {
      const Cursor cp = cur;
      if (!Iss_separate(cur, n)) {
        cur = cp;
      }
    }
    {
      const Cursor cp = cur;
      if (!Isns_flow_seq_entry(cur, n)) {
        cur = cp;
        return true;  // 항목 없는 후행 콤마 허용
      }
    }
    {
      const Cursor cp = cur;
      if (!Iss_separate(cur, n)) {
        cur = cp;
      }
    }
  }
  return true;
}

bool Grammar::State::Isns_flow_seq_entry(Cursor& cur,
                                      const std::ptrdiff_t n) const {
  // 스펙 순서는 pair | node지만 그대로 구현하면 pair의 json-key 시도가
  // 서브트리 전체를 파싱한 뒤 ':'가 없어 실패하고 node가 같은 서브트리를
  // 다시 파싱해, 중첩 시퀀스에서 2^깊이 지수 시간이 된다. 언어 동치 변환:
  // 노드 파싱 없이 판별되는 pair('?' 명시 키, ':' 빈 키)만 먼저 시도하고,
  // 그 외에는 node를 한 번만 파싱한 뒤 그 끝(행내 분리 허용) 바로 뒤에
  // ':'가 따라올 때에만 pair로 재시도한다. (implicit key와 node의 행내
  // 문법이 동일하므로 pair가 성립하는 입력에서는 반드시 ':'가 노드 끝
  // 뒤에 드러난다)
  if (At(cur.cps, 0) == c_mapping_key || At(cur.cps, 0) == c_mapping_value) {
    const Cursor cp = cur;
    if (Isns_flow_pair(cur, n)) {
      return true;
    }
    cur = cp;
  }
  const Cursor start = cur;
  if (!Isns_flow_node(cur, n)) {
    cur = start;
    return Isns_flow_pair(cur, n);
  }
  // node 성공: 행내 분리 뒤 ':'가 오는지 엿본다
  Cursor peek = cur;
  {
    const Cursor cp = peek;
    if (!Iss_separate_in_line(peek)) {
      peek = cp;
    }
  }
  if (At(peek.cps, 0) == c_mapping_value) {
    Cursor cp = start;
    if (Isns_flow_pair(cp, n)) {
      cur = cp;  // 스펙 순서상 pair 해석이 우선
      return true;
    }
  }
  return true;  // node
}

bool Grammar::State::Isc_flow_mapping(Cursor& cur, const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != c_mapping_start) {
    return false;
  }
  Advance(cur, 1);
  Emit(cur, {.kind = EventKind::kMapStart, .begin = cur.before.size()});
  {
    const Cursor cp = cur;
    if (!Iss_separate(cur, n)) {
      cur = cp;
    }
  }
  // ns-s-flow-map-entries(n, in-flow(c))?
  if (const State* mapped = InFlow()) {
    const Cursor cp = cur;
    if (!mapped->Isns_s_flow_map_entries(cur, n)) {
      cur = cp;
    }
  }
  if (At(cur.cps, 0) != c_mapping_end) {
    return false;
  }
  Advance(cur, 1);
  Emit(cur, {.kind = EventKind::kMapEnd, .begin = cur.before.size()});
  return true;
}

bool Grammar::State::Isns_s_flow_map_entries(Cursor& cur,
                                          const std::ptrdiff_t n) const {
  // 스펙의 꼬리 재귀를 반복으로 전개 (콤마 수에 비례한 스택 소비 방지)
  if (!Isns_flow_map_entry(cur, n)) {
    return false;
  }
  {
    const Cursor cp = cur;
    if (!Iss_separate(cur, n)) {
      cur = cp;
    }
  }
  while (At(cur.cps, 0) == c_collect_entry) {
    Advance(cur, 1);
    {
      const Cursor cp = cur;
      if (!Iss_separate(cur, n)) {
        cur = cp;
      }
    }
    {
      const Cursor cp = cur;
      if (!Isns_flow_map_entry(cur, n)) {
        cur = cp;
        return true;  // 항목 없는 후행 콤마 허용
      }
    }
    {
      const Cursor cp = cur;
      if (!Iss_separate(cur, n)) {
        cur = cp;
      }
    }
  }
  return true;
}

bool Grammar::State::Isns_flow_map_entry(Cursor& cur,
                                      const std::ptrdiff_t n) const {
  // '?'는 뒤에 분리가 와야 명시적 키 — 아니면 암시적 항목으로 백트래킹
  if (At(cur.cps, 0) == c_mapping_key) {
    const Cursor cp = cur;
    Advance(cur, 1);
    if (Iss_separate(cur, n) && Isns_flow_map_explicit_entry(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Isns_flow_map_implicit_entry(cur, n);
}

bool Grammar::State::Isns_flow_map_explicit_entry(Cursor& cur,
                                               const std::ptrdiff_t n) const {
  {
    const Cursor cp = cur;
    if (Isns_flow_map_implicit_entry(cur, n)) {
      return true;
    }
    cur = cp;
  }
  EmitEmpty(cur);  // e-node e-node — 빈 키 + 빈 값
  EmitEmpty(cur);
  return true;
}

bool Grammar::State::Isns_flow_map_implicit_entry(Cursor& cur,
                                               const std::ptrdiff_t n) const {
  // 최장 매치 선택: yaml-key 대안이 "속성만 있는 빈 키"(예: {!!str "a": v}의
  // "!!str")로 짧게 성공해 json-key 대안을 가리는 것을 방지 — 스펙의
  // 선언적 알터네이션 의미론 유지
  const Cursor start = cur;
  int winner = -1;
  std::size_t best = 0;
  {
    Cursor t = start;
    if (Isns_flow_map_yaml_key_entry(t, n)) {
      winner = 0;
      best = t.before.size();
    }
  }
  {
    Cursor t = start;
    if (Isc_ns_flow_map_empty_key_entry(t, n) &&
        (winner < 0 || t.before.size() > best)) {
      winner = 1;
      best = t.before.size();
    }
  }
  {
    Cursor t = start;
    if (Isc_ns_flow_map_json_key_entry(t, n) &&
        (winner < 0 || t.before.size() > best)) {
      winner = 2;
      best = t.before.size();
    }
  }
  if (winner < 0) {
    return false;
  }
  // 승자를 마지막에 다시 실행 — 이벤트 버퍼에 승자 대안의 이벤트만 남긴다
  // (시도들이 순서대로 서로의 이벤트를 덮어썼으므로 버퍼에는 마지막 시도의
  // 이벤트가 남아 있다. 매처는 결정적이라 재실행 결과는 동일하다)
  cur = start;
  switch (winner) {
    case 0:
      return Isns_flow_map_yaml_key_entry(cur, n);
    case 1:
      return Isc_ns_flow_map_empty_key_entry(cur, n);
    default:
      return Isc_ns_flow_map_json_key_entry(cur, n);
  }
}

bool Grammar::State::Isns_flow_map_yaml_key_entry(Cursor& cur,
                                               const std::ptrdiff_t n) const {
  if (!Isns_flow_yaml_node(cur, n)) {
    return false;
  }
  const Cursor j = cur;
  Cursor j2 = j;
  {
    const Cursor cp = j2;
    if (!Iss_separate(j2, n)) {
      j2 = cp;
    }
  }
  {
    Cursor t = j2;
    if (Isc_ns_flow_map_separate_value(t, n)) {
      cur = t;
      return true;
    }
  }
  if (j2.before.size() != j.before.size()) {
    Cursor t = j;
    if (Isc_ns_flow_map_separate_value(t, n)) {
      cur = t;
      return true;
    }
  }
  cur = j;  // e-node — 값 없는 키
  EmitEmpty(cur);
  return true;
}

bool Grammar::State::Isc_ns_flow_map_empty_key_entry(
    Cursor& cur, const std::ptrdiff_t n) const {
  // e-node(빈 키) 뒤 값 — 실패 시 호출자의 커서 복원이 키 이벤트도 되감는다
  EmitEmpty(cur);
  return Isc_ns_flow_map_separate_value(cur, n);
}

bool Grammar::State::Isc_ns_flow_map_separate_value(
    Cursor& cur, const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != c_mapping_value) {
    return false;
  }
  // [lookahead ≠ ns-plain-safe(c)] — ':'가 plain의 일부가 아닐 것
  if (Isns_plain_safe(At(cur.cps, 1))) {
    return false;
  }
  Advance(cur, 1);
  {
    const Cursor cp = cur;
    if (Iss_separate(cur, n) && Isns_flow_node(cur, n)) {
      return true;
    }
    cur = cp;
  }
  EmitEmpty(cur);  // e-node — 빈 값
  return true;
}

bool Grammar::State::Isc_ns_flow_map_json_key_entry(Cursor& cur,
                                                 const std::ptrdiff_t n) const {
  if (!Isc_flow_json_node(cur, n)) {
    return false;
  }
  const Cursor j = cur;
  Cursor j2 = j;
  {
    const Cursor cp = j2;
    if (!Iss_separate(j2, n)) {
      j2 = cp;
    }
  }
  {
    Cursor t = j2;
    if (Isc_ns_flow_map_adjacent_value(t, n)) {
      cur = t;
      return true;
    }
  }
  if (j2.before.size() != j.before.size()) {
    Cursor t = j;
    if (Isc_ns_flow_map_adjacent_value(t, n)) {
      cur = t;
      return true;
    }
  }
  cur = j;  // e-node — 값 없는 키
  EmitEmpty(cur);
  return true;
}

bool Grammar::State::Isc_ns_flow_map_adjacent_value(
    Cursor& cur, const std::ptrdiff_t n) const {
  if (At(cur.cps, 0) != c_mapping_value) {
    return false;
  }
  Advance(cur, 1);
  const Cursor j = cur;
  Cursor j2 = j;
  {
    const Cursor cp = j2;
    if (!Iss_separate(j2, n)) {
      j2 = cp;
    }
  }
  {
    Cursor t = j2;
    if (Isns_flow_node(t, n)) {
      cur = t;
      return true;
    }
  }
  if (j2.before.size() != j.before.size()) {
    Cursor t = j;
    if (Isns_flow_node(t, n)) {
      cur = t;
      return true;
    }
  }
  cur = j;  // e-node — 빈 값
  EmitEmpty(cur);
  return true;
}

bool Grammar::State::Isns_flow_pair(Cursor& cur, const std::ptrdiff_t n) const {
  // 흐름 시퀀스 안의 단일 쌍은 암시적 단일 항목 매핑이다
  Emit(cur, {.kind = EventKind::kMapStart, .begin = cur.before.size()});
  if (At(cur.cps, 0) == c_mapping_key) {
    const Cursor cp = cur;
    Advance(cur, 1);
    if (Iss_separate(cur, n) && Isns_flow_map_explicit_entry(cur, n)) {
      Emit(cur, {.kind = EventKind::kMapEnd, .begin = cur.before.size()});
      return true;
    }
    cur = cp;
  }
  if (!Isns_flow_pair_entry(cur, n)) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kMapEnd, .begin = cur.before.size()});
  return true;
}

bool Grammar::State::Isns_flow_pair_entry(Cursor& cur,
                                       const std::ptrdiff_t n) const {
  const Cursor start = cur;
  int winner = -1;
  std::size_t best = 0;
  {
    Cursor t = start;
    if (Isns_flow_pair_yaml_key_entry(t, n)) {
      winner = 0;
      best = t.before.size();
    }
  }
  {
    Cursor t = start;
    if (Isc_ns_flow_map_empty_key_entry(t, n) &&
        (winner < 0 || t.before.size() > best)) {
      winner = 1;
      best = t.before.size();
    }
  }
  {
    Cursor t = start;
    if (Isc_ns_flow_pair_json_key_entry(t, n) &&
        (winner < 0 || t.before.size() > best)) {
      winner = 2;
      best = t.before.size();
    }
  }
  if (winner < 0) {
    return false;
  }
  // 승자를 마지막에 다시 실행 — 이벤트 버퍼에 승자 대안의 이벤트만 남긴다
  cur = start;
  switch (winner) {
    case 0:
      return Isns_flow_pair_yaml_key_entry(cur, n);
    case 1:
      return Isc_ns_flow_map_empty_key_entry(cur, n);
    default:
      return Isc_ns_flow_pair_json_key_entry(cur, n);
  }
}

bool Grammar::State::Isns_flow_pair_yaml_key_entry(Cursor& cur,
                                                const std::ptrdiff_t n) const {
  if (!_flow_key_state.Isns_s_implicit_yaml_key(cur)) {
    return false;
  }
  return Isc_ns_flow_map_separate_value(cur, n);
}

bool Grammar::State::Isc_ns_flow_pair_json_key_entry(
    Cursor& cur, const std::ptrdiff_t n) const {
  if (!_flow_key_state.Isc_s_implicit_json_key(cur)) {
    return false;
  }
  return Isc_ns_flow_map_adjacent_value(cur, n);
}

bool Grammar::State::Isns_s_implicit_yaml_key(Cursor& cur) const {
  // 스펙의 1024자 제한을 파싱 창으로도 강제해 키 후보 파싱 비용을 제한
  const std::size_t win =
      std::min(cur.cps.size(), static_cast<std::size_t>(1026));
  Cursor sub = cur;  // 이벤트/진단 공유 — 창 안에서 방출된 키 이벤트를 승계
  sub.cps = cur.cps.first(win);
  if (!Isns_flow_yaml_node(sub, 0)) {  // n은 무관(단일 행)
    return false;
  }
  {
    const Cursor cp = sub;
    if (!Iss_separate_in_line(sub)) {
      sub = cp;
    }
  }
  const std::size_t consumed = sub.before.size() - cur.before.size();
  if (consumed > 1024) {
    return false;  // 스펙: 전체 1024자 제한
  }
  Advance(cur, consumed);
  cur.event_len = sub.event_len;
  return true;
}

bool Grammar::State::Isc_s_implicit_json_key(Cursor& cur) const {
  // 스펙의 1024자 제한을 파싱 창으로도 강제해 키 후보 파싱 비용을 제한
  const std::size_t win =
      std::min(cur.cps.size(), static_cast<std::size_t>(1026));
  Cursor sub = cur;  // 이벤트/진단 공유 — 창 안에서 방출된 키 이벤트를 승계
  sub.cps = cur.cps.first(win);
  if (!Isc_flow_json_node(sub, 0)) {  // n은 무관(단일 행)
    return false;
  }
  {
    const Cursor cp = sub;
    if (!Iss_separate_in_line(sub)) {
      sub = cp;
    }
  }
  const std::size_t consumed = sub.before.size() - cur.before.size();
  if (consumed > 1024) {
    return false;
  }
  Advance(cur, consumed);
  cur.event_len = sub.event_len;
  return true;
}

bool Grammar::State::Isns_flow_yaml_content(Cursor& cur,
                                         const std::ptrdiff_t n) const {
  const std::size_t b = cur.before.size();
  if (!Isns_plain(cur, n)) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kScalar,
             .style = ScalarStyle::kPlain,
             .begin = b,
             .end = cur.before.size()});
  return true;
}

bool Grammar::State::Isc_flow_json_content(Cursor& cur,
                                        const std::ptrdiff_t n) const {
  {
    const Cursor cp = cur;
    if (Isc_flow_sequence(cur, n)) {
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isc_flow_mapping(cur, n)) {
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isc_single_quoted(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Isc_double_quoted(cur, n);
}

bool Grammar::State::Isns_flow_content(Cursor& cur, const std::ptrdiff_t n) const {
  {
    const Cursor cp = cur;
    if (Isns_flow_yaml_content(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Isc_flow_json_content(cur, n);
}

bool Grammar::State::Isns_flow_yaml_node(Cursor& cur,
                                      const std::ptrdiff_t n) const {
  {
    const Cursor cp = cur;
    if (Isc_ns_alias_node(cur)) {
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isns_flow_yaml_content(cur, n)) {
      return true;
    }
    cur = cp;
  }
  if (Isc_ns_properties(cur, n)) {
    const Cursor cp = cur;
    if (Iss_separate(cur, n) && Isns_flow_yaml_content(cur, n)) {
      return true;
    }
    cur = cp;
    EmitEmpty(cur);  // e-scalar — 속성만 있는 빈 스칼라
    return true;
  }
  return false;
}

bool Grammar::State::Isc_flow_json_node(Cursor& cur,
                                     const std::ptrdiff_t n) const {
  // (c-ns-properties(n,c) s-separate(n,c))? — 그룹 성공 후 본문이 실패하면
  // 그룹 없이 재시도
  {
    const Cursor cp = cur;
    if (Isc_ns_properties(cur, n) && Iss_separate(cur, n) &&
        Isc_flow_json_content(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Isc_flow_json_content(cur, n);
}

bool Grammar::State::Isns_flow_node(Cursor& cur, const std::ptrdiff_t n) const {
  {
    const Cursor cp = cur;
    if (Isc_ns_alias_node(cur)) {
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isns_flow_content(cur, n)) {
      return true;
    }
    cur = cp;
  }
  if (Isc_ns_properties(cur, n)) {
    const Cursor cp = cur;
    if (Iss_separate(cur, n) && Isns_flow_content(cur, n)) {
      return true;
    }
    cur = cp;
    EmitEmpty(cur);  // e-scalar — 속성만 있는 빈 스칼라
    return true;
  }
  return false;
}

Grammar::BlockHeader Grammar::Isc_b_block_header(Cursor& cur) {
  // 스펙 1.2.2의 [162][163] 표기는 들여쓰기 지시자를 필수처럼 적고
  // 있으나(알려진 결함), prose 8.1.1.1과 1.2.1의 [163]에 따라 생략 가능하며
  // 생략 시 자동 감지(m = 0으로 표시)로 구현
  std::ptrdiff_t m = 0;
  const ChompingState* t = &_clip_state;
  if (const std::uint32_t d = Isc_indentation_indicator(At(cur.cps, 0))) {
    m = static_cast<std::ptrdiff_t>(d - '0');
    Advance(cur, 1);
    t = Isc_chomping_indicator(cur);
  } else {
    t = Isc_chomping_indicator(cur);
    if (const std::uint32_t d2 = Isc_indentation_indicator(At(cur.cps, 0))) {
      m = static_cast<std::ptrdiff_t>(d2 - '0');
      Advance(cur, 1);
    }
  }
  if (!Iss_b_comment(cur)) {
    return {false, 0, nullptr};
  }
  return {true, m, t};
}

std::uint32_t Grammar::Isc_indentation_indicator(const std::uint32_t cp) {
  if ('1' <= cp && cp <= '9') {
    return cp;
  }
  return 0;
}

const Grammar::ChompingState* Grammar::Isc_chomping_indicator(Cursor& cur) {
  switch (At(cur.cps, 0)) {
    case '-':
      Advance(cur, 1);
      return &_strip_state;
    case '+':
      Advance(cur, 1);
      return &_keep_state;
    default:
      break;
  }
  return &_clip_state;  // "" — 빈 매치
}
bool Grammar::ChompingState::Isb_chomped_last(Cursor& cur) const {
  if (AtEnd(cur.cps, 0)) {
    return true;  // <end-of-input>
  }
  const std::size_t b = Isb_as_line_feed(cur.cps);
  if (b) {
    Advance(cur, b);
    return true;
  }
  return false;
}
bool Grammar::STRIP_state::Isb_chomped_last(Cursor& cur) const {
  if (AtEnd(cur.cps, 0)) {
    return true;
  }
  const std::size_t b = Isb_non_content(cur.cps);
  if (b) {
    Advance(cur, b);
    return true;
  }
  return false;
}
bool Grammar::ChompingState::Isl_chomped_empty(Cursor& cur,
                                            const std::ptrdiff_t n) const {
  return Isl_strip_empty(cur, n);
}
bool Grammar::KEEP_state::Isl_chomped_empty(Cursor& cur,
                                         const std::ptrdiff_t n) const {
  return Isl_keep_empty(cur, n);
}

bool Grammar::Isl_strip_empty(Cursor& cur, const std::ptrdiff_t n) {
  while (true) {
    const Cursor cp = cur;
    if (!Iss_indent_less_or_equal(cur, n)) {
      cur = cp;
      break;
    }
    const std::size_t b = Isb_non_content(cur.cps);
    if (!b) {
      cur = cp;
      break;
    }
    Advance(cur, b);
  }
  {
    const Cursor cp = cur;
    if (!Isl_trail_comments(cur, n)) {
      cur = cp;
    }
  }
  return true;
}

bool Grammar::Isl_keep_empty(Cursor& cur, const std::ptrdiff_t n) {
  while (true) {
    const Cursor cp = cur;
    if (!_block_in_state.Isl_empty(cur, n)) {
      cur = cp;
      break;
    }
  }
  {
    const Cursor cp = cur;
    if (!Isl_trail_comments(cur, n)) {
      cur = cp;
    }
  }
  return true;
}

bool Grammar::Isl_trail_comments(Cursor& cur, const std::ptrdiff_t n) {
  if (!Iss_indent_less_than(cur, n)) {
    return false;
  }
  if (!Isc_nb_comment_text(cur)) {
    return false;
  }
  if (!Isb_comment(cur)) {
    return false;
  }
  while (true) {
    const Cursor cp = cur;
    if (!Isl_comment(cur)) {
      cur = cp;
      break;
    }
    if (cur.before.size() == cp.before.size()) {
      cur = cp;
      break;
    }
  }
  return true;
}

bool Grammar::Isc_l_literal(Cursor& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != c_literal) {
    return false;
  }
  Advance(cur, 1);
  const BlockHeader h = Isc_b_block_header(cur);
  if (!h.ok) {
    return false;
  }
  std::ptrdiff_t m = h.m;
  if (m == 0) {
    const std::ptrdiff_t det = DetectScalarIndentation(cur.cps, n);
    if (!det) {
      return false;
    }
    m = det;
  }
  const std::size_t b = cur.before.size();
  if (!Isl_literal_content(cur, n + m, *h.t)) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kScalar,
             .style = ScalarStyle::kLiteral,
             .chomp = ChompKindOf(h.t),
             .indent = n + m,
             .begin = b,
             .end = cur.before.size()});
  return true;
}

bool Grammar::Isl_nb_literal_text(Cursor& cur, const std::ptrdiff_t n) {
  while (true) {
    const Cursor cp = cur;
    if (!_block_in_state.Isl_empty(cur, n)) {
      cur = cp;
      break;
    }
  }
  if (!Iss_indent(cur, n)) {
    return false;
  }
  // 베어 문서의 내용 행은 문서 마커로 시작할 수 없음 ([206][207])
  if (AtLineStart(cur) && Isc_forbidden(cur)) {
    return false;
  }
  std::size_t k = 0;
  while (Isnb_char(At(cur.cps, k))) {
    k++;
  }
  if (k == 0) {
    return false;  // nb-char+ — 1자 이상
  }
  Advance(cur, k);
  return true;
}

bool Grammar::Isb_nb_literal_next(Cursor& cur, const std::ptrdiff_t n) {
  const std::size_t b = Isb_as_line_feed(cur.cps);
  if (!b) {
    return false;
  }
  Advance(cur, b);
  return Isl_nb_literal_text(cur, n);
}

bool Grammar::Isl_literal_content(Cursor& cur, const std::ptrdiff_t n,
                               const ChompingState& t) {
  // (l-nb-literal-text b-nb-literal-next* b-chomped-last)? l-chomped-empty
  {
    const Cursor cp = cur;
    if (Isl_nb_literal_text(cur, n)) {
      while (true) {
        const Cursor cp2 = cur;
        if (!Isb_nb_literal_next(cur, n)) {
          cur = cp2;
          break;
        }
      }
      if (t.Isb_chomped_last(cur) && t.Isl_chomped_empty(cur, n)) {
        return true;
      }
    }
    cur = cp;  // 그룹 도중 실패 → 그룹 없이 재시도
  }
  return t.Isl_chomped_empty(cur, n);
}

bool Grammar::Isc_l_folded(Cursor& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != c_folded) {
    return false;
  }
  Advance(cur, 1);
  const BlockHeader h = Isc_b_block_header(cur);
  if (!h.ok) {
    return false;
  }
  std::ptrdiff_t m = h.m;
  if (m == 0) {
    const std::ptrdiff_t det = DetectScalarIndentation(cur.cps, n);
    if (!det) {
      return false;
    }
    m = det;
  }
  const std::size_t b = cur.before.size();
  if (!Isl_folded_content(cur, n + m, *h.t)) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kScalar,
             .style = ScalarStyle::kFolded,
             .chomp = ChompKindOf(h.t),
             .indent = n + m,
             .begin = b,
             .end = cur.before.size()});
  return true;
}

bool Grammar::Iss_nb_folded_text(Cursor& cur, const std::ptrdiff_t n) {
  if (!Iss_indent(cur, n)) {
    return false;
  }
  if (AtLineStart(cur) && Isc_forbidden(cur)) {
    return false;
  }
  if (!Isns_char(At(cur.cps, 0))) {
    return false;
  }
  std::size_t k = 1;
  while (Isnb_char(At(cur.cps, k))) {
    k++;
  }
  Advance(cur, k);
  return true;
}

bool Grammar::Isl_nb_folded_lines(Cursor& cur, const std::ptrdiff_t n) {
  if (!Iss_nb_folded_text(cur, n)) {
    return false;
  }
  while (true) {
    const Cursor cp = cur;
    if (!_block_in_state.Isb_l_folded(cur, n)) {
      cur = cp;
      break;
    }
    if (!Iss_nb_folded_text(cur, n)) {
      cur = cp;
      break;
    }
  }
  return true;
}

bool Grammar::Iss_nb_spaced_text(Cursor& cur, const std::ptrdiff_t n) {
  if (!Iss_indent(cur, n)) {
    return false;
  }
  // 첫 문자가 s-white이므로 문서 마커일 수 없음 — c-forbidden 검사 불필요
  if (!Iss_white(At(cur.cps, 0))) {
    return false;
  }
  std::size_t k = 1;
  while (Isnb_char(At(cur.cps, k))) {
    k++;
  }
  Advance(cur, k);
  return true;
}

bool Grammar::Isb_l_spaced(Cursor& cur, const std::ptrdiff_t n) {
  const std::size_t b = Isb_as_line_feed(cur.cps);
  if (!b) {
    return false;
  }
  Advance(cur, b);
  while (true) {
    const Cursor cp = cur;
    if (!_block_in_state.Isl_empty(cur, n)) {
      cur = cp;
      break;
    }
  }
  return true;
}

bool Grammar::Isl_nb_spaced_lines(Cursor& cur, const std::ptrdiff_t n) {
  if (!Iss_nb_spaced_text(cur, n)) {
    return false;
  }
  while (true) {
    const Cursor cp = cur;
    if (!Isb_l_spaced(cur, n)) {
      cur = cp;
      break;
    }
    if (!Iss_nb_spaced_text(cur, n)) {
      cur = cp;
      break;
    }
  }
  return true;
}

bool Grammar::Isl_nb_same_lines(Cursor& cur, const std::ptrdiff_t n) {
  while (true) {
    const Cursor cp = cur;
    if (!_block_in_state.Isl_empty(cur, n)) {
      cur = cp;
      break;
    }
  }
  {
    const Cursor cp = cur;
    if (Isl_nb_folded_lines(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Isl_nb_spaced_lines(cur, n);
}

bool Grammar::Isl_nb_diff_lines(Cursor& cur, const std::ptrdiff_t n) {
  if (!Isl_nb_same_lines(cur, n)) {
    return false;
  }
  while (true) {
    const Cursor cp = cur;
    const std::size_t b = Isb_as_line_feed(cur.cps);
    if (!b) {
      break;
    }
    Advance(cur, b);
    if (!Isl_nb_same_lines(cur, n)) {
      cur = cp;
      break;
    }
  }
  return true;
}

bool Grammar::Isl_folded_content(Cursor& cur, const std::ptrdiff_t n,
                              const ChompingState& t) {
  // (l-nb-diff-lines b-chomped-last)? l-chomped-empty
  {
    const Cursor cp = cur;
    if (Isl_nb_diff_lines(cur, n) && t.Isb_chomped_last(cur) &&
        t.Isl_chomped_empty(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return t.Isl_chomped_empty(cur, n);
}

bool Grammar::Isl_block_sequence(Cursor& cur, const std::ptrdiff_t n) {
  // m 자동 감지: 첫 항목 행의 들여쓰기 w = n+1+m (m ≥ 0)
  const std::size_t w = LeadingSpaces(cur.cps);
  const std::ptrdiff_t sw = static_cast<std::ptrdiff_t>(w);
  if (sw < n + 1) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kSeqStart, .begin = cur.before.size()});
  std::size_t count = 0;
  while (true) {
    const Cursor cp = cur;
    if (!Iss_indent(cur, sw)) {
      cur = cp;
      break;
    }
    if (!Isc_l_block_seq_entry(cur, sw)) {
      cur = cp;
      break;
    }
    count++;
  }
  if (!count) {
    return false;  // 실패 — 호출자의 커서 복원이 SeqStart도 되감는다
  }
  Emit(cur, {.kind = EventKind::kSeqEnd, .begin = cur.before.size()});
  return true;
}

bool Grammar::Isc_l_block_seq_entry(Cursor& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != c_sequence_entry) {
    return false;
  }
  // [lookahead ≠ ns-char] — "-a"는 plain 스칼라의 시작
  if (Isns_char(At(cur.cps, 1))) {
    return false;
  }
  Advance(cur, 1);
  return _block_in_state.Iss_l_block_indented(cur, n);
}

bool Grammar::State::Iss_l_block_indented(Cursor& cur,
                                       const std::ptrdiff_t n) const {
  // m 자동 감지: 현 위치의 공백 수
  const std::size_t m = LeadingSpaces(cur.cps);
  {
    const Cursor cp = cur;
    Advance(cur, m);
    const std::ptrdiff_t nm = n + 1 + static_cast<std::ptrdiff_t>(m);
    {
      const Cursor cp2 = cur;
      if (Isns_l_compact_sequence(cur, nm)) {
        return true;
      }
      cur = cp2;
    }
    {
      const Cursor cp2 = cur;
      if (Isns_l_compact_mapping(cur, nm)) {
        return true;
      }
      cur = cp2;
    }
    cur = cp;  // 압축 컬렉션 실패 → 원위치(i)에서 재개
  }
  {
    const Cursor cp = cur;
    if (Iss_l_block_node(cur, n)) {
      return true;
    }
    cur = cp;
  }
  // e-node s-l-comments — 빈 노드
  if (!Iss_l_comments(cur)) {
    return false;
  }
  EmitEmpty(cur);
  return true;
}

bool Grammar::Isns_l_compact_sequence(Cursor& cur, const std::ptrdiff_t n) {
  Emit(cur, {.kind = EventKind::kSeqStart, .begin = cur.before.size()});
  if (!Isc_l_block_seq_entry(cur, n)) {
    return false;
  }
  while (true) {
    const Cursor cp = cur;
    if (!Iss_indent(cur, n)) {
      cur = cp;
      break;
    }
    if (!Isc_l_block_seq_entry(cur, n)) {
      cur = cp;
      break;
    }
  }
  Emit(cur, {.kind = EventKind::kSeqEnd, .begin = cur.before.size()});
  return true;
}

bool Grammar::Isl_block_mapping(Cursor& cur, const std::ptrdiff_t n) {
  // m 자동 감지: 첫 항목 행의 들여쓰기 w = n+1+m (m ≥ 0)
  const std::size_t w = LeadingSpaces(cur.cps);
  const std::ptrdiff_t sw = static_cast<std::ptrdiff_t>(w);
  if (sw < n + 1) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kMapStart, .begin = cur.before.size()});
  std::size_t count = 0;
  while (true) {
    const Cursor cp = cur;
    if (!Iss_indent(cur, sw)) {
      cur = cp;
      break;
    }
    if (!Isns_l_block_map_entry(cur, sw)) {
      cur = cp;
      break;
    }
    count++;
  }
  if (!count) {
    return false;  // 실패 — 호출자의 커서 복원이 MapStart도 되감는다
  }
  Emit(cur, {.kind = EventKind::kMapEnd, .begin = cur.before.size()});
  return true;
}

bool Grammar::Isns_l_block_map_entry(Cursor& cur, const std::ptrdiff_t n) {
  {
    const Cursor cp = cur;
    if (Isc_l_block_map_explicit_entry(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Isns_l_block_map_implicit_entry(cur, n);
}

bool Grammar::Isc_l_block_map_explicit_entry(Cursor& cur, const std::ptrdiff_t n) {
  if (!Isc_l_block_map_explicit_key(cur, n)) {
    return false;
  }
  {
    const Cursor cp = cur;
    if (Isl_block_map_explicit_value(cur, n)) {
      return true;
    }
    cur = cp;
  }
  EmitEmpty(cur);  // e-node — 값 없는 명시적 키
  return true;
}

bool Grammar::Isc_l_block_map_explicit_key(Cursor& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != c_mapping_key) {
    return false;
  }
  // '?' 뒤에 ns-char가 붙으면 s-l+block-indented가 자연히 실패하고
  // 암시적 키(plain "?...")로 백트래킹된다
  Advance(cur, 1);
  return _block_out_state.Iss_l_block_indented(cur, n);
}

bool Grammar::Isl_block_map_explicit_value(Cursor& cur, const std::ptrdiff_t n) {
  if (!Iss_indent(cur, n)) {
    return false;
  }
  if (At(cur.cps, 0) != c_mapping_value) {
    return false;
  }
  Advance(cur, 1);
  return _block_out_state.Iss_l_block_indented(cur, n);
}

bool Grammar::Isns_l_block_map_implicit_entry(Cursor& cur,
                                           const std::ptrdiff_t n) {
  {
    const Cursor cp = cur;
    if (Isns_s_block_map_implicit_key(cur) &&
        Isc_l_block_map_implicit_value(cur, n)) {
      return true;
    }
    cur = cp;
  }
  // | e-node — 빈 키 (":"로 시작하는 행)
  // 실패 시 호출자의 커서 복원이 키 이벤트도 되감는다
  EmitEmpty(cur);
  return Isc_l_block_map_implicit_value(cur, n);
}

bool Grammar::Isns_s_block_map_implicit_key(Cursor& cur) {
  {
    const Cursor cp = cur;
    if (_block_key_state.Isc_s_implicit_json_key(cur)) {
      return true;
    }
    cur = cp;
  }
  return _block_key_state.Isns_s_implicit_yaml_key(cur);
}

bool Grammar::Isc_l_block_map_implicit_value(Cursor& cur, const std::ptrdiff_t n) {
  if (At(cur.cps, 0) != c_mapping_value) {
    return false;
  }
  Advance(cur, 1);
  {
    const Cursor cp = cur;
    if (_block_out_state.Iss_l_block_node(cur, n)) {
      return true;
    }
    cur = cp;
  }
  // e-node s-l-comments — 빈 값
  if (!Iss_l_comments(cur)) {
    return false;
  }
  EmitEmpty(cur);
  return true;
}

bool Grammar::Isns_l_compact_mapping(Cursor& cur, const std::ptrdiff_t n) {
  Emit(cur, {.kind = EventKind::kMapStart, .begin = cur.before.size()});
  if (!Isns_l_block_map_entry(cur, n)) {
    return false;
  }
  while (true) {
    const Cursor cp = cur;
    if (!Iss_indent(cur, n)) {
      cur = cp;
      break;
    }
    if (!Isns_l_block_map_entry(cur, n)) {
      cur = cp;
      break;
    }
  }
  Emit(cur, {.kind = EventKind::kMapEnd, .begin = cur.before.size()});
  return true;
}

bool Grammar::State::Iss_l_block_node(Cursor& cur, const std::ptrdiff_t n) const {
  {
    const Cursor cp = cur;
    if (Iss_l_block_in_block(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Iss_l_flow_in_block(cur, n);
}

bool Grammar::Iss_l_flow_in_block(Cursor& cur, const std::ptrdiff_t n) {
  if (!_flow_out_state.Iss_separate(cur, n + 1)) {
    return false;
  }
  if (!_flow_out_state.Isns_flow_node(cur, n + 1)) {
    return false;
  }
  return Iss_l_comments(cur);
}

bool Grammar::State::Iss_l_block_in_block(Cursor& cur,
                                       const std::ptrdiff_t n) const {
  {
    const Cursor cp = cur;
    if (Iss_l_block_scalar(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Iss_l_block_collection(cur, n);
}

bool Grammar::State::Iss_l_block_scalar(Cursor& cur,
                                     const std::ptrdiff_t n) const {
  if (!Iss_separate(cur, n + 1)) {
    return false;
  }
  // (c-ns-properties(n+1,c) s-separate(n+1,c))? — 그룹 성공 후 본문이
  // 실패하면 그룹 없이 재시도
  {
    const Cursor cp = cur;
    if (Isc_ns_properties(cur, n + 1) && Iss_separate(cur, n + 1)) {
      {
        const Cursor cp2 = cur;
        if (Isc_l_literal(cur, n)) {
          return true;
        }
        cur = cp2;
      }
      {
        const Cursor cp2 = cur;
        if (Isc_l_folded(cur, n)) {
          return true;
        }
        cur = cp2;
      }
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isc_l_literal(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Isc_l_folded(cur, n);
}

bool Grammar::State::Iss_l_block_collection(Cursor& cur,
                                         const std::ptrdiff_t n) const {
  // (s-separate(n+1,c) c-ns-properties(n+1,c))? s-l-comments
  // (seq-space(n,c) | l+block-mapping(n)) — 속성 그룹 성공 후 본문이
  // 실패하면 그룹 없이 재시도
  {
    const Cursor cp = cur;
    if (Iss_separate(cur, n + 1) && Isc_ns_properties(cur, n + 1) &&
        Iss_l_comments(cur)) {
      {
        const Cursor cp2 = cur;
        if (Isseq_space(cur, n)) {
          return true;
        }
        cur = cp2;
      }
      {
        const Cursor cp2 = cur;
        if (Isl_block_mapping(cur, n)) {
          return true;
        }
        cur = cp2;
      }
    }
    cur = cp;
  }
  if (!Iss_l_comments(cur)) {
    return false;
  }
  {
    const Cursor cp = cur;
    if (Isseq_space(cur, n)) {
      return true;
    }
    cur = cp;
  }
  return Isl_block_mapping(cur, n);
}

bool Grammar::BLOCK_OUT_state::Isseq_space(Cursor& cur,
                                        const std::ptrdiff_t n) const {
  return Isl_block_sequence(cur, n - 1);
}

bool Grammar::BLOCK_IN_state::Isseq_space(Cursor& cur,
                                       const std::ptrdiff_t n) const {
  return Isl_block_sequence(cur, n);
}

bool Grammar::Isl_document_prefix(Cursor& cur) {
  if (At(cur.cps, 0) == c_byte_order_mark) {
    Advance(cur, 1);
  }
  while (true) {
    const Cursor cp = cur;
    if (!Isl_comment(cur)) {
      cur = cp;
      break;
    }
    if (cur.before.size() == cp.before.size()) {
      cur = cp;
      break;
    }
  }
  return true;  // 빈 매치 가능
}

bool Grammar::Isc_directives_end(Cursor& cur) {
  if (At(cur.cps, 0) == '-' && At(cur.cps, 1) == '-' && At(cur.cps, 2) == '-') {
    Advance(cur, 3);
    return true;
  }
  return false;
}

bool Grammar::Isc_document_end(Cursor& cur) {
  if (At(cur.cps, 0) != '.' || At(cur.cps, 1) != '.' || At(cur.cps, 2) != '.') {
    return false;
  }
  // 스펙 주석: (not followed by non-ws char)
  const std::uint32_t f = At(cur.cps, 3);
  if (!AtEnd(cur.cps, 3) && !Iss_white(f) && !Isb_char(f)) {
    return false;
  }
  Advance(cur, 3);
  return true;
}

bool Grammar::Isl_document_suffix(Cursor& cur) {
  if (!Isc_document_end(cur)) {
    return false;
  }
  return Iss_l_comments(cur);
}

bool Grammar::Isc_forbidden(const Cursor& cur) {
  if (!AtLineStart(cur)) {
    return false;
  }
  const bool de =
      At(cur.cps, 0) == '-' && At(cur.cps, 1) == '-' && At(cur.cps, 2) == '-';
  const bool dd =
      At(cur.cps, 0) == '.' && At(cur.cps, 1) == '.' && At(cur.cps, 2) == '.';
  if (!de && !dd) {
    return false;
  }
  const std::size_t j = 3;  // c-directives-end | c-document-end
  if (AtEnd(cur.cps, j)) {
    return true;  // <end-of-input>
  }
  const std::uint32_t f = At(cur.cps, j);
  return Isb_char(f) || Iss_white(f);
}

bool Grammar::Isl_bare_document(Cursor& cur) {
  return _block_in_state.Iss_l_block_node(cur, -1);
}

bool Grammar::Isl_explicit_document(Cursor& cur) {
  if (!Isc_directives_end(cur)) {
    return false;
  }
  {
    const Cursor cp = cur;
    if (Isl_bare_document(cur)) {
      return true;
    }
    cur = cp;
  }
  // e-node s-l-comments — 내용 없는 문서
  if (!Iss_l_comments(cur)) {
    return false;
  }
  EmitEmpty(cur);
  return true;
}

bool Grammar::Isl_directive_document(Cursor& cur) {
  std::size_t count = 0;
  while (true) {
    const Cursor cp = cur;
    if (!Isl_directive(cur)) {
      cur = cp;
      break;
    }
    count++;  // l-directive는 항상 '%'를 소비하므로 진행 보장
  }
  if (!count) {
    return false;  // l-directive+ — 1회 이상
  }
  return Isl_explicit_document(cur);
}

bool Grammar::Isl_any_document(Cursor& cur) {
  // 문서 경계 이벤트는 여기서만 방출한다(내부의 explicit/bare 재사용과
  // 중복되지 않도록). 실패 시 호출자의 커서 복원이 DocStart를 되감는다.
  Emit(cur, {.kind = EventKind::kDocStart, .begin = cur.before.size()});
  {
    const Cursor cp = cur;
    if (Isl_directive_document(cur)) {
      Emit(cur, {.kind = EventKind::kDocEnd, .begin = cur.before.size()});
      return true;
    }
    cur = cp;
  }
  {
    const Cursor cp = cur;
    if (Isl_explicit_document(cur)) {
      Emit(cur, {.kind = EventKind::kDocEnd, .begin = cur.before.size()});
      return true;
    }
    cur = cp;
  }
  if (!Isl_bare_document(cur)) {
    return false;
  }
  Emit(cur, {.kind = EventKind::kDocEnd, .begin = cur.before.size()});
  return true;
}

bool Grammar::Isl_yaml_stream(Cursor& cur) {
  while (true) {  // l-document-prefix* (빈 매치 시 종료)
    const Cursor cp = cur;
    if (!Isl_document_prefix(cur)) {
      cur = cp;
      break;
    }
    if (cur.before.size() == cp.before.size()) {
      cur = cp;
      break;
    }
  }
  {
    const Cursor cp = cur;
    if (!Isl_any_document(cur)) {
      cur = cp;
    }
  }
  while (true) {
    // (l-document-suffix+ l-document-prefix* l-any-document?)
    // | c-byte-order-mark | l-comment | l-explicit-document
    {
      const Cursor cp = cur;
      if (Isl_document_suffix(cur)) {
        while (true) {
          const Cursor cp2 = cur;
          if (!Isl_document_suffix(cur)) {
            cur = cp2;
            break;  // suffix는 항상 "..."를 소비하므로 진행 보장
          }
        }
        while (true) {
          const Cursor cp2 = cur;
          if (!Isl_document_prefix(cur)) {
            cur = cp2;
            break;
          }
          if (cur.before.size() == cp2.before.size()) {
            cur = cp2;
            break;
          }
        }
        {
          const Cursor cp2 = cur;
          if (!Isl_any_document(cur)) {
            cur = cp2;
          }
        }
        continue;
      }
      cur = cp;
    }
    if (At(cur.cps, 0) == c_byte_order_mark) {
      Advance(cur, 1);
      continue;
    }
    {
      const Cursor cp = cur;
      if (Isl_comment(cur) && cur.before.size() > cp.before.size()) {
        continue;
      }
      cur = cp;
    }
    {
      const Cursor cp = cur;
      Emit(cur, {.kind = EventKind::kDocStart, .begin = cur.before.size()});
      if (Isl_explicit_document(cur) && cur.before.size() > cp.before.size()) {
        Emit(cur, {.kind = EventKind::kDocEnd, .begin = cur.before.size()});
        continue;
      }
      cur = cp;
    }
    break;
  }
  if (cur.events) {
    cur.events->list.resize(cur.event_len);  // 죽은 꼬리 이벤트 확정 폐기
  }
  return true;
}
}  // namespace bedrock::archive::yaml
