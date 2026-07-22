/**
 * @file compose.cc
 * @brief YAML 이벤트 → 표현(Node) 트리 조립기(Compose) 구현.
 *
 * 스칼라 디코더들은 문법이 이미 검증한 구간을 받으므로 형식 오류를
 * 다시 진단하지 않고 값 복원에만 집중한다. 접힘(folding) 규칙:
 * 원시 줄바꿈 1개는 공백으로, 연속 k+1개(빈 줄 k개)는 줄바꿈 k개로
 * 접히고, 줄 경계의 원시 공백은 제거된다(이스케이프된 공백은 보존).
 */
#include "common/archive/yaml/compose.h"

#include <cstddef>
#include <map>
#include <utility>

#include "common/util/unicode.h"

namespace bedrock::archive::yaml {

namespace {

using transcriber::Node;
using util::AppendUtf8;
using util::EncodeUtf8;

/** @brief 줄 나눔(CRLF|CR|LF)의 길이. 아니면 0. */
std::size_t BreakLen(std::span<const std::uint32_t> cps, std::size_t i) {
  if (i >= cps.size()) {
    return 0;
  }
  if (cps[i] == '\r') {
    return (i + 1 < cps.size() && cps[i + 1] == '\n') ? 2 : 1;
  }
  return cps[i] == '\n' ? 1 : 0;
}

/** @brief 공백류(스페이스/탭)인지. */
bool IsWhite(std::uint32_t c) { return c == ' ' || c == '\t'; }

/** @brief 원문 한 행 — 본문 구간과 뒤따르는 줄 나눔 존재 여부. */
struct RawLine {
  std::span<const std::uint32_t> text;
  bool had_break;
};

/** @brief 구간을 행 단위로 분할한다(줄 나눔은 본문에서 제외). */
std::vector<RawLine> SplitLines(std::span<const std::uint32_t> cps) {
  std::vector<RawLine> lines;
  std::size_t start = 0;
  std::size_t i = 0;
  while (i < cps.size()) {
    const std::size_t b = BreakLen(cps, i);
    if (b) {
      lines.push_back({cps.subspan(start, i - start), true});
      i += b;
      start = i;
    } else {
      i++;
    }
  }
  lines.push_back({cps.subspan(start), false});
  return lines;
}

/** @brief 앞뒤 공백류를 잘라낸 구간(선두는 strip_lead일 때만). */
std::span<const std::uint32_t> Trim(std::span<const std::uint32_t> s,
                                    bool strip_lead) {
  std::size_t b = 0;
  std::size_t e = s.size();
  if (strip_lead) {
    while (b < e && IsWhite(s[b])) {
      b++;
    }
  }
  while (e > b && IsWhite(s[e - 1])) {
    e--;
  }
  return s.subspan(b, e - b);
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
  auto append_line = [&](std::span<const std::uint32_t> l) {
    for (std::size_t i = 0; i < l.size();) {
      if (single_quoted && l[i] == '\'' && i + 1 < l.size() &&
          l[i + 1] == '\'') {
        out += '\'';
        i += 2;
        continue;
      }
      AppendUtf8(out, static_cast<char32_t>(l[i]));
      i++;
    }
  };
  bool first = true;
  std::size_t pending_empties = 0;
  for (std::size_t li = 0; li < lines.size(); li++) {
    const std::span<const std::uint32_t> l = Trim(lines[li].text, !first);
    if (first) {
      append_line(l);
      first = false;
      continue;
    }
    if (l.empty()) {
      pending_empties++;
      continue;
    }
    if (pending_empties) {
      out.append(pending_empties, '\n');
      pending_empties = 0;
    } else {
      out += ' ';
    }
    append_line(l);
  }
  return out;  // 구간은 내용으로 끝나므로 후행 빈 행은 없다
}

/** @brief 겹따옴표 디코드의 중간 조각 — 이스케이프 여부를 함께 기억. */
struct Piece {
  char32_t cp;
  bool escaped;
};

/** @brief 이스케이프된 줄바꿈(조인) 마커 — 유효 코드포인트 밖의 값. */
constexpr char32_t kJoinMarker = 0x7FFFFFFF;

/** @brief 16진 이스케이프(xNN/uNNNN/UNNNNNNNN)의 값을 읽는다. */
char32_t ReadHex(std::span<const std::uint32_t> cps, std::size_t i,
                 std::size_t digits) {
  std::uint32_t v = 0;
  for (std::size_t k = 0; k < digits && i + k < cps.size(); k++) {
    const std::uint32_t c = cps[i + k];
    std::uint32_t d = 0;
    if ('0' <= c && c <= '9') {
      d = c - '0';
    } else if ('A' <= c && c <= 'F') {
      d = c - 'A' + 10;
    } else if ('a' <= c && c <= 'f') {
      d = c - 'a' + 10;
    }
    v = (v << 4) | d;
  }
  return static_cast<char32_t>(v);
}

/**
 * @brief 겹따옴표 스칼라 디코드 — 전체 이스케이프 집합([42]~[62]) 해석
 * 후 접힘. 이스케이프된 줄바꿈은 아무것도 내지 않고(조인), 뒤따르는
 * 빈 행들은 각각 줄바꿈이 된다.
 */
std::string DecodeDouble(std::span<const std::uint32_t> cps) {
  // 1단계: 이스케이프 해석 — 원시/이스케이프 구분을 남긴다
  // (원시 줄바꿈은 CRLF 포함 모두 단일 '\n' 조각으로 정규화)
  std::vector<Piece> ps;
  ps.reserve(cps.size());
  for (std::size_t i = 0; i < cps.size();) {
    if (const std::size_t bl = BreakLen(cps, i)) {
      ps.push_back({U'\n', false});
      i += bl;
      continue;
    }
    if (cps[i] != '\\') {
      ps.push_back({static_cast<char32_t>(cps[i]), false});
      i++;
      continue;
    }
    const std::uint32_t c1 = i + 1 < cps.size() ? cps[i + 1] : 0;
    if (BreakLen(cps, i + 1)) {  // 이스케이프된 줄바꿈
      ps.push_back({kJoinMarker, true});
      i += 1 + BreakLen(cps, i + 1);
      continue;
    }
    char32_t cp = 0;
    std::size_t len = 2;
    switch (c1) {
      case '0': cp = 0x00; break;
      case 'a': cp = 0x07; break;
      case 'b': cp = 0x08; break;
      case 't': case '\t': cp = 0x09; break;
      case 'n': cp = 0x0A; break;
      case 'v': cp = 0x0B; break;
      case 'f': cp = 0x0C; break;
      case 'r': cp = 0x0D; break;
      case 'e': cp = 0x1B; break;
      case ' ': cp = 0x20; break;
      case '"': cp = 0x22; break;
      case '/': cp = 0x2F; break;
      case '\\': cp = 0x5C; break;
      case 'N': cp = 0x85; break;
      case '_': cp = 0xA0; break;
      case 'L': cp = 0x2028; break;
      case 'P': cp = 0x2029; break;
      case 'x': cp = ReadHex(cps, i + 2, 2); len = 4; break;
      case 'u': cp = ReadHex(cps, i + 2, 4); len = 6; break;
      case 'U': cp = ReadHex(cps, i + 2, 8); len = 10; break;
      default: cp = static_cast<char32_t>(c1); break;  // 문법상 도달 불가
    }
    ps.push_back({cp, true});
    i += len;
  }
  // 2단계: 접힘 — 원시 줄바꿈/공백만 접힘 대상(이스케이프는 내용)
  std::string out;
  std::string line;        // 현재 행의 디코드 결과
  std::size_t raw_ws = 0;  // line 끝의 원시 공백 바이트 수(접힘 때 제거)
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
  for (const Piece& p : ps) {
    const bool raw_white =
        !p.escaped && IsWhite(static_cast<std::uint32_t>(p.cp));
    if (!p.escaped && p.cp == U'\n') {
      // 원시 줄바꿈: 행 마감(원시 후행 공백 제거)
      line.erase(line.size() - raw_ws);
      raw_ws = 0;
      out += line;
      line.clear();
      breaks++;
      at_line_start = true;
      continue;
    }
    if (p.escaped && p.cp == kJoinMarker) {
      // 이스케이프된 줄바꿈: 앞의 공백은 내용으로 보존, 행만 마감
      out += line;
      line.clear();
      raw_ws = 0;
      joined = true;
      at_line_start = true;
      continue;
    }
    if (at_line_start && raw_white) {
      continue;  // 행 선두 원시 공백은 들여쓰기 — 제거
    }
    at_line_start = false;
    flush_sep();
    if (raw_white) {
      raw_ws++;
    } else {
      raw_ws = 0;
    }
    AppendUtf8(line, p.cp);
  }
  if (breaks || joined) {
    flush_sep();  // 내용이 줄바꿈으로 끝난 경우(예: "a\n" → "a ")
  }
  out += line;
  return out;
}

/**
 * @brief 블록 스칼라(| / >) 디코드 — 들여쓰기 제거, (폴디드) 접힘, 청킹.
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
  for (const RawLine& rl : raw) {
    std::size_t lead = 0;
    while (lead < rl.text.size() && rl.text[lead] == ' ') {
      lead++;
    }
    const bool all_ws = lead == rl.text.size();
    if (all_ws && rl.text.size() <= ind) {
      lines.push_back({std::string(), rl.had_break});  // 빈 행
      continue;
    }
    if (lead < ind && !all_ws) {
      break;  // 들여쓰기 미달 내용 행 — 후행 주석/청킹 밖
    }
    // 내용 행(공백만이라도 indent 초과분은 내용)
    lines.push_back({EncodeUtf8(rl.text.subspan(ind)), rl.had_break});
  }
  // 마지막 원소가 "빈 행 + 줄바꿈 없음"이면 구간 끝의 자투리 — 내용 아님
  if (!lines.empty() && lines.back().text.empty() && !lines.back().had_break) {
    lines.pop_back();
  }
  // 후행 빈 행 수(t)와 내용 행 수(k)
  std::size_t k = lines.size();
  while (k > 0 && lines[k - 1].text.empty()) {
    k--;
  }
  const std::size_t trailing = lines.size() - k;
  std::string body;
  if (!folded) {
    for (std::size_t i = 0; i < k; i++) {
      body += lines[i].text;
      if (i + 1 < k) {
        body += '\n';
      }
    }
  } else {
    // 폴디드 접힘: 보통 행끼리 이웃하면 공백, 빈 행 k개는 \n k개,
    // 더 들여쓴(공백/탭 시작) 행 경계는 줄바꿈 그대로
    int prev = 0;  // 0=없음, 1=보통, 2=더 들여씀
    std::size_t pending_empties = 0;
    for (std::size_t i = 0; i < k; i++) {
      const std::string& t = lines[i].text;
      if (t.empty()) {
        pending_empties++;
        continue;
      }
      const int kind = (t[0] == ' ' || t[0] == '\t') ? 2 : 1;
      if (prev != 0) {
        if (pending_empties) {
          body.append(pending_empties, '\n');
        } else if (prev == 1 && kind == 1) {
          body += ' ';
        } else {
          body += '\n';
        }
      }
      pending_empties = 0;
      body += t;
      prev = kind;
    }
  }
  // 청킹
  const bool last_had_break = k > 0 && (trailing > 0 || lines[k - 1].had_break);
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
  if (k > 0) {
    if (lines[k - 1].had_break || trailing > 0) {
      body += '\n';
    }
  }
  for (std::size_t i = k; i < lines.size(); i++) {
    if (lines[i].had_break) {
      body += '\n';
    }
  }
  return body;
}

/** @brief 이벤트 구간의 원문을 UTF-8로 뽑는다. */
std::string SpanText(std::span<const std::uint32_t> buf, const Event& e) {
  return EncodeUtf8(buf.subspan(e.begin, e.end - e.begin));
}

/** @brief 조립 중인 컨테이너 하나(매핑은 키/값 교대 상태 포함). */
struct Build {
  Node node;
  Node key;
  bool has_key = false;
  std::string anchor;
};

}  // namespace

ComposeResult Compose(std::span<const std::uint32_t> buf,
                      std::span<const Event> events) {
  ComposeResult r;
  std::vector<Build> stack;
  std::map<std::string, Node> anchors;
  std::string pend_tag;
  std::string pend_anchor;
  Node root;
  bool have_root = false;

  auto fail = [&r](std::string msg) {
    r.ok = false;
    r.error = std::move(msg);
  };
  // 완성된 노드를 부모(컨테이너 또는 문서 루트)에 붙인다
  auto attach = [&](Node&& n, std::string&& anchor) -> bool {
    if (!anchor.empty()) {
      anchors[std::move(anchor)] = n;  // 완성 시점 등록(자기 참조 배제)
    }
    if (stack.empty()) {
      root = std::move(n);
      have_root = true;
      return true;
    }
    Build& b = stack.back();
    if (b.node.kind == Node::Kind::kSequence) {
      b.node.items.push_back(std::move(n));
      return true;
    }
    if (b.node.kind == Node::Kind::kMapping) {
      if (!b.has_key) {
        b.key = std::move(n);
        b.has_key = true;
      } else {
        b.node.pairs.push_back({std::move(b.key), std::move(n)});
        b.has_key = false;
      }
      return true;
    }
    return false;
  };

  for (const Event& e : events) {
    switch (e.kind) {
      case EventKind::kDocStart: {
        have_root = false;
        break;
      }
      case EventKind::kDocEnd: {
        if (!stack.empty()) {
          fail("문서 끝에서 컨테이너가 닫히지 않음");
          return r;
        }
        if (!have_root) {
          Node n;
          n.null = true;
          root = std::move(n);
        }
        r.docs.push_back(std::move(root));
        root = Node{};
        have_root = false;
        break;
      }
      case EventKind::kAnchor: {
        pend_anchor = SpanText(buf, e);
        break;
      }
      case EventKind::kTag: {
        pend_tag = SpanText(buf, e);
        break;
      }
      case EventKind::kScalar: {
        Node n;
        n.kind = Node::Kind::kScalar;
        n.tag = std::move(pend_tag);
        pend_tag.clear();
        const std::span<const std::uint32_t> body =
            buf.subspan(e.begin, e.end - e.begin);
        if (e.style == ScalarStyle::kPlain) {
          n.null = body.empty();
          n.scalar = n.null ? std::string() : DecodeFlowFolded(body, false);
        } else if (e.style == ScalarStyle::kSingleQuoted) {
          n.scalar = DecodeFlowFolded(body, true);
        } else if (e.style == ScalarStyle::kDoubleQuoted) {
          n.scalar = DecodeDouble(body);
        } else {
          n.scalar = DecodeBlock(body, e.indent, e.chomp,
                                 e.style == ScalarStyle::kFolded);
        }
        std::string anchor = std::move(pend_anchor);
        pend_anchor.clear();
        if (!attach(std::move(n), std::move(anchor))) {
          fail("스칼라를 붙일 컨테이너가 없음");
          return r;
        }
        break;
      }
      case EventKind::kAlias: {
        const std::string name = SpanText(buf, e);
        const auto it = anchors.find(name);
        if (it == anchors.end()) {
          fail("미해소 별칭 *" + name);
          return r;
        }
        Node copy = it->second;  // 값 복사로 해소(순환 자연 배제)
        if (!attach(std::move(copy), std::string())) {
          fail("별칭을 붙일 컨테이너가 없음");
          return r;
        }
        break;
      }
      case EventKind::kMapStart:
      case EventKind::kSeqStart: {
        Build b;
        b.node.kind = e.kind == EventKind::kMapStart ? Node::Kind::kMapping
                                                     : Node::Kind::kSequence;
        b.node.tag = std::move(pend_tag);
        pend_tag.clear();
        b.anchor = std::move(pend_anchor);
        pend_anchor.clear();
        stack.push_back(std::move(b));
        break;
      }
      case EventKind::kMapEnd:
      case EventKind::kSeqEnd: {
        if (stack.empty()) {
          fail("여는 이벤트 없이 컨테이너가 닫힘");
          return r;
        }
        Build b = std::move(stack.back());
        stack.pop_back();
        if (b.has_key) {
          fail("매핑의 키에 대응하는 값이 없음");
          return r;
        }
        if (!attach(std::move(b.node), std::move(b.anchor))) {
          fail("컨테이너를 붙일 부모가 없음");
          return r;
        }
        break;
      }
    }
  }
  if (!stack.empty()) {
    fail("스트림 끝에서 컨테이너가 닫히지 않음");
    return r;
  }
  r.ok = true;
  return r;
}

}  // namespace bedrock::archive::yaml
