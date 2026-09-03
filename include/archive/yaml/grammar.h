/**
 * @file
 * @brief YAML 1.2.2 문법(파서/인식기) 선언.
 *
 * yaml.org 스펙 1.2.2의 BNF 프로덕션 [1]~[211]을 함수/상수로 그대로
 * 옮긴 것이다. 함수명은 IsX_y_z <-> 프로덕션 x-y-z 규칙을 따르며(예:
 * Isc_printable <-> c-printable, Iss_l_block_node <-> s-l+block-node),
 * 각 선언 앞의 `[NN]` 주석이 해당 프로덕션 번호다. 컨텍스트 c(BLOCK-IN
 * /OUT/KEY, FLOW-IN/OUT/KEY)와 chomping 모드 t(STRIP/CLIP/KEEP)는 각각
 * State/ChompingState 패턴의 파생 클래스로 표현한다.
 *
 * 저수준 bool 프로덕션은 입력을 인식하며 Parse는 그 결과를 구문 트리로 만든다.
 * Parse 내부 이벤트는 별도 TU의 BuildEvents visitor가 구문 트리로부터 만든다.
 * 진단(Diag)을 연결하면 가장 깊이 도달한 오프셋을 추적한다.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "archive/yaml/syntax.h"

namespace bedrock::archive::yaml {

/** @brief 파스 진단 정보 — 오류 위치 보고용. */
struct Diag {
  /** @brief 어떤 시도든 가장 깊이 도달했던 코드포인트 오프셋. */
  std::size_t furthest = 0;
};

/**
 * @brief 입력 커서 — 소비된 앞부분(before)과 남은 입력(cps)의 분할점.
 *
 * before와 cps는 같은 연속 버퍼의 인접한 두 조각이다(before.data() +
 * before.size() == cps.data()). 현재 위치는 before.size()이며, before는
 * <start-of-line>([66][79][206])·lookbehind([130]) 판정에, cps는 앞으로
 * 읽을 입력에 쓰인다. 커서 전진은 분할점을 앞으로 옮기는 것이다.
 *
 * Cursor는 입력 위치와 진단 포인터만 가진다. 구문 트리와 백트래킹 상태는
 * Grammar의 ParseState가 불변 persistent frame으로 소유한다. 대안은 cur를
 * 복사한 trial에서 파싱하고 성공할 때만 cur에 commit한다. 실패한 trial은
 * 외부 저장소를 rollback하지 않고 지역 frame과 함께 폐기된다. diag가 있으면
 * Advance가 최심 오프셋을 갱신한다.
 */
struct Cursor {
  std::span<const std::uint32_t> before;
  std::span<const std::uint32_t> cps;
  Diag* diag = nullptr;
};

/** @brief 1 기반 행/열 좌표. */
struct LineCol {
  std::size_t line;
  std::size_t col;
};

/**
 * @brief 코드포인트 오프셋을 1 기반 행/열로 변환한다(CRLF는 한 줄바꿈).
 * @param buf 전체 입력 버퍼.
 * @param offset 변환할 오프셋(버퍼 크기 초과분은 끝으로 클램프).
 */
LineCol OffsetToLineCol(std::span<const std::uint32_t> buf, std::size_t offset);

/**
 * @brief YAML 1.2.2 문법 본체(정적 프로덕션 모음 — 인스턴스 없음).
 *
 * ── 규약 ──
 * [1]~[62]: 순수 문자/토큰 분류기다. 단일 코드포인트를 받는 것은 cp를
 * (0=불일치), 여러 코드포인트를 보는 것은 남은 입력 span(cps) 하나만
 * 받아 매치 길이를(0=불일치) 반환한다. 위치를 소비하지 않으며 커서를
 * 건드리지 않는다. 길이 부족은 span 크기 검사로 처리한다(EOF sentinel
 * 배열을 만들지 않는다).
 *
 * [63]~[211]: 내부적으로 ParseState&(cur)를 받아, 성공하면 cur를 매치
 * 끝으로 전진시키고 true를, 실패하면 false를 반환한다. 실패 시 cur는
 * 임의 지점까지 전진해 있을 수 있으므로, 백트래킹이 필요한 호출자는 원본
 * cur를 수정하지 않고 복사한 trial에서 production을 호출한다. 성공하면
 * cur = trial로 commit하고 실패하면 trial을 폐기한다.
 * 빈 매치(예: s-indent(0)·e-node([106]))는 cur를 전진시키지 않고 true를
 * 반환한다 — 위치는 cur가 들고 있으므로 성공/실패만 bool로 구분한다.
 * ParseState 복사는 입력 위치와 불변 구문 트리 candidate의 소유 핸들을 함께
 * 보존한다. Cursor 자체에는 구문 트리나 이벤트 저장소가 들어가지 않는다.
 */
class Grammar {
 public:
  /** @brief 정적 프로덕션 모음 — 인스턴스를 만들지 않는다. */
  Grammar() = delete;

  static constexpr std::size_t kNoArenaEntry =
      std::numeric_limits<std::size_t>::max();

  struct NodeLink {
    SyntaxNode node;
    std::size_t previous = kNoArenaEntry;
  };

  struct Frame {
    SyntaxKind kind;
    std::size_t begin;
    std::size_t children = kNoArenaEntry;
    std::size_t parent = kNoArenaEntry;
  };

  struct Arena {
    std::vector<NodeLink> node_links;
    std::vector<Frame> frames;
  };

  struct ParseState : Cursor {
    Arena* arena = nullptr;
    std::size_t roots = kNoArenaEntry;
    std::size_t frame = kNoArenaEntry;
  };

  /**
   * @brief persistent node chain을 호출측이 소유할 SyntaxNode 배열로 만든다.
   */
  static std::vector<SyntaxNode> Materialize(const Arena& arena,
                                             std::size_t nodes);

  /**
   * @brief [211] l-yaml-stream — YAML 스트림 전체 production.
   *
   * @details 호출측이 Arena와 ParseState를 구성한 뒤 호출한다. 스트림은 빈
   * match가 가능하므로 production 자체는 항상 성공하며, 전체 입력 수용 여부는
   * 호출측이 `cur.before.size()`와 입력 크기를 비교해 판단한다. 구문 트리가
   * 필요하면 호출 후 roots를 Materialize한다.
   */
  static bool IslYamlStream(ParseState& cur);

  /** @brief 테스트 TU에서 비공개 프로덕션에 접근하기 위한 훅. */
  friend struct GrammarTestAccess;

 private:

  // ── 헬퍼 (스펙 프로덕션 아님) ─────────────────────────────────────
  /**
   * @brief 커서를 repetition_count 코드포인트만큼 전진(분할점을 앞으로 이동).
   * diag가 있으면 최심 도달 오프셋을 갱신한다. repetition_count만큼 전진을
   * 보장하지는 않는다.
   */
  static void Advance(ParseState& cur, std::size_t repetition_count) noexcept;

  /** @brief 완성된 node를 현재 열린 frame의 child 또는 root에 추가한다. */
  static void AddNode(ParseState& cur, SyntaxNode node);
  static void AddEmpty(ParseState& cur);
  /**
   * @brief collection/document의 child를 받을 persistent frame을 연다.
   *
   * child production이 AddNode를 호출할 위치를 지정하므로, child를 가진
   * node에는 시작 시점의 frame이 필요하다.
   */
  static void OpenNode(ParseState& cur, SyntaxKind kind, std::size_t begin);
  /**
   * @brief 현재 frame을 완성된 SyntaxNode로 닫고 AddNode한다.
   *
   * frame의 persistent child chain을 Materialize하고 parent frame으로
   * 돌아간 뒤 AddNode를 호출한다. 따라서 close 후 별도의 AddNode 호출은
   * 필요하지 않다.
   */
  static void CloseNode(ParseState& cur, std::size_t end);

  /**
   * @brief cps의 repetition_index번째 코드포인트(현재 위치 기준
   * repetition_index칸 앞). 범위 밖(EOF)이면 0(0은 c-printable이 아니므로
   * 안전).
   */
  static std::uint32_t At(std::span<const std::uint32_t> cps,
                          std::size_t repetition_index) noexcept;
  /**
   * @brief cps의 subspan을 범위를 체크하며 생성.
   * 범위 밖이라면 {}를 반환한다.
   */
  static std::span<const std::uint32_t> Subspan(
      std::span<const std::uint32_t> cps, std::size_t offset) noexcept;
  /**
   * @brief cps의 subspan을 범위를 체크하며 생성.
   * 범위 밖이라면 {}를 반환한다.
   */
  static std::span<const std::uint32_t> Subspan(
      std::span<const std::uint32_t> cps, std::size_t offset,
      std::size_t count) noexcept;
  /** @brief cps의 repetition_index번째 위치가 버퍼 끝(EOF)인지. */
  static bool AtEnd(std::span<const std::uint32_t> cps,
                    std::size_t repetition_index) noexcept;
  /**
   * @brief <start-of-line>: 버퍼 시작이거나 직전 줄바꿈이 완결된 위치
   * (CRLF 중간 제외). before(lookbehind)와 cps(전방 1자)로 판정한다.
   */
  static bool AtLineStart(const ParseState& cur) noexcept;
  /**
   * @brief 8.1.1.1 블록 스칼라 들여쓰기 자동 감지(cps를 소비하지 않음).
   * 첫 비어있지 않은 행의 들여쓰기로 m = w - n(최소 1). 선행 빈 행이
   * 그보다 더 들여쓰여 있으면 에러(0 반환). 내용 행이 없으면 가장 긴
   * 빈 행 기준.
   */
  static std::ptrdiff_t DetectScalarIndentation(
      std::span<const std::uint32_t> cps, std::ptrdiff_t n);
  /** @brief 헬퍼: 선두 공백 개수(스펙 프로덕션 아님, 소비하지 않음). */
  static std::size_t LeadingSpaces(std::span<const std::uint32_t> cps);

  /** @brief [1] c-printable — 인쇄 가능 문자(허용 유니코드 범위: 8/16/32비트).
   */
  static std::uint32_t IscPrintable(std::uint32_t code_point);
  /** @brief [2] nb-json — JSON 호환 비개행 문자(탭 또는 x20~x10FFFF). */
  static std::uint32_t IsnbJson(std::uint32_t code_point);
  /** @brief [3] c-byte-order-mark: 0xFEFF */
  static constexpr std::uint32_t kCByteOrderMark = 0xFEFF;
  /** @brief [4] c-sequence-entry: '-' */
  static constexpr std::uint32_t kCSequenceEntry = '-';
  /** @brief [5] c-mapping-key: '?' */
  static constexpr std::uint32_t kCMappingKey = '?';
  /** @brief [6] c-mapping-value: ':' */
  static constexpr std::uint32_t kCMappingValue = ':';
  /** @brief [7] c-collect-entry: ',' */
  static constexpr std::uint32_t kCCollectEntry = ',';
  /** @brief [8] c-sequence-start: '[' */
  static constexpr std::uint32_t kCSequenceStart = '[';
  /** @brief [9] c-sequence-end: ']' */
  static constexpr std::uint32_t kCSequenceEnd = ']';
  /** @brief [10] c-mapping-start: '{' */
  static constexpr std::uint32_t kCMappingStart = '{';
  /** @brief [11] c-mapping-end: '}' */
  static constexpr std::uint32_t kCMappingEnd = '}';
  /** @brief [12] c-comment: '#' */
  static constexpr std::uint32_t kCComment = '#';
  /** @brief [13] c-anchor: '&' */
  static constexpr std::uint32_t kCAnchor = '&';
  /** @brief [14] c-alias: '*' */
  static constexpr std::uint32_t kCAlias = '*';
  /** @brief [15] c-tag: '!' */
  static constexpr std::uint32_t kCTag = '!';
  /** @brief [16] c-literal: '|' */
  static constexpr std::uint32_t kCLiteral = '|';
  /** @brief [17] c-folded: '>' */
  static constexpr std::uint32_t kCFolded = '>';
  /** @brief [18] c-single-quote: "'" */
  static constexpr std::uint32_t kCSingleQuote = '\'';
  /** @brief [19] c-double-quote: '"' */
  static constexpr std::uint32_t kCDoubleQuote = '\"';
  /** @brief [20] c-directive: '%' */
  static constexpr std::uint32_t kCDirective = '%';
  /** @brief [21] c-reserved — 예약된 지시자 문자('@' | '`'). */
  static std::uint32_t IscReserved(std::uint32_t code_point);
  /** @brief [22] c-indicator — 모든 지시자 문자([4]~[21]의 합집합). */
  static std::uint32_t IscIndicator(std::uint32_t code_point);
  /** @brief [23] c-flow-indicator — 흐름 컨텍스트 전용 지시자 문자. */
  static std::uint32_t IscFlowIndicator(std::uint32_t code_point);
  /** @brief [24] b-line-feed: LF(x0A) */
  static constexpr std::uint32_t kBLineFeed = '\n';
  /** @brief [25] b-carriage-return: CR(x0D) */
  static constexpr std::uint32_t kBCarriageReturn = '\r';
  /** @brief [26] b-char — 줄 나눔 문자(b-line-feed | b-carriage-return). */
  static std::uint32_t IsbChar(std::uint32_t code_point);
  /** @brief [27] nb-char — 줄 나눔 아닌 문자(c-printable - b-char - BOM). */
  static std::uint32_t IsnbChar(std::uint32_t code_point);
  /** @brief [28] b-break — 줄 나눔(CRLF | CR | LF)의 길이(0=불일치). */
  static std::size_t IsbBreak(std::span<const std::uint32_t> cps);
  /** @brief [29] b-as-line-feed — 줄 나눔을 LF로 해석(b-break와 동일). */
  static std::size_t IsbAsLineFeed(std::span<const std::uint32_t> cps);
  /** @brief [30] b-non-content — 콘텐츠 아닌 줄 나눔(b-break와 동일). */
  static std::size_t IsbNonContent(std::span<const std::uint32_t> cps);
  /** @brief [31] s-space: ' ' */
  static constexpr std::uint32_t kSSpace = ' ';
  /** @brief [32] s-tab: 탭(x09) */
  static constexpr std::uint32_t kSTab = '\t';
  /** @brief [33] s-white — 공백류(s-space | s-tab). */
  static std::uint32_t IssWhite(std::uint32_t code_point);
  /** @brief [34] ns-char — 비-공백류 콘텐츠 문자(nb-char - s-white). */
  static std::uint32_t IsnsChar(std::uint32_t code_point);
  /** @brief [35] ns-dec-digit — 십진 숫자([0-9]). */
  static std::uint32_t IsnsDecDigit(std::uint32_t code_point);
  /** @brief [36] ns-hex-digit — 16진 숫자([0-9A-Fa-f]). */
  static std::uint32_t IsnsHexDigit(std::uint32_t code_point);
  /** @brief [37] ns-ascii-letter — ASCII 알파벳([A-Za-z]). */
  static std::uint32_t IsnsAsciiLetter(std::uint32_t code_point);
  /** @brief [38] ns-word-char — 워드 문자(숫자 | 알파벳 | '-'). */
  static std::uint32_t IsnsWordChar(std::uint32_t code_point);
  /** @brief [39] ns-uri-char — URI 문자(%XX | word-char | 예약기호)의 길이. */
  static std::size_t IsnsUriChar(std::span<const std::uint32_t> cps);
  /** @brief [40] ns-tag-char — (uri-char - '!' - flow-indicator)의 길이. */
  static std::size_t IsnsTagChar(std::span<const std::uint32_t> cps);
  /** @brief [41] c-escape — 이스케이프 시작 문자(역슬래시). */
  static constexpr std::uint32_t kCEscape = '\\';
  /** @brief [42] ns-esc-null: '0' (NUL 이스케이프) */
  static constexpr std::uint32_t kNsEscNull = '0';
  /** @brief [43] ns-esc-bell: 'a' (벨 이스케이프) */
  static constexpr std::uint32_t kNsEscBell = 'a';
  /** @brief [44] ns-esc-backspace: 'b' (백스페이스 이스케이프) */
  static constexpr std::uint32_t kNsEscBackspace = 'b';
  /** @brief [45] ns-esc-horizontal-tab — 't' | 실제 탭 문자. */
  static std::uint32_t IsnsEscHorizontalTab(std::uint32_t code_point);
  /** @brief [46] ns-esc-line-feed: 'n' (LF 이스케이프) */
  static constexpr std::uint32_t kNsEscLineFeed = 'n';
  /** @brief [47] ns-esc-vertical-tab: 'v' (수직 탭 이스케이프) */
  static constexpr std::uint32_t kNsEscVerticalTab = 'v';
  /** @brief [48] ns-esc-form-feed: 'f' (폼 피드 이스케이프) */
  static constexpr std::uint32_t kNsEscFormFeed = 'f';
  /** @brief [49] ns-esc-carriage-return: 'r' (CR 이스케이프) */
  static constexpr std::uint32_t kNsEscCarriageReturn = 'r';
  /** @brief [50] ns-esc-escape: 'e' (ESC 이스케이프) */
  static constexpr std::uint32_t kNsEscEscape = 'e';
  /** @brief [51] ns-esc-space: ' ' (스페이스 이스케이프) */
  static constexpr std::uint32_t kNsEscSpace = ' ';
  /** @brief [52] ns-esc-double-quote: '"' (겹따옴표 이스케이프) */
  static constexpr std::uint32_t kNsEscDoubleQuote = '\"';
  /** @brief [53] ns-esc-slash: '/' (슬래시 이스케이프) */
  static constexpr std::uint32_t kNsEscSlash = '/';
  /** @brief [54] ns-esc-backslash — 역슬래시 이스케이프(역슬래시 자체). */
  static constexpr std::uint32_t kNsEscBackslash = '\\';
  /** @brief [55] ns-esc-next-line: 'N' (NEL 이스케이프) */
  static constexpr std::uint32_t kNsEscNextLine = 'N';
  /** @brief [56] ns-esc-non-breaking-space: '_' (줄바꿈 없는 공백) */
  static constexpr std::uint32_t kNsEscNonBreakingSpace = '_';
  /** @brief [57] ns-esc-line-separator: 'L' (라인 구분자 이스케이프) */
  static constexpr std::uint32_t kNsEscLineSeparator = 'L';
  /** @brief [58] ns-esc-paragraph-separator: 'P' (문단 구분자 이스케이프) */
  static constexpr std::uint32_t kNsEscParagraphSeparator = 'P';
  /** @brief [59] ns-esc-8-bit — 'x' + 16진 2자의 길이(0=불일치). */
  static std::size_t IsnsEsc8Bit(std::span<const std::uint32_t> cps);
  /** @brief [60] ns-esc-16-bit — 'u' + 16진 4자의 길이(0=불일치). */
  static std::size_t IsnsEsc16Bit(std::span<const std::uint32_t> cps);
  /** @brief [61] ns-esc-32-bit — 'U' + 16진 8자의 길이(0=불일치). */
  static std::size_t IsnsEsc32Bit(std::span<const std::uint32_t> cps);
  /**
   * @brief [62] c-ns-esc-char — 전체 이스케이프 시퀀스(c-escape + 위
   * 대안 중 하나)의 길이(0=불일치).
   */
  static std::size_t IscNsEscChar(std::span<const std::uint32_t> cps);
  /**
   * @brief [63] s-indent(n) — 정확히 n칸 들여쓰기.
   *
   * @details 스펙은 `s-indent(n+1) ::= s-space s-indent(n)`으로 재귀
   * 정의하지만, 구현은 n개의 s-space를 먼저 확인한 뒤 한 번에 Advance한다.
   * n <= 0은 재귀의 base case인 빈 match로 취급해 항상 성공하고 전진하지
   * 않는다. 부분 소비 후 실패하지 않으므로 caller의 rollback 부담도 없다.
   */
  static bool IssIndent(ParseState& cur, std::ptrdiff_t n);
  /**
   * @brief [64] s-indent-less-than(n) — n칸 미만 들여쓰기(`m < n`).
   *
   * @details 스펙의 재귀적 `s-space s-indent-less-than(n) | empty`를 선행
   * s-space 개수 m의 측정으로 내린다. m < n일 때 그 m칸을 직접 소비하며,
   * 이는 [63] 호출이 아니라 [64] 자체의 반복 전개다. n <= 0에는 가능한
   * m이 없으므로 실패한다.
   */
  static bool IssIndentLessThan(ParseState& cur, std::ptrdiff_t n);
  /**
   * @brief [65] s-indent-less-or-equal(n) — n칸 이하 들여쓰기(`m <= n`).
   *
   * @details [64]와 마찬가지로 재귀 production을 선행 s-space 개수 m의
   * 측정과 한 번의 Advance로 전개한다. m <= n이면 m칸을 소비하고, n < 0은
   * 유효한 반복 횟수가 없으므로 실패한다.
   */
  static bool IssIndentLessOrEqual(ParseState& cur, std::ptrdiff_t n);
  /**
   * @brief [66] s-separate-in-line — 한 줄 내 분리(s-white+ | 줄 시작).
   * s-white가 없으면 <start-of-line>일 때만 빈 매치로 성공한다.
   */
  static bool IssSeparateInLine(ParseState& cur);
  /** @brief [68] s-block-line-prefix(n) — 블록 줄 prefix(=s-indent(n)). */
  static bool IssBlockLinePrefix(ParseState& cur, std::ptrdiff_t n);
  /** @brief [69] s-flow-line-prefix(n) — 흐름 줄 prefix(들여쓰기+선택 분리).
   */
  static bool IssFlowLinePrefix(ParseState& cur, std::ptrdiff_t n);

  /**
   * @brief 문법 컨텍스트 c(BLOCK-IN/OUT/KEY, FLOW-IN/OUT/KEY)를 표현하는
   * State 패턴의 기반 클래스.
   *
   * c에 따라 실제 동작이 갈리는 프로덕션([67][80][110][121][127][131]
   * [136][201])만 가상 함수이고, c를 그대로 물려주기만 하는 프로덕션은
   * 비가상 메서드로 State에 둔다. 명시적 컨텍스트 전환(예: [74]의
   * FLOW-IN)은 싱글턴 인스턴스를 직접 사용한다.
   */
  struct State {
    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

    /** @brief 다형성 소멸자(vtable 앵커). */
    virtual ~State();
    /**
     * @brief [67] s-line-prefix(n,c) — 컨텍스트별 줄 prefix.
     * BLOCK-IN/OUT은 s-block-line-prefix, FLOW-IN/OUT은
     * s-flow-line-prefix로 갈라지는 지점(파생 State가 구현).
     */
    virtual bool IssLinePrefix(ParseState& cur, std::ptrdiff_t n) const = 0;
    /** @brief [70] l-empty(n,c) — 빈 줄(prefix 또는 부족 들여쓰기+줄바꿈). */
    bool IslEmpty(ParseState& cur, std::ptrdiff_t n) const;

    /**
     * @brief [80] s-separate(n,c) — 컨텍스트별 분리.
     * 기본은 s-separate-lines(n)이고, BLOCK-KEY/FLOW-KEY만
     * s-separate-in-line으로 재정의한다.
     */
    virtual bool IssSeparate(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [110] nb-double-text(n,c) — 컨텍스트별 겹따옴표 내용.
     * FLOW-IN/OUT은 여러 줄(multi-line), BLOCK-KEY/FLOW-KEY는 한 줄
     * (one-line), BLOCK-IN/OUT은 정의 없음(기본 구현은 false).
     */
    virtual bool IsnbDoubleText(ParseState& cur, const std::ptrdiff_t n) const {
      static_cast<void>(cur);
      static_cast<void>(n);
      return false;
    }
    /**
     * @brief [121] nb-single-text(c) — 컨텍스트별 홑따옴표 내용.
     * FLOW-IN/OUT은 여러 줄, BLOCK-KEY/FLOW-KEY는 한 줄, BLOCK-IN/OUT은
     * 정의 없음(기본 구현은 false).
     */
    virtual bool IsnbSingleText(ParseState& cur, const std::ptrdiff_t n) const {
      static_cast<void>(cur);
      static_cast<void>(n);
      return false;
    }
    /**
     * @brief [127] ns-plain-safe(c) — 컨텍스트별 plain 안전 문자.
     * FLOW-OUT/BLOCK-KEY는 safe-out, FLOW-IN/FLOW-KEY는 safe-in을
     * 사용한다(기본 구현은 0).
     */
    [[nodiscard]] virtual std::uint32_t IsnsPlainSafe(
        const std::uint32_t code_point) const {
      static_cast<void>(code_point);
      return 0;
    }
    /**
     * @brief [131] ns-plain(n,c) — 컨텍스트별 plain 스칼라.
     * FLOW-IN/OUT은 여러 줄, BLOCK-KEY/FLOW-KEY는 한 줄, BLOCK-IN/OUT은
     * 정의 없음(기본 구현은 false).
     */
    virtual bool IsnsPlain(ParseState& cur, const std::ptrdiff_t n) const {
      static_cast<void>(cur);
      static_cast<void>(n);
      return false;
    }
    /**
     * @brief 컨텍스트 변환 in-flow(c).
     * FLOW-OUT/FLOW-IN은 FLOW-IN, BLOCK-KEY/FLOW-KEY는 FLOW-KEY로 변환한다.
     * BLOCK-IN/OUT에는 정의되지 않는다(기본 구현은 nullptr).
     */
    [[nodiscard]] virtual const State* InFlow() const { return nullptr; }
    /** @brief [136] in-flow(n,c) — ns-s-flow-seq-entries(n,in-flow(c)). */
    bool IsinFlow(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [201] seq-space(n,c) — BLOCK-OUT/IN에 따른 시퀀스 들여쓰기.
     * BLOCK-OUT은 l+block-sequence(n-1), BLOCK-IN은
     * l+block-sequence(n)이며, 그 외는 정의 없음(기본 구현은 false).
     */
    virtual bool IsseqSpace(ParseState& cur, const std::ptrdiff_t n) const {
      static_cast<void>(cur);
      static_cast<void>(n);
      return false;
    }

    /** @brief [71] b-l-trimmed(n,c) — 줄바꿈 뒤 빈 줄들을 트리밍한 위치. */
    bool IsbLTrimmed(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [73] b-l-folded(n,c) — 접힌 줄바꿈(트리밍 우선, 아니면 스페이스).
     */
    bool IsbLFolded(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [96] c-ns-properties(n,c) — 노드 속성(태그·앵커, 순서 무관 조합).
     */
    bool IscNsProperties(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [109] c-double-quoted(n,c) — 겹따옴표로 감싼 스칼라. */
    bool IscDoubleQuoted(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [120] c-single-quoted(n,c) — 홑따옴표로 감싼 스칼라. */
    bool IscSingleQuoted(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [126] ns-plain-first(c) — plain 스칼라의 첫 문자 규칙.
     *
     * @details [207]의 `l-bare-document ::= s-l+block-node(-1,BLOCK-IN)
     * - c-forbidden`은 전체 문서에 대한 제외 규칙이지만, 여러 줄 scalar는
     * bare document 진입 이후의 행에서도 문서 마커를 만날 수 있다. 따라서
     * 구현은 plain content를 실제로 소비하는 이 경계에서도 복사한 trial로
     * [206]을 검사한다. [206] 자체는 consuming production이며, 원본 cur를
     * 보존하는 책임은 이 호출자가 가진다.
     */
    bool IsnsPlainFirst(ParseState& cur) const;
    /** @brief [130] ns-plain-char(c) — plain 내부 문자(':'/'#' 특례 포함). */
    bool IsnsPlainChar(ParseState& cur) const;
    /** @brief [132] nb-ns-plain-in-line(c) — plain 한 줄 내 공백+문자 시퀀스.
     */
    bool IsnbNsPlainInLine(ParseState& cur) const;
    /** @brief [133] ns-plain-one-line(c) — plain 한 줄 스칼라. */
    bool IsnsPlainOneLine(ParseState& cur) const;
    /**
     * @brief [134] s-ns-plain-next-line(n,c) — plain의 다음 줄 이어짐.
     *
     * @details [207]의 전역 c-forbidden 제외를 여러 줄 plain scalar의 각
     * content line에 적용한다. 새 행 prefix를 소비한 trial에서 [206]을
     * 호출하고, 문서 마커이면 해당 행을 scalar에 commit하지 않는다. 이는
     * [207]의 제외를 content-consuming production에 내린 구현이다.
     */
    bool IssNsPlainNextLine(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [135] ns-plain-multi-line(n,c) — plain 여러 줄 스칼라. */
    bool IsnsPlainMultiLine(ParseState& cur, std::ptrdiff_t n) const;

    /** @brief [137] c-flow-sequence(n,c) — 흐름 시퀀스 '[' ... ']'. */
    bool IscFlowSequence(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [138] ns-s-flow-seq-entries(n,c) — 흐름 시퀀스 항목들(콤마 구분).
     *
     * @details 스펙의 마지막 optional group은
     * `',' s-separate? ns-s-flow-seq-entries?`로 자기 자신을 꼬리 호출한다.
     * 구현은 콤마 개수에 비례한 호출 스택을 피하기 위해 이를 반복문으로
     * 전개한다. 첫 entry와 separator를 먼저 소비한 뒤, 각 반복에서 콤마와
     * separator, 다음 entry를 하나 처리한다. 콤마 뒤의 재귀 production이
     * optional이므로 다음 entry가 없으면 후행 콤마로 성공한다.
     */
    bool IsnsSFlowSeqEntries(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [139] ns-flow-seq-entry(n,c) — 흐름 시퀀스 항목(쌍 | 노드).
     *
     * @details 스펙의 `ns-flow-pair | ns-flow-node`를 그대로 순차
     * backtracking하면, pair의 key 후보로 중첩 node 전체를 파싱한 뒤 ':'가
     * 없어 실패하고 같은 node를 두 번째 대안에서 다시 파싱한다. 이 모호성이
     * 중첩 flow sequence의 각 깊이에서 반복되어 2^depth 시간이 될 수 있다.
     *
     * 구현은 이를 피하도록 left-factor한다. '?' 또는 ':'로 식별 가능한 pair를
     * 먼저 trial에서 시도하고, 나머지는 공유 node 후보를 한 번 파싱한다. 그
     * node 뒤의 `s-separate-in-line?` 다음 문자가 ':'일 때만 원래 위치에서
     * 실제 ns-flow-pair를 실행해 mapping 구문 트리를 만들며, 아니면 이미
     * 파싱한 ns-flow-node를 commit한다. pair인 경우는 구문 트리의 올바른
     * key/value 구조를 만들기 위해 probe 후 production을 다시 실행한다.
     */
    bool IsnsFlowSeqEntry(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [140] c-flow-mapping(n,c) — 흐름 매핑 '{' ... '}'. */
    bool IscFlowMapping(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [141] ns-s-flow-map-entries(n,c) — 흐름 매핑 항목들(콤마 구분).
     *
     * @details [138]과 같은 꼬리 재귀 형태를 반복문으로 전개한다. 첫 map
     * entry 이후 `',' s-separate? entry`를 반복하며, 스펙에서 재귀 호출이
     * optional이므로 다음 entry가 없는 콤마는 후행 콤마로 성공한다. 목적은
     * 항목 수에 비례한 호출 스택을 제거하는 것이다.
     */
    bool IsnsSFlowMapEntries(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [142] ns-flow-map-entry(n,c) — 흐름 매핑 항목(명시적 | 암시적).
     */
    bool IsnsFlowMapEntry(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [143] ns-flow-map-explicit-entry(n,c) — 명시적('?') 매핑 항목. */
    bool IsnsFlowMapExplicitEntry(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [144] ns-flow-map-implicit-entry(n,c) — 암시적 흐름 매핑 항목.
     *
     * @details 스펙의 yaml-key | empty-key | json-key 대안을 단순한 첫 성공으로
     * 고르면, yaml-key가 태그 속성만 포함한 빈 node로 짧게 성공해 뒤의 실제
     * JSON key를 가릴 수 있다(예: `{!!str "a": v}`). 구현은 세 대안을 각각
     * trial에서 실행하고 가장 멀리 소비한 대안을 선택한 뒤, 그 대안만 원본
     * 상태에서 다시 실행한다. 마지막 재실행은 실패 후보의 구문 트리를 남기지
     * 않고 승자 candidate만 materialize하기 위한 것이다.
     */
    bool IsnsFlowMapImplicitEntry(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [145] ns-flow-map-yaml-key-entry(n,c) — YAML 노드가 키인 항목. */
    bool IsnsFlowMapYamlKeyEntry(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [146] c-ns-flow-map-empty-key-entry(n,c) — 빈 키 + 값 항목. */
    bool IscNsFlowMapEmptyKeyEntry(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [147] c-ns-flow-map-separate-value(n,c) — ':' 뒤 분리된 값. */
    bool IscNsFlowMapSeparateValue(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [148] c-ns-flow-map-json-key-entry(n,c) — JSON 노드가 키인 항목.
     */
    bool IscNsFlowMapJsonKeyEntry(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [149] c-ns-flow-map-adjacent-value(n,c) — ':' 뒤 인접한 값. */
    bool IscNsFlowMapAdjacentValue(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [150] ns-flow-pair(n,c) — 흐름 시퀀스 안의 단일 쌍 매핑 항목. */
    bool IsnsFlowPair(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [151] ns-flow-pair-entry(n,c) — yaml-key | 빈 키 | json-key.
     *
     * @details [144]와 동일한 공통-prefix 문제 때문에 세 대안을 trial에서
     * 모두 검사해 최장 match를 선택하고, 승자만 다시 실행해 구문 트리를
     * 만든다. 첫 성공만 택하면 속성만 있는 짧은 YAML key가 완전한 JSON key
     * pair를 가릴 수 있다.
     */
    bool IsnsFlowPairEntry(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [152] ns-flow-pair-yaml-key-entry(n,c) — 암시적 YAML 키의 쌍. */
    bool IsnsFlowPairYamlKeyEntry(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [153] c-ns-flow-pair-json-key-entry(n,c) — 암시적 JSON 키의 쌍.
     */
    bool IscNsFlowPairJsonKeyEntry(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [154] ns-s-implicit-yaml-key(c) — 암시적 YAML 키(최대 1024자).
     *
     * @details n은 단일 행이라 무의미하다(스펙 `n/a`). 스펙의 "At most
     * 1024 characters altogether"를 결과 길이 검사로만 적용하면 긴 key
     * 후보를 끝까지 파싱할 수 있으므로, 구현은 입력 span 자체를 최대 1026자
     * 창으로 제한해 node와 선택적 in-line separator를 파싱한다. 성공하면
     * 실제 소비 길이가 1024 이하인지 다시 검사하고 제한 창의 persistent
     * syntax candidate를 원본 상태에 승계한다.
     */
    bool IsnsSImplicitYamlKey(ParseState& cur) const;
    /**
     * @brief [155] c-s-implicit-json-key(c) — 암시적 JSON 키(최대 1024자).
     *
     * @details [154]와 마찬가지로 최대 길이를 파싱 창과 최종 소비 길이 양쪽에
     * 적용한다. 제한된 span에서 JSON node와 `s-separate-in-line?`을 파싱한
     * 뒤 1024자 이하일 때만 cursor와 persistent syntax candidate를 commit한다.
     */
    bool IscSImplicitJsonKey(ParseState& cur) const;
    /** @brief [156] ns-flow-yaml-content(n,c) — 흐름 YAML 콘텐츠(=plain). */
    bool IsnsFlowYamlContent(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [157] c-flow-json-content(n,c) — 시퀀스/매핑/따옴표 스칼라. */
    bool IscFlowJsonContent(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [158] ns-flow-content(n,c) — 흐름 콘텐츠(yaml | json). */
    bool IsnsFlowContent(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [159] ns-flow-yaml-node(n,c) — 별칭 | 콘텐츠 | 속성+콘텐츠. */
    bool IsnsFlowYamlNode(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [160] c-flow-json-node(n,c) — 선택적 속성 + JSON 콘텐츠. */
    bool IscFlowJsonNode(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [161] ns-flow-node(n,c) — 별칭 | 콘텐츠 | 속성+콘텐츠. */
    bool IsnsFlowNode(ParseState& cur, std::ptrdiff_t n) const;
    /**
     * @brief [185] s-l+block-indented(n,c) — 압축 시퀀스/매핑 | 블록 노드 | 빈
     * 노드.
     *
     * @details 스펙의 m은 호출 인자가 아니라 현재 위치에서 감지하는 지역
     * indentation parameter다. 구현은 선행 공백 수를 m으로 측정하되,
     * `s-indent(m)` production을 별도로 호출해 실제 소비와 측정을 분리한다.
     * compact collection에는 `n + 1 + m`을 전달하고, 실패하면 원래 위치에서
     * block node 또는 `e-node s-l-comments` 대안을 시도한다.
     */
    bool IssLBlockIndented(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [196] s-l+block-node(n,c) — 블록-인-블록 | 흐름-인-블록. */
    bool IssLBlockNode(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [198] s-l+block-in-block(n,c) — 블록 안의 블록(스칼라 | 컬렉션).
     */
    bool IssLBlockInBlock(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [199] s-l+block-scalar(n,c) — 선택적 속성 + 리터럴 | 폴디드. */
    bool IssLBlockScalar(ParseState& cur, std::ptrdiff_t n) const;
    /** @brief [200] s-l+block-collection(n,c) — 선택적 속성 + 시퀀스 | 매핑. */
    bool IssLBlockCollection(ParseState& cur, std::ptrdiff_t n) const;
  };

  /** @brief 컨텍스트 BLOCK-IN(블록 시퀀스 항목의 내용)을 표현하는 State. */
  struct BlockInState final : State {
    /** @brief [67] s-line-prefix — BLOCK-IN: s-block-line-prefix(n). */
    bool IssLinePrefix(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [201] seq-space — BLOCK-IN: l+block-sequence(n)(들여쓰기 그대로).
     */
    bool IsseqSpace(ParseState& cur, std::ptrdiff_t n) const final;
  };
  /** @brief 컨텍스트 BLOCK-OUT(블록 매핑 키/값의 내용)을 표현하는 State. */
  struct BlockOutState final : State {
    /** @brief [67] s-line-prefix — BLOCK-OUT: s-block-line-prefix(n). */
    bool IssLinePrefix(ParseState& cur, std::ptrdiff_t n) const final;
    /**
     * @brief [201] seq-space — BLOCK-OUT: l+block-sequence(n-1).
     * 매핑 값이 시퀀스일 때 한 단계 덜 들여써도 허용하는 규칙.
     */
    bool IsseqSpace(ParseState& cur, std::ptrdiff_t n) const final;
  };
  /**
   * @brief 컨텍스트 BLOCK-KEY(블록 매핑의 암시적 키)를 표현하는 State.
   * 한 줄로 제한된다.
   */
  struct BlockKeyState final : State {
    /** @brief [67] s-line-prefix — BLOCK-KEY: 해당 없음(항상 빈 매치). */
    bool IssLinePrefix(ParseState& cur, const std::ptrdiff_t n) const final {
      static_cast<void>(cur);
      static_cast<void>(n);
      return true;  // key 컨텍스트: 줄 prefix 해당 없음(빈 매치)
    }
    /** @brief [80] s-separate — BLOCK-KEY: s-separate-in-line(한 줄 내 분리만).
     */
    bool IssSeparate(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [110] nb-double-text — BLOCK-KEY: nb-double-one-line(한 줄만). */
    bool IsnbDoubleText(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [121] nb-single-text — BLOCK-KEY: nb-single-one-line(한 줄만). */
    bool IsnbSingleText(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [127] ns-plain-safe — BLOCK-KEY: ns-plain-safe-out. */
    [[nodiscard]] std::uint32_t IsnsPlainSafe(
        std::uint32_t code_point) const final;
    /** @brief [131] ns-plain — BLOCK-KEY: ns-plain-one-line(한 줄만). */
    bool IsnsPlain(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief in-flow(c) -> FLOW-KEY. */
    [[nodiscard]] const State* InFlow() const final;
  };
  /** @brief 컨텍스트 FLOW-IN(흐름 컬렉션 내부)을 표현하는 State. */
  struct FlowInState final : State {
    /** @brief [67] s-line-prefix — FLOW-IN: s-flow-line-prefix. */
    bool IssLinePrefix(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [110] nb-double-text — FLOW-IN: nb-double-multi-line(여러 줄). */
    bool IsnbDoubleText(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [121] nb-single-text — FLOW-IN: nb-single-multi-line(여러 줄). */
    bool IsnbSingleText(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [127] ns-plain-safe — FLOW-IN: ns-plain-safe-in(흐름 지시자
     * 제외). */
    [[nodiscard]] std::uint32_t IsnsPlainSafe(
        std::uint32_t code_point) const final;
    /** @brief [131] ns-plain — FLOW-IN: ns-plain-multi-line(여러 줄). */
    bool IsnsPlain(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief in-flow(c) -> FLOW-IN. */
    [[nodiscard]] const State* InFlow() const final;
  };
  /** @brief 컨텍스트 FLOW-OUT(흐름 노드가 블록 컨텍스트에 놓인 위치)를 표현. */
  struct FlowOutState final : State {
    /** @brief [67] s-line-prefix — FLOW-OUT: s-flow-line-prefix. */
    bool IssLinePrefix(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [110] nb-double-text — FLOW-OUT: nb-double-multi-line(여러 줄).
     */
    bool IsnbDoubleText(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [121] nb-single-text — FLOW-OUT: nb-single-multi-line(여러 줄).
     */
    bool IsnbSingleText(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [127] ns-plain-safe — FLOW-OUT: ns-plain-safe-out. */
    [[nodiscard]] std::uint32_t IsnsPlainSafe(
        std::uint32_t code_point) const final;
    /** @brief [131] ns-plain — FLOW-OUT: ns-plain-multi-line(여러 줄). */
    bool IsnsPlain(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief in-flow(c) -> FLOW-IN. */
    [[nodiscard]] const State* InFlow() const final;
  };
  /**
   * @brief 컨텍스트 FLOW-KEY(흐름 매핑의 암시적 키)를 표현하는 State.
   * 한 줄로 제한된다.
   */
  struct FlowKeyState final : State {
    /** @brief [67] s-line-prefix — FLOW-KEY: 해당 없음(항상 빈 매치). */
    bool IssLinePrefix(ParseState& cur, const std::ptrdiff_t n) const final {
      static_cast<void>(cur);
      static_cast<void>(n);
      return true;  // key 컨텍스트: 줄 prefix 해당 없음(빈 매치)
    }
    /** @brief [80] s-separate — FLOW-KEY: s-separate-in-line(한 줄 내 분리만).
     */
    bool IssSeparate(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [110] nb-double-text — FLOW-KEY: nb-double-one-line(한 줄만). */
    bool IsnbDoubleText(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [121] nb-single-text — FLOW-KEY: nb-single-one-line(한 줄만). */
    bool IsnbSingleText(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief [127] ns-plain-safe — FLOW-KEY: ns-plain-safe-in(흐름 지시자
     * 제외). */
    [[nodiscard]] std::uint32_t IsnsPlainSafe(
        std::uint32_t code_point) const final;
    /** @brief [131] ns-plain — FLOW-KEY: ns-plain-one-line(한 줄만). */
    bool IsnsPlain(ParseState& cur, std::ptrdiff_t n) const final;
    /** @brief in-flow(c) -> FLOW-KEY. */
    [[nodiscard]] const State* InFlow() const final;
  };

  /** @brief BLOCK-IN 컨텍스트 싱글턴 인스턴스. */
  static BlockInState block_in_state;
  /** @brief BLOCK-OUT 컨텍스트 싱글턴 인스턴스. */
  static BlockOutState block_out_state;
  /** @brief BLOCK-KEY 컨텍스트 싱글턴 인스턴스. */
  static BlockKeyState block_key_state;
  /** @brief FLOW-IN 컨텍스트 싱글턴 인스턴스. */
  static FlowInState flow_in_state;
  /** @brief FLOW-OUT 컨텍스트 싱글턴 인스턴스. */
  static FlowOutState flow_out_state;
  /** @brief FLOW-KEY 컨텍스트 싱글턴 인스턴스. */
  static FlowKeyState flow_key_state;

  /**
   * @brief chomping 모드 t([164])를 표현하는 State 패턴의 기반 클래스.
   * 기본 구현이 CLIP이고, STRIP은 [165], KEEP은 [166]만 다시 구현한다.
   */
  struct ChompingState {
    ChompingState() = default;
    ChompingState(const ChompingState&) = delete;
    ChompingState& operator=(const ChompingState&) = delete;
    ChompingState(ChompingState&&) = delete;
    ChompingState& operator=(ChompingState&&) = delete;

    /** @brief 다형성 소멸자(vtable 앵커). */
    virtual ~ChompingState();
    /**
     * @brief [165] b-chomped-last(t) — 기본(CLIP/KEEP).
     * b-as-line-feed | <end-of-input>.
     */
    virtual bool IsbChompedLast(ParseState& cur) const;
    /** @brief [166] l-chomped-empty(n,t) — 기본(STRIP/CLIP): l-strip-empty(n).
     */
    virtual bool IslChompedEmpty(ParseState& cur, std::ptrdiff_t n) const;
  };
  /** @brief chomping 모드 STRIP(트레일링 개행을 모두 제거)을 표현하는 State. */
  struct StripState final : ChompingState {
    /** @brief [165] b-chomped-last — STRIP: b-non-content | <end-of-input>. */
    bool IsbChompedLast(ParseState& cur) const final;
  };
  /** @brief chomping 모드 CLIP(기본값, 마지막 개행 하나만 유지)을 표현하는
   * State.
   */
  struct ClipState final : ChompingState {
    ClipState() = default;
    ClipState(const ClipState&) = delete;
    ClipState& operator=(const ClipState&) = delete;
    ClipState(ClipState&&) = delete;
    ClipState& operator=(ClipState&&) = delete;

    /** @brief vtable 앵커용 소멸자(동작은 전부 기본 구현). */
    ~ClipState() final;
  };
  /** @brief chomping 모드 KEEP(모든 트레일링 개행을 보존)을 표현하는 State. */
  struct KeepState final : ChompingState {
    /** @brief [166] l-chomped-empty — KEEP: l-keep-empty(n). */
    bool IslChompedEmpty(ParseState& cur, std::ptrdiff_t n) const final;
  };

  /** @brief STRIP chomping 모드 싱글턴 인스턴스. */
  static StripState strip_state;
  /** @brief CLIP chomping 모드 싱글턴 인스턴스. */
  static ClipState clip_state;
  /** @brief KEEP chomping 모드 싱글턴 인스턴스. */
  static KeepState keep_state;

  /** @brief [72] b-as-space — 줄 나눔을 스페이스로 해석(b-break와 동일)의 길이.
   */
  static std::size_t IsbAsSpace(std::span<const std::uint32_t> cps);
  /** @brief [74] s-flow-folded(n) — 흐름 컨텍스트의 접힌 줄바꿈. */
  static bool IssFlowFolded(ParseState& cur, std::ptrdiff_t n);
  /** @brief [75] c-nb-comment-text — 주석 텍스트('#' + nb-char*). */
  static bool IscNbCommentText(ParseState& cur);
  /**
   * @brief [76] b-comment — 주석 뒤 줄 나눔 또는 입력 끝.
   * <end-of-input>이면 빈 매치.
   */
  static bool IsbComment(ParseState& cur);
  /** @brief [77] s-b-comment — 선택적 주석 + 줄 나눔/입력 끝. */
  static bool IssBComment(ParseState& cur);
  /** @brief [78] l-comment — 한 줄 전체가 주석(분리+선택적 텍스트+줄 나눔). */
  static bool IslComment(ParseState& cur);
  /** @brief [79] s-l-comments — 주석 줄들의 연속. */
  static bool IssLComments(ParseState& cur);
  /** @brief [81] s-separate-lines(n) — 여러 줄 분리(주석+prefix | 한 줄
   * 분리). */
  static bool IssSeparateLines(ParseState& cur, std::ptrdiff_t n);
  /**
   * @brief [82] l-directive — 지시자 한 줄('%' + YAML/TAG/예약 + 주석).
   *
   * @details 각 YAML/TAG/reserved 대안은 그 뒤의 `s-l-comments`까지 포함해
   * trial에서 검사한다. 이름이 YAML 또는 TAG여도 전용 형식과 줄 끝 전체가
   * 맞지 않으면 해당 trial을 폐기하고 reserved-directive 대안을 시도한다.
   * 이는 production의 전체 대안을 backtrack하는 문법 단계 동작이며, 알려진
   * directive 이름의 형식 오류를 최종 거부할지는 이후 의미론 단계가 맡는다.
   */
  static bool IslDirective(ParseState& cur);
  /** @brief [83] ns-reserved-directive — 예약(알 수 없는) 지시자. */
  static bool IsnsReservedDirective(ParseState& cur);
  /** @brief [84] ns-directive-name — 지시자 이름(ns-char+). */
  static bool IsnsDirectiveName(ParseState& cur);
  /** @brief [85] ns-directive-parameter — 지시자 매개변수(ns-char+). */
  static bool IsnsDirectiveParameter(ParseState& cur);
  /** @brief [86] ns-yaml-directive — "YAML" 버전 지시자. */
  static bool IsnsYamlDirective(ParseState& cur);
  /** @brief [87] ns-yaml-version — 버전 번호(정수.정수). */
  static bool IsnsYamlVersion(ParseState& cur);
  /** @brief [88] ns-tag-directive — "TAG" 지시자(handle + prefix). */
  static bool IsnsTagDirective(ParseState& cur);
  /** @brief [89] c-tag-handle — 태그 핸들(named | secondary | primary). */
  static bool IscTagHandle(ParseState& cur);
  /** @brief [90] c-primary-tag-handle — 기본 태그 핸들 '!'. */
  static bool IscPrimaryTagHandle(ParseState& cur);
  /** @brief [91] c-secondary-tag-handle — 보조 태그 핸들 "!!". */
  static bool IscSecondaryTagHandle(ParseState& cur);
  /** @brief [92] c-named-tag-handle — 명명된 태그 핸들('!'+워드 문자+'!'). */
  static bool IscNamedTagHandle(ParseState& cur);
  /** @brief [93] ns-tag-prefix — 태그 prefix(로컬 | 전역). */
  static bool IsnsTagPrefix(ParseState& cur);
  /** @brief [94] c-ns-local-tag-prefix — 로컬 태그 prefix('!'+uri-char*). */
  static bool IscNsLocalTagPrefix(ParseState& cur);
  /** @brief [95] ns-global-tag-prefix — 전역 태그 prefix. */
  static bool IsnsGlobalTagPrefix(ParseState& cur);
  /** @brief [97] c-ns-tag-property — verbatim | shorthand | non-specific. */
  static bool IscNsTagProperty(ParseState& cur);
  /** @brief [98] c-verbatim-tag — 완전 태그("!&lt;" + uri-char+ + '>'). */
  static bool IscVerbatimTag(ParseState& cur);
  /** @brief [99] c-ns-shorthand-tag — 축약 태그(핸들 + 태그 문자열). */
  static bool IscNsShorthandTag(ParseState& cur);
  /** @brief [100] c-non-specific-tag — 비특정 태그 '!'. */
  static bool IscNonSpecificTag(ParseState& cur);
  /** @brief [101] c-ns-anchor-property — 앵커 속성('&' + 이름). */
  static bool IscNsAnchorProperty(ParseState& cur);
  /** @brief [102] ns-anchor-char — 앵커 이름에 쓸 수 있는 문자. */
  static std::uint32_t IsnsAnchorChar(std::uint32_t code_point);
  /** @brief [103] ns-anchor-name — 앵커 이름(ns-anchor-char+). */
  static bool IsnsAnchorName(ParseState& cur);
  /** @brief [104] c-ns-alias-node — 별칭 노드('*' + 앵커 이름). */
  static bool IscNsAliasNode(ParseState& cur);
  /** @brief [105] e-scalar — 빈 스칼라. 빈 매치로 항상 성공. */
  static bool IseScalar(ParseState& cur);
  /** @brief [106] e-node — 빈 노드(e-scalar와 동일). */
  static bool IseNode(ParseState& cur);
  /**
   * @brief [107] nb-double-char — 겹따옴표 내 비공백 문자.
   * 이스케이프 포함 매치 길이 반환(0 = 불일치).
   */
  static std::size_t IsnbDoubleChar(std::span<const std::uint32_t> cps);
  /** @brief [108] ns-double-char — 겹따옴표 내 비-공백류 문자의 길이. */
  static std::size_t IsnsDoubleChar(std::span<const std::uint32_t> cps);
  /** @brief [111] nb-double-one-line — 겹따옴표 한 줄 내용. */
  static bool IsnbDoubleOneLine(ParseState& cur);
  /** @brief [112] s-double-escaped(n) — 겹따옴표 내 이스케이프된 줄바꿈. */
  static bool IssDoubleEscaped(ParseState& cur, std::ptrdiff_t n);
  /** @brief [113] s-double-break(n) — 겹따옴표 내 줄바꿈(이스케이프 | 접힘). */
  static bool IssDoubleBreak(ParseState& cur, std::ptrdiff_t n);
  /** @brief [114] nb-ns-double-in-line — 한 줄 내 공백+문자 시퀀스. */
  static bool IsnbNsDoubleInLine(ParseState& cur);
  /**
   * @brief [115] s-double-next-line(n) — 겹따옴표의 다음 줄 이어짐.
   *
   * @details 스펙은 `s-double-next-line`을 자기 재귀로 정의하고 각 단계 끝에
   * `s-double-next-line | s-white*` 대안을 둔다. 구현은 긴 여러 줄 scalar가
   * 행 수만큼 호출 스택을 쓰지 않도록 이를 반복형 상태 기계로 전개한다.
   * `work`는 다음 재귀 단계의 시작, `res`는 현재까지 유효한 `s-white*`
   * fallback 끝을 보존하며, 더 깊은 단계가 실패하면 마지막 res를 commit한다.
   * 또한 [207]의 전역 제외를 각 새 content line에 적용해 c-forbidden 행을
   * quoted scalar가 문서 내용으로 소비하지 않게 한다.
   */
  static bool IssDoubleNextLine(ParseState& cur, std::ptrdiff_t n);
  /** @brief [116] nb-double-multi-line(n) — 겹따옴표 여러 줄 내용. */
  static bool IsnbDoubleMultiLine(ParseState& cur, std::ptrdiff_t n);
  /** @brief [117] c-quoted-quote — 이스케이프된 홑따옴표("''")의 길이. */
  static std::size_t IscQuotedQuote(std::span<const std::uint32_t> cps);
  /** @brief [118] nb-single-char — 홑따옴표 내 비공백 문자의 길이. */
  static std::size_t IsnbSingleChar(std::span<const std::uint32_t> cps);
  /** @brief [119] ns-single-char — 홑따옴표 내 비-공백류 문자의 길이. */
  static std::size_t IsnsSingleChar(std::span<const std::uint32_t> cps);
  /** @brief [122] nb-single-one-line — 홑따옴표 한 줄 내용. */
  static bool IsnbSingleOneLine(ParseState& cur);
  /** @brief [123] nb-ns-single-in-line — 한 줄 내 공백+문자 시퀀스. */
  static bool IsnbNsSingleInLine(ParseState& cur);
  /**
   * @brief [124] s-single-next-line(n) — 홑따옴표의 다음 줄 이어짐.
   *
   * @details [115]와 같은 자기 재귀와 `s-white*` fallback을 반복형 상태
   * 기계로 전개한다. 마지막으로 성공한 fallback 위치를 res에 보존하므로 더
   * 깊은 이어짐이 실패해도 스펙의 대안과 같은 위치에서 성공한다. 새 행마다
   * [207]에서 내려온 c-forbidden 검사를 수행한다.
   */
  static bool IssSingleNextLine(ParseState& cur, std::ptrdiff_t n);
  /** @brief [125] nb-single-multi-line(n) — 홑따옴표 여러 줄 내용. */
  static bool IsnbSingleMultiLine(ParseState& cur, std::ptrdiff_t n);
  /** @brief [128] ns-plain-safe-out — 블록/명시 키의 plain 안전 문자(=ns-char).
   */
  static std::uint32_t IsnsPlainSafeOut(std::uint32_t code_point);
  /** @brief [129] ns-plain-safe-in — 흐름의 plain 안전 문자(흐름 지시자 제외).
   */
  static std::uint32_t IsnsPlainSafeIn(std::uint32_t code_point);

  /**
   * @brief [162]의 결과.
   * 성공 여부(ok) + 들여쓰기 지시자 m(0 = 생략, 자동 감지) + chomping 상태 t.
   * ok가 true면 커서는 헤더 끝까지 전진해 있다.
   */
  struct BlockHeader {
    bool ok;
    std::ptrdiff_t indent_width;
    const ChompingState* trial;
  };
  /**
   * @brief [162] c-b-block-header(t) — 블록 스칼라 헤더.
   *
   * @details YAML 1.2.2의 [162][163] 문법 표기는 indentation indicator를
   * 필수처럼 보이게 하는 알려진 결함이 있다. 그러나 8.1.1.1 prose는 이를
   * optional이라고 명시하고 YAML 1.2.1의 [163]도 생략을 허용한다. 구현은
   * digit/chomping, chomping/digit, chomping-only, 둘 다 생략을 모두 허용하며
   * 생략된 indentation은 `indent_width == 0`으로 반환해 content에서 자동
   * 감지하게 한다. 따라서 이 함수의 제어 흐름은 1.2.2 표의 RHS보다 prose와
   * 이전 정정 문법을 따른다.
   */
  static BlockHeader IscBBlockHeader(ParseState& cur);
  /** @brief [163] c-indentation-indicator — '1'~'9'면 cp, 아니면 0. */
  static std::uint32_t IscIndentationIndicator(std::uint32_t code_point);
  /**
   * @brief [164] c-chomping-indicator(t) — '-' | '+' | 생략(=CLIP).
   * 항상 성공(생략은 빈 매치)하며, 소비한 만큼 커서를 전진시키고 t를 반환한다.
   */
  static const ChompingState* IscChompingIndicator(ParseState& cur);
  /** @brief chomping 상태 싱글턴 -> ChompKind 열거값 변환(이벤트용). */
  static ChompKind ChompKindOf(const ChompingState* trial);
  /** @brief [167] l-strip-empty(n) — STRIP/CLIP의 후행 빈 줄(버림). */
  static bool IslStripEmpty(ParseState& cur, std::ptrdiff_t n);
  /** @brief [168] l-keep-empty(n) — KEEP의 후행 빈 줄(보존). */
  static bool IslKeepEmpty(ParseState& cur, std::ptrdiff_t n);
  /** @brief [169] l-trail-comments(n) — 후행 주석 줄들. */
  static bool IslTrailComments(ParseState& cur, std::ptrdiff_t n);
  /** @brief [170] c-l+literal(n) — 리터럴 블록 스칼라 전체('|'+헤더+내용). */
  static bool IscLLiteral(ParseState& cur, std::ptrdiff_t n);
  /**
   * @brief [171] l-nb-literal-text(n) — 리터럴 내용 한 줄(선행 빈 행 포함).
   *
   * @details [207]의 c-forbidden 제외를 block scalar content line에도
   * 적용한다. 들여쓰기 뒤 실제 내용을 소비하기 전에 [206]을 trial에서
   * 검사해 문서 마커 행이면 literal scalar를 그 앞에서 끝낸다.
   */
  static bool IslNbLiteralText(ParseState& cur, std::ptrdiff_t n);
  /** @brief [172] b-nb-literal-next(n) — 리터럴 다음 줄로의 이어짐. */
  static bool IsbNbLiteralNext(ParseState& cur, std::ptrdiff_t n);
  /** @brief [173] l-literal-content(n,t) — 리터럴 전체 내용(텍스트+chomping).
   */
  static bool IslLiteralContent(ParseState& cur, std::ptrdiff_t n,
                                const ChompingState& trial);
  /** @brief [174] c-l+folded(n) — 폴디드 블록 스칼라 전체('>'+헤더+내용). */
  static bool IscLFolded(ParseState& cur, std::ptrdiff_t n);
  /**
   * @brief [175] s-nb-folded-text(n) — 폴디드 내용 한
   * 줄(들여쓰기+첫문자+본문).
   *
   * @details [171]과 마찬가지로 [207]의 전역 c-forbidden 제외를 실제 folded
   * content line 경계에 내린다. consuming [206]은 복사한 trial에서만
   * 실행하므로 금지 행을 인식해도 committed cursor는 움직이지 않는다.
   */
  static bool IssNbFoldedText(ParseState& cur, std::ptrdiff_t n);
  /** @brief [176] l-nb-folded-lines(n) — 폴디드 줄들의 연속(접힌 줄바꿈). */
  static bool IslNbFoldedLines(ParseState& cur, std::ptrdiff_t n);
  /** @brief [177] s-nb-spaced-text(n) — 들여쓰기 뒤 공백으로 시작하는 폴디드
   * 줄. */
  static bool IssNbSpacedText(ParseState& cur, std::ptrdiff_t n);
  /** @brief [178] b-l-spaced(n) — spaced 줄 뒤 줄바꿈(보존, 접히지 않음). */
  static bool IsbLSpaced(ParseState& cur, std::ptrdiff_t n);
  /** @brief [179] l-nb-spaced-lines(n) — spaced 줄들의 연속. */
  static bool IslNbSpacedLines(ParseState& cur, std::ptrdiff_t n);
  /** @brief [180] l-nb-same-lines(n) — 같은 들여쓰기의 줄들(folded | spaced).
   */
  static bool IslNbSameLines(ParseState& cur, std::ptrdiff_t n);
  /** @brief [181] l-nb-diff-lines(n) — 서로 다른 들여쓰기 그룹들의 연속. */
  static bool IslNbDiffLines(ParseState& cur, std::ptrdiff_t n);
  /** @brief [182] l-folded-content(n,t) — 폴디드 전체 내용(줄들+chomping). */
  static bool IslFoldedContent(ParseState& cur, std::ptrdiff_t n,
                               const ChompingState& trial);
  /**
   * @brief [183] l+block-sequence(n) — 블록 시퀀스(항목들의 연속).
   *
   * @details 스펙의 m은 호출자가 전달하지 않는 auto-detected indentation
   * parameter다. 구현은 첫 항목 행의 선행 공백을 `entry_indent`로 측정하고
   * `m = entry_indent - (n + 1)`로 복원한다. m >= 0이어야 하며, 이후 모든
   * 항목에서 고정된 entry_indent를 `s-indent`와 [184]에 사용한다. 이 사전
   * 측정은 단순 최적화가 아니라 중첩 collection이 부모보다 얕은 항목을
   * 받아들이지 않도록 스펙의 고정 m을 구체화한 것이다.
   */
  static bool IslBlockSequence(ParseState& cur, std::ptrdiff_t n);
  /** @brief [184] c-l-block-seq-entry(n) — 블록 시퀀스 항목('-'+들여쓰기 노드).
   */
  static bool IscLBlockSeqEntry(ParseState& cur, std::ptrdiff_t n);
  /** @brief [186] ns-l-compact-sequence(n) — 한 줄에 이어지는 압축 시퀀스. */
  static bool IsnsLCompactSequence(ParseState& cur, std::ptrdiff_t n);
  /**
   * @brief [187] l+block-mapping(n) — 블록 매핑(항목들의 연속).
   *
   * @details [183]과 같은 방식으로 첫 mapping entry의 선행 공백에서
   * `entry_indent`를 측정하고 `m = entry_indent - (n + 1)`을 계산한다.
   * 계산된 들여쓰기는 mapping 전체에서 고정해 각 [188] entry 앞에
   * `s-indent(entry_indent)`를 적용한다.
   */
  static bool IslBlockMapping(ParseState& cur, std::ptrdiff_t n);
  /** @brief [188] ns-l-block-map-entry(n) — 블록 매핑 항목(명시적 | 암시적). */
  static bool IsnsLBlockMapEntry(ParseState& cur, std::ptrdiff_t n);
  /** @brief [189] c-l-block-map-explicit-entry(n) — 명시적('?') 매핑 항목. */
  static bool IscLBlockMapExplicitEntry(ParseState& cur, std::ptrdiff_t n);
  /** @brief [190] c-l-block-map-explicit-key(n) — 명시적 블록 매핑 키. */
  static bool IscLBlockMapExplicitKey(ParseState& cur, std::ptrdiff_t n);
  /** @brief [191] l-block-map-explicit-value(n) — 명시적 값(':'+들여쓰기 노드).
   */
  static bool IslBlockMapExplicitValue(ParseState& cur, std::ptrdiff_t n);
  /** @brief [192] ns-l-block-map-implicit-entry(n) — 암시적 항목(키+값). */
  static bool IsnsLBlockMapImplicitEntry(ParseState& cur, std::ptrdiff_t n);
  /** @brief [193] ns-s-block-map-implicit-key — 암시적 키(JSON | YAML). */
  static bool IsnsSBlockMapImplicitKey(ParseState& cur);
  /** @brief [194] c-l-block-map-implicit-value(n) — 암시적 값(':'+노드|빈값).
   */
  static bool IscLBlockMapImplicitValue(ParseState& cur, std::ptrdiff_t n);
  /** @brief [195] ns-l-compact-mapping(n) — 한 줄에 이어지는 압축 매핑. */
  static bool IsnsLCompactMapping(ParseState& cur, std::ptrdiff_t n);
  /** @brief [197] s-l+flow-in-block(n) — 블록 안의 흐름 노드. */
  static bool IssLFlowInBlock(ParseState& cur, std::ptrdiff_t n);
  /** @brief [202] l-document-prefix — 문서 prefix(BOM? + 주석*). */
  static bool IslDocumentPrefix(ParseState& cur);
  /** @brief [203] c-directives-end — 지시자 종료/문서 시작 표식 "---". */
  static bool IscDirectivesEnd(ParseState& cur);
  /**
   * @brief [204] c-document-end — 문서 종료 표식 "...".
   * "..." 뒤가 비공백 문자면 실패한다(스펙 주석).
   */
  static bool IscDocumentEnd(ParseState& cur);
  /** @brief [205] l-document-suffix — 문서 서픽스("..."+주석들). */
  static bool IslDocumentSuffix(ParseState& cur);
  /**
   * @brief [206] c-forbidden — 행 시작의 문서 마커와 뒤따르는 구분 문자.
   *
   * @details 이 함수는 read-only predicate가 아니라 스펙 production이다.
   * 성공하면 `c-directives-end | c-document-end`와 뒤따르는
   * `b-char | s-white`를 실제로 소비하며, end-of-input 대안만 추가 소비가
   * 없다. c-forbidden 여부만 확인해야 하는 호출자는 ParseState trial을
   * 복사해 이 production을 실행하고 trial을 폐기한다.
   */
  static bool IscForbidden(ParseState& cur);
  /**
   * @brief [207] l-bare-document — bare document(마커 없는 문서).
   *
   * @details 스펙의 `s-l+block-node(-1,BLOCK-IN) - c-forbidden`을 진입점에서
   * 직접 검사한다. 그러나 이 검사만으로는 여러 줄 scalar가 이후 행의
   * `---`/`...`를 content로 소비하는 것을 막을 수 없다. 따라서 같은 전역
   * 제외를 plain [126][134], double-quoted [115], single-quoted [124],
   * literal [171], folded [175]의 실제 content-line 경계에도 내린다. 모든
   * 지점은 consuming [206]을 caller-owned trial에서 probe해 원본을 보존한다.
   */
  static bool IslBareDocument(ParseState& cur);
  /** @brief [208] l-explicit-document — 명시적("---"로 시작하는) 문서. */
  static bool IslExplicitDocument(ParseState& cur);
  /** @brief [209] l-directive-document — 지시자가 있는 문서. */
  static bool IslDirectiveDocument(ParseState& cur);
  /** @brief [210] l-any-document — 임의 종류의 문서. */
  static bool IslAnyDocument(ParseState& cur);
};

}  // namespace bedrock::archive::yaml
