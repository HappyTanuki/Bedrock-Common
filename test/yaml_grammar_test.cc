/**
 * @file yaml_grammar_test.cc
 * @brief YAML 문법(Grammar)의 인식·이벤트 방출·진단 단위 테스트.
 *
 * 인식: 유효한 스트림은 버퍼 전체가 매치되어야 하고, 무효한 스트림은
 * 도중에 매치가 끊겨야 한다(부분 매치).
 *
 * 이벤트: 이벤트 버퍼를 연결한 Cursor로 파스해, 방출된 이벤트 열을
 * 압축 문자열로 렌더링한 뒤 기대 문자열과 비교한다. 표기:
 *  - +doc/-doc, +map/-map, +seq/-seq: 문서/매핑/시퀀스 경계
 *  - =p(텍스트): plain 스칼라(빈 텍스트 = e-node/null)
 *  - =s(…)/=d(…): 홑따옴표/겹따옴표 스칼라(본문 구간, 따옴표 제외)
 *  - =l<indent><chomp>(…)/=f<indent><chomp>(…): 리터럴/폴디드 블록
 *    (chomp: s=strip, c=clip, k=keep)
 *  - &이름 및 별표+이름: 앵커/별칭, tag(…): 태그 속성
 * 텍스트 안의 개행은 \n으로 이스케이프해 표기한다.
 *
 * 진단: 무효 입력에서 Diag(최심 도달 오프셋)가 매치 정지 지점보다
 * 깊은 실제 시도 지점을 가리키는지 확인한다.
 */
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/archive/yaml/grammar.h"

namespace {

using bedrock::archive::yaml::ChompKind;
using bedrock::archive::yaml::Cursor;
using bedrock::archive::yaml::Diag;
using bedrock::archive::yaml::Event;
using bedrock::archive::yaml::EventKind;
using bedrock::archive::yaml::Events;
using bedrock::archive::yaml::Grammar;
using bedrock::archive::yaml::ScalarStyle;

/** @brief 버퍼 시작에 놓인 Cursor를 만든다(이벤트/진단은 선택). */
Cursor MakeCursor(const std::vector<std::uint32_t>& buf,
                  Events* events = nullptr, Diag* diag = nullptr) {
  return Cursor{
      std::span<const std::uint32_t>(buf.data(), static_cast<std::size_t>(0)),
      std::span<const std::uint32_t>(buf.data(), buf.size()), events, diag};
}

// ── 인식(l-yaml-stream 수용) ─────────────────────────────────────────

/** @brief 인식 테스트 한 건(이름·입력·기대 결과)을 표현한다. */
struct RecognizerCase {
  const char* name;
  std::u32string_view yaml;  // 전역 생성자/소멸자 회피 — constexpr 구성 가능
  bool valid;                // true면 스트림 전체가 매치되어야 함
};

/**
 * @brief l-yaml-stream 인식기에 대한 테스트 케이스 표.
 *
 * 유효/무효 스트림을 섹션별(기본 스칼라·문서, 블록 컬렉션, 플로우
 * 컬렉션, 인용 스칼라, 블록 스칼라, 지시자/태그/앵커, 문서 구조,
 * 무효 스트림)로 묶어 나열한다. 각 항목의 의미는 항목 옆 주석 참고.
 */
constexpr RecognizerCase kRecognizerCases[] = {
    // ── 기본 스칼라/문서 ─────────────────────────────────────
    {"empty stream", U"", true},
    {"plain scalar no newline", U"a", true},
    {"plain scalar", U"a\n", true},
    {"plain with colon no space is scalar", U"a:b\n", true},
    {"multiline plain", U"a\nb\nc\n", true},
    {"multiline plain with blank line", U"a\n\nb\n", true},
    {"plain first line extra indent", U" a\nb\n", true},
    {"tab separation continuation", U"a\n\tb\n", true},
    {"comment only stream", U"# hello\n# world\n", true},
    {"doc with comments", U"# c\na: 1 # trailing\n# end\n", true},
    {"BOM at start", U"﻿a: 1\n", true},
    {"non-ascii plain", U"한글: 값\n", true},
    {"crlf breaks", U"a: 1\r\nb: 2\r\n", true},

    // ── 블록 컬렉션 ─────────────────────────────────────────
    {"block seq", U"- a\n- b\n", true},
    {"block map", U"a: 1\nb: 2\n", true},
    {"nested block", U"a:\n  b: c\n  d:\n    - 1\n    - 2\n", true},
    {"seq at parent indent (BLOCK-OUT n-1)", U"key:\n- item1\n- item2\n",
     true},
    {"null value then next entry", U"key:\nother: x\n", true},
    {"compact nested seq", U"- - a\n  - b\n", true},
    {"seq of maps", U"- a: 1\n  b: 2\n- c: 3\n", true},
    {"explicit key/value", U"? key\n: value\n", true},
    {"explicit key only", U"? key\n", true},
    {"empty seq entry", U"-\n- b\n", true},
    // 함정 케이스: 연속행 " - b"는 plain "a"의 접힘 연속 → ["a - b"]
    // (PyYAML 동일 동작 확인)
    {"plain continuation looks like seq entry", U"- a\n - b\n", true},

    // ── 플로우 컬렉션 ────────────────────────────────────────
    {"flow map nested", U"{a: 1, b: [x, y], c: {d: e}}\n", true},
    {"flow seq multiline", U"[\n  a,\n  b\n]\n", true},
    {"flow trailing comma", U"[a, b,]\n", true},
    {"flow empty", U"[]\n", true},
    {"flow map empty", U"{}\n", true},
    {"flow pair in seq", U"[a: 1, b: 2]\n", true},
    {"json key adjacent value", U"{\"a\":1}\n", true},
    {"tagged json key in flow map", U"{!!str \"a\": v}\n", true},
    {"tagged json key in flow pair", U"[!!str \"a\": v]\n", true},

    // ── 인용 스칼라 ─────────────────────────────────────────
    {"double quoted with escapes", U"\"a\\tb\\u0041\\\\\"\n", true},
    {"double quoted multiline", U"\"a\n b\"\n", true},
    {"single quoted with quote", U"'it''s'\n", true},
    {"single quoted multiline", U"'a\n b'\n", true},

    // ── 블록 스칼라 ─────────────────────────────────────────
    {"literal", U"|\n  text\n  more\n", true},
    {"literal keep chomping", U"|+\n a\n\n\n", true},
    {"literal explicit indent", U"|2\n  a\n", true},
    {"literal strip", U"|-\n  a\n", true},
    {"folded", U">\n  folded\n  text\n\n  more\n", true},
    {"literal more indented content line", U"|\nx\n y\n", true},
    {"empty literal then next key", U"a: |\nb: c\n", true},
    {"seq of block scalars", U"- |\n  a\n- b\n", true},

    // ── 지시자/태그/앵커 ─────────────────────────────────────
    {"yaml directive", U"%YAML 1.2\n---\ndoc\n", true},
    {"tag directive", U"%TAG !e! tag:example.com,2000:app/\n---\n!e!foo a\n",
     true},
    {"reserved directive", U"%FOO bar baz\n---\na\n", true},
    {"anchor alias", U"a: &x 1\nb: *x\n", true},
    {"tagged scalar", U"!!str text\n", true},
    {"verbatim tag", U"!<tag:a> x\n", true},
    {"alias only doc", U"*a\n", true},
    {"props only value", U"a: !!str\n", true},

    // ── 문서 구조 ───────────────────────────────────────────
    {"explicit doc", U"---\na\n", true},
    {"explicit doc same line", U"--- a\n", true},
    {"explicit empty doc", U"---\n", true},
    {"multi docs with suffix", U"---\na\n...\n---\nb\n", true},
    {"bare then explicit", U"a\n---\nb\n", true},
    {"suffix then bare doc", U"a\n...\nb\n", true},
    {"suffix only", U"...\n", true},
    {"plain looks like marker but is not", U"---foo\n", true},
    {"explicit doc block scalar", U"--- |\n text\n", true},

    // ── 무효 스트림 (부분 매치여야 함) ────────────────────────
    {"mapping value in plain context", U"a: b: c\n", false},
    {"unclosed double quote", U"\"unclosed\n", false},
    {"unclosed single quote", U"'unclosed\n", false},
    {"unclosed flow seq", U"[a, b\n", false},
    {"tab indented mapping", U"a:\n\tb: c\n", false},
    {"leftover after seq", U"- a\nb\n", false},
    {"bad map indent", U"a: 1\n b: 2\n", false},
    {"content after doc at deeper indent", U"a: 1\n  b: 2\n", false},
    {"directive without doc", U"%YAML 1.2\na\n", false},
    {"anchor without name", U"&\n", false},
    {"marker inside double quoted", U"\"a\n--- b\"\n", false},
    {"leading empty line more indented", U"|\n  \n x\n", false},
    {"alias with trailing garbage", U"*a b\n", false},
    {"BOM inside document", U"a﻿b\n", false},
    {"leftover after mapping", U"a: 1\nc\n", false},
};

TEST(YamlRecognizer, MatchesSpecCases) {
  for (const RecognizerCase& c : kRecognizerCases) {
    SCOPED_TRACE(c.name);
    std::vector<std::uint32_t> buf(c.yaml.begin(), c.yaml.end());
    Cursor cur = MakeCursor(buf);
    static_cast<void>(Grammar::Isl_yaml_stream(cur));
    EXPECT_EQ(cur.before.size() == buf.size(), c.valid)
        << "matched " << cur.before.size() << "/" << buf.size()
        << " codepoints";
  }
}

// ── 이벤트 방출 ─────────────────────────────────────────────────────

/** @brief 이벤트 렌더링 테스트 한 건(이름·입력·기대 이벤트 열). */
struct EventCase {
  const char* name;
  std::u32string_view yaml;
  const char* events;
};

/** @brief 이벤트 방출 테스트 케이스 표(입력은 ASCII로 한정). */
constexpr EventCase kEventCases[] = {
    {"empty stream", U"", ""},
    {"block map", U"a: 1\n", "+doc +map =p(a) =p(1) -map -doc"},
    {"block seq", U"- a\n- b\n", "+doc +seq =p(a) =p(b) -seq -doc"},
    {"nested flow", U"{a: [1, 2], \"k\": v}\n",
     "+doc +map =p(a) +seq =p(1) =p(2) -seq =d(k) =p(v) -map -doc"},
    {"null value", U"key:\n", "+doc +map =p(key) =p() -map -doc"},
    {"anchor alias", U"a: &x 1\nb: *x\n",
     "+doc +map =p(a) &x =p(1) =p(b) *x -map -doc"},
    {"binary literal", U"k: !!binary |\n  QUJD\n",
     "+doc +map =p(k) tag(!!binary) =l2c(  QUJD\\n) -map -doc"},
    {"multi docs", U"---\ndoc\n---\nx\n",
     "+doc =p(doc) -doc +doc =p(x) -doc"},
    {"flow pair in seq", U"[a: 1]\n",
     "+doc +seq +map =p(a) =p(1) -map -seq -doc"},
    {"compact nested seq", U"- - a\n  - b\n",
     "+doc +seq +seq =p(a) =p(b) -seq -seq -doc"},
    {"explicit key/value", U"? key\n: value\n",
     "+doc +map =p(key) =p(value) -map -doc"},
    {"seq at parent indent", U"key:\n- item1\n- item2\n",
     "+doc +map =p(key) +seq =p(item1) =p(item2) -seq -map -doc"},
    {"tagged scalar", U"!!str text\n", "+doc tag(!!str) =p(text) -doc"},
    {"props only value", U"a: !!str\n",
     "+doc +map =p(a) tag(!!str) =p() -map -doc"},
    {"json key adjacent value", U"{\"a\":1}\n",
     "+doc +map =d(a) =p(1) -map -doc"},
    {"single quoted multiline", U"'a\n b'\n", "+doc =s(a\\n b) -doc"},
    {"folded", U">\n  folded\n", "+doc =f2c(  folded\\n) -doc"},
    {"explicit empty doc", U"---\n", "+doc =p() -doc"},
    {"seq of maps", U"- a: 1\n  b: 2\n- c: 3\n",
     "+doc +seq +map =p(a) =p(1) =p(b) =p(2) -map +map =p(c) =p(3) -map -seq "
     "-doc"},
    {"literal keep chomping", U"|+\n a\n\n\n", "+doc =l1k( a\\n\\n\\n) -doc"},
    {"tagged json key in flow map", U"{!!str \"a\": v}\n",
     "+doc +map tag(!!str) =d(a) =p(v) -map -doc"},
};

/** @brief [begin,end) 구간을 ASCII 문자열로 렌더(개행은 \\n 표기). */
std::string Text(std::span<const std::uint32_t> buf, std::size_t begin,
                 std::size_t end) {
  std::string out;
  for (std::size_t i = begin; i < end && i < buf.size(); i++) {
    const std::uint32_t c = buf[i];
    if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

/** @brief 청킹 모드의 한 글자 표기. */
char ChompChar(ChompKind k) {
  if (k == ChompKind::kStrip) {
    return 's';
  }
  if (k == ChompKind::kKeep) {
    return 'k';
  }
  return 'c';
}

/** @brief 이벤트 열 전체를 공백 구분 압축 문자열로 렌더링한다. */
std::string Render(std::span<const std::uint32_t> buf,
                   const std::vector<Event>& events) {
  std::string out;
  for (const Event& e : events) {
    if (!out.empty()) {
      out += ' ';
    }
    if (e.kind == EventKind::kDocStart) {
      out += "+doc";
    } else if (e.kind == EventKind::kDocEnd) {
      out += "-doc";
    } else if (e.kind == EventKind::kMapStart) {
      out += "+map";
    } else if (e.kind == EventKind::kMapEnd) {
      out += "-map";
    } else if (e.kind == EventKind::kSeqStart) {
      out += "+seq";
    } else if (e.kind == EventKind::kSeqEnd) {
      out += "-seq";
    } else if (e.kind == EventKind::kScalar) {
      if (e.style == ScalarStyle::kPlain) {
        out += "=p(";
      } else if (e.style == ScalarStyle::kSingleQuoted) {
        out += "=s(";
      } else if (e.style == ScalarStyle::kDoubleQuoted) {
        out += "=d(";
      } else if (e.style == ScalarStyle::kLiteral) {
        out += "=l" + std::to_string(e.indent) + ChompChar(e.chomp) + "(";
      } else {
        out += "=f" + std::to_string(e.indent) + ChompChar(e.chomp) + "(";
      }
      out += Text(buf, e.begin, e.end);
      out += ')';
    } else if (e.kind == EventKind::kAlias) {
      out += '*';
      out += Text(buf, e.begin, e.end);
    } else if (e.kind == EventKind::kAnchor) {
      out += '&';
      out += Text(buf, e.begin, e.end);
    } else {
      out += "tag(";
      out += Text(buf, e.begin, e.end);
      out += ')';
    }
  }
  return out;
}

TEST(YamlEvents, EmitsExpectedSequences) {
  for (const EventCase& c : kEventCases) {
    SCOPED_TRACE(c.name);
    std::vector<std::uint32_t> buf(c.yaml.begin(), c.yaml.end());
    Events events;
    Diag diag;
    Cursor cur = MakeCursor(buf, &events, &diag);
    static_cast<void>(Grammar::Isl_yaml_stream(cur));
    ASSERT_EQ(cur.before.size(), buf.size()) << "전체 매치 실패";
    EXPECT_EQ(Render(buf, events.list), c.events);
  }
}

// ── 진단(최심 도달 추적) ─────────────────────────────────────────────

TEST(YamlDiag, TracksFurthestReach) {
  constexpr std::u32string_view yaml = U"a: [1\nb: 2\n";  // 닫히지 않은 [
  std::vector<std::uint32_t> buf(yaml.begin(), yaml.end());
  Diag diag;
  Cursor cur = MakeCursor(buf, nullptr, &diag);
  static_cast<void>(Grammar::Isl_yaml_stream(cur));
  ASSERT_NE(cur.before.size(), buf.size()) << "무효 입력이 전체 매치됨";
  // '[' 뒤 "1"까지는 시도돼야 하고, 최심 도달 지점은 매치 정지 지점보다
  // 깊어야 진단 가치가 있다
  EXPECT_GE(diag.furthest, 5u);
  EXPECT_GT(diag.furthest, cur.before.size());
  const auto lc = bedrock::archive::yaml::OffsetToLineCol(buf, diag.furthest);
  EXPECT_GE(lc.line, 1u);
  EXPECT_GE(lc.col, 1u);
}

}  // namespace
