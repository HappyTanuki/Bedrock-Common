/**
 * @file
 * @brief YAML 1.2.2 문법(파서/인식기) 선언.
 *
 * yaml.org 스펙 1.2.2의 BNF 프로덕션 [1]~[211]을 함수/상수로 그대로
 * 옮긴 것이다. 함수명은 IsX_y_z ↔ 프로덕션 x-y-z 규칙을 따르며(예:
 * Isc_printable ↔ c-printable, Iss_l_block_node ↔ s-l+block-node),
 * 각 선언 앞의 `[NN]` 주석이 해당 프로덕션 번호다. 컨텍스트 c(BLOCK-IN
 * /OUT/KEY, FLOW-IN/OUT/KEY)와 청킹 모드 t(STRIP/CLIP/KEEP)는 각각
 * State/ChompingState 패턴의 파생 클래스로 표현한다.
 *
 * Cursor에 이벤트 버퍼(Events)를 연결하면 매치 과정에서 노드 이벤트
 * (스칼라/시퀀스/매핑/앵커/태그/별칭/문서 경계)를 방출하는 파서로,
 * 연결하지 않으면(기본) 순수 문법 인식기로 동작한다. 진단(Diag)을
 * 연결하면 가장 깊이 도달한 오프셋을 추적해 오류 위치 보고에 쓴다.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bedrock::archive::yaml {

/** @brief 노드 이벤트의 종류(스펙 3.1의 Serialization 모델에 대응). */
enum class EventKind : std::uint8_t {
  /** @brief 문서 시작. */
  kDocStart,
  /** @brief 문서 끝. */
  kDocEnd,
  /** @brief 매핑 시작(블록/플로우/단일 쌍 공통). */
  kMapStart,
  /** @brief 매핑 끝. */
  kMapEnd,
  /** @brief 시퀀스 시작(블록/플로우 공통). */
  kSeqStart,
  /** @brief 시퀀스 끝. */
  kSeqEnd,
  /** @brief 스칼라 하나. [begin,end)이 본문(따옴표 제외) 구간. */
  kScalar,
  /** @brief 별칭(*name). [begin,end)이 앵커 이름 구간. */
  kAlias,
  /** @brief 앵커 속성(&name). 다음 노드 이벤트에 붙는다. */
  kAnchor,
  /** @brief 태그 속성(!!str 등 원문 그대로). 다음 노드 이벤트에 붙는다. */
  kTag,
};

/** @brief 스칼라 표기 스타일(값 디코드 방식을 결정). */
enum class ScalarStyle : std::uint8_t {
  /** @brief plain(따옴표 없음). 빈 구간이면 null(e-node). */
  kPlain,
  /** @brief 홑따옴표('') 스칼라. */
  kSingleQuoted,
  /** @brief 겹따옴표("") 스칼라(이스케이프 지원). */
  kDoubleQuoted,
  /** @brief 리터럴 블록(|) 스칼라. */
  kLiteral,
  /** @brief 폴디드 블록(>) 스칼라. */
  kFolded,
};

/** @brief 블록 스칼라의 청킹 모드([164] c-chomping-indicator). */
enum class ChompKind : std::uint8_t {
  /** @brief '-': 트레일링 개행 전부 제거. */
  kStrip,
  /** @brief 생략(기본): 마지막 개행 하나만 유지. */
  kClip,
  /** @brief '+': 트레일링 개행 전부 보존. */
  kKeep,
};

/**
 * @brief 파스 중 방출되는 노드 이벤트 하나.
 *
 * begin/end는 입력 버퍼의 코드포인트 오프셋 [begin,end)이다.
 * kScalar에서는 본문 구간(따옴표 제외, 블록 스칼라는 헤더 뒤부터),
 * kAlias/kAnchor는 앵커 이름, kTag는 태그 원문 전체 구간이다.
 * indent/chomp는 kLiteral/kFolded 스칼라의 디코드에만 쓰인다
 * (indent = 내용의 유효 들여쓰기 n+m).
 */
struct Event {
  EventKind kind;
  ScalarStyle style = ScalarStyle::kPlain;
  ChompKind chomp = ChompKind::kClip;
  std::ptrdiff_t indent = 0;
  std::size_t begin = 0;
  std::size_t end = 0;
};

/**
 * @brief 이벤트 수집 버퍼.
 *
 * Grammar::Isl_yaml_stream이 반환하는 시점에 list는 확정된(살아남은)
 * 이벤트만 담도록 잘려 있다. 도중에는 백트래킹으로 폐기될 이벤트가
 * 꼬리에 남아 있을 수 있으므로 중간에 읽지 않는다.
 */
struct Events {
  std::vector<Event> list;
};

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
 * events가 연결되어 있으면 매처가 노드 이벤트를 방출한다. event_len은
 * "이 커서 상태에서 확정된 이벤트 수"로, 커서 checkpoint 복원(cur = cp)이
 * 이벤트까지 함께 되감는 것을 보장한다(다음 방출이 죽은 꼬리를 자른다).
 * diag가 연결되어 있으면 Advance가 최심 도달 오프셋을 갱신한다.
 */
struct Cursor {
  std::span<const std::uint32_t> before;
  std::span<const std::uint32_t> cps;
  Events* events = nullptr;
  Diag* diag = nullptr;
  std::size_t event_len = 0;
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
 * 건드리지 않는다. 길이 부족은 span 크기 검사로 처리한다(EOF 센티널
 * 배열을 만들지 않는다).
 *
 * [63]~[211]: 매처다. Cursor&(cur)를 입력으로 받아, 성공하면 cur를 매치
 * 끝으로 전진시키고 true를, 실패하면 false를 반환한다. 실패 시 cur는
 * 임의 지점까지 전진해 있을 수 있으므로, 백트래킹이 필요한 호출자가
 * 호출 전에 Cursor를 복사(checkpoint)해 두었다가 실패 시 되돌린다.
 * 빈 매치(예: s-indent(0)·e-node([106]))는 cur를 전진시키지 않고 true를
 * 반환한다 — 위치는 cur가 들고 있으므로 성공/실패만 bool로 구분한다.
 * 이벤트 방출은 모든 실패 경로가 커서를 복원한다는 위 규약 덕분에
 * 어느 지점에서든 안전하다(복원이 event_len을 함께 되감는다).
 */
class Grammar {
 public:
  /** @brief 정적 프로덕션 모음 — 인스턴스를 만들지 않는다. */
  Grammar() = delete;

  /**
   * @brief [211] l-yaml-stream — YAML 스트림 전체 인식/파스 진입점.
   * 항상 매치에 성공한다(스트림은 빈 매치가 가능하므로 실패하지 않음).
   * 매치 끝은 cur.before.size()로 읽는다. cur.events가 연결되어 있으면
   * 반환 직전에 이벤트 목록을 확정분(event_len)까지로 잘라 마무리한다.
   */
  static bool Isl_yaml_stream(Cursor& cur);

  /** @brief 테스트 TU에서 비공개 프로덕션에 접근하기 위한 훅. */
  friend struct GrammarTestAccess;

 private:
  // ── 헬퍼 (스펙 프로덕션 아님) ─────────────────────────────────────
  /**
   * @brief 커서를 k 코드포인트만큼 전진(분할점을 앞으로 이동).
   * diag가 있으면 최심 도달 오프셋을 갱신한다.
   */
  static void Advance(Cursor& cur, std::size_t k);
  /**
   * @brief 이벤트를 방출한다(cur.events가 없으면 no-op).
   * 버퍼를 event_len까지 자른 뒤 추가하므로, checkpoint 복원으로
   * event_len이 되감긴 커서에서 방출하면 죽은 분기의 이벤트가
   * 자동으로 폐기된다.
   */
  static void Emit(Cursor& cur, const Event& e);
  /** @brief 빈 스칼라(e-node/e-scalar) 이벤트를 현재 위치에 방출한다. */
  static void EmitEmpty(Cursor& cur);
  /**
   * @brief cps의 k번째 코드포인트(현재 위치 기준 k칸 앞).
   * 범위 밖(EOF)이면 0(0은 c-printable이 아니므로 안전).
   */
  static std::uint32_t At(std::span<const std::uint32_t> cps, std::size_t k);
  /** @brief cps의 k번째 위치가 버퍼 끝(EOF)인지. */
  static bool AtEnd(std::span<const std::uint32_t> cps, std::size_t k);
  /**
   * @brief <start-of-line>: 버퍼 시작이거나 직전 줄바꿈이 완결된 위치
   * (CRLF 중간 제외). before(lookbehind)와 cps(전방 1자)로 판정한다.
   */
  static bool AtLineStart(const Cursor& cur);
  /**
   * @brief 8.1.1.1 블록 스칼라 들여쓰기 자동 감지(cps를 소비하지 않음).
   * 첫 비어있지 않은 행의 들여쓰기로 m = w - n(최소 1). 선행 빈 행이
   * 그보다 더 들여쓰여 있으면 에러(0 반환). 내용 행이 없으면 가장 긴
   * 빈 행 기준.
   */
  static std::ptrdiff_t DetectScalarIndentation(
      std::span<const std::uint32_t> cps, const std::ptrdiff_t n);
  /** @brief 헬퍼: 선두 공백 개수(스펙 프로덕션 아님, 소비하지 않음). */
  static std::size_t LeadingSpaces(std::span<const std::uint32_t> cps);

  /** @brief [1] c-printable — 인쇄 가능 문자(허용 유니코드 범위: 8/16/32비트). */
  static std::uint32_t Isc_printable(const std::uint32_t cp);
  /** @brief [2] nb-json — JSON 호환 비개행 문자(탭 또는 x20~x10FFFF). */
  static std::uint32_t Isnb_json(const std::uint32_t cp);
  /** @brief [3] c-byte-order-mark: 0xFEFF */
  static constexpr std::uint32_t c_byte_order_mark = 0xFEFF;
  /** @brief [4] c-sequence-entry: '-' */
  static constexpr std::uint32_t c_sequence_entry = '-';
  /** @brief [5] c-mapping-key: '?' */
  static constexpr std::uint32_t c_mapping_key = '?';
  /** @brief [6] c-mapping-value: ':' */
  static constexpr std::uint32_t c_mapping_value = ':';
  /** @brief [7] c-collect-entry: ',' */
  static constexpr std::uint32_t c_collect_entry = ',';
  /** @brief [8] c-sequence-start: '[' */
  static constexpr std::uint32_t c_sequence_start = '[';
  /** @brief [9] c-sequence-end: ']' */
  static constexpr std::uint32_t c_sequence_end = ']';
  /** @brief [10] c-mapping-start: '{' */
  static constexpr std::uint32_t c_mapping_start = '{';
  /** @brief [11] c-mapping-end: '}' */
  static constexpr std::uint32_t c_mapping_end = '}';
  /** @brief [12] c-comment: '#' */
  static constexpr std::uint32_t c_comment = '#';
  /** @brief [13] c-anchor: '&' */
  static constexpr std::uint32_t c_anchor = '&';
  /** @brief [14] c-alias: '*' */
  static constexpr std::uint32_t c_alias = '*';
  /** @brief [15] c-tag: '!' */
  static constexpr std::uint32_t c_tag = '!';
  /** @brief [16] c-literal: '|' */
  static constexpr std::uint32_t c_literal = '|';
  /** @brief [17] c-folded: '>' */
  static constexpr std::uint32_t c_folded = '>';
  /** @brief [18] c-single-quote: "'" */
  static constexpr std::uint32_t c_single_quote = '\'';
  /** @brief [19] c-double-quote: '"' */
  static constexpr std::uint32_t c_double_quote = '\"';
  /** @brief [20] c-directive: '%' */
  static constexpr std::uint32_t c_directive = '%';
  /** @brief [21] c-reserved — 예약된 지시자 문자('@' | '`'). */
  static std::uint32_t Isc_reserved(const std::uint32_t cp);
  /** @brief [22] c-indicator — 모든 지시자 문자([4]~[21]의 합집합). */
  static std::uint32_t Isc_indicator(const std::uint32_t cp);
  /** @brief [23] c-flow-indicator — 흐름 컨텍스트 전용 지시자 문자. */
  static std::uint32_t Isc_flow_indicator(const std::uint32_t cp);
  /** @brief [24] b-line-feed: LF(x0A) */
  static constexpr std::uint32_t b_line_feed = '\n';
  /** @brief [25] b-carriage-return: CR(x0D) */
  static constexpr std::uint32_t b_carrige_return = '\r';
  /** @brief [26] b-char — 줄 나눔 문자(b-line-feed | b-carriage-return). */
  static std::uint32_t Isb_char(const std::uint32_t cp);
  /** @brief [27] nb-char — 줄 나눔 아닌 문자(c-printable - b-char - BOM). */
  static std::uint32_t Isnb_char(const std::uint32_t cp);
  /** @brief [28] b-break — 줄 나눔(CRLF | CR | LF)의 길이(0=불일치). */
  static std::size_t Isb_break(std::span<const std::uint32_t> cps);
  /** @brief [29] b-as-line-feed — 줄 나눔을 LF로 해석(b-break와 동일). */
  static std::size_t Isb_as_line_feed(std::span<const std::uint32_t> cps);
  /** @brief [30] b-non-content — 콘텐츠 아닌 줄 나눔(b-break와 동일). */
  static std::size_t Isb_non_content(std::span<const std::uint32_t> cps);
  /** @brief [31] s-space: ' ' */
  static constexpr std::uint32_t s_space = ' ';
  /** @brief [32] s-tab: 탭(x09) */
  static constexpr std::uint32_t s_tab = '\t';
  /** @brief [33] s-white — 공백류(s-space | s-tab). */
  static std::uint32_t Iss_white(const std::uint32_t cp);
  /** @brief [34] ns-char — 비-공백류 콘텐츠 문자(nb-char - s-white). */
  static std::uint32_t Isns_char(const std::uint32_t cp);
  /** @brief [35] ns-dec-digit — 십진 숫자([0-9]). */
  static std::uint32_t Isns_dec_digit(const std::uint32_t cp);
  /** @brief [36] ns-hex-digit — 16진 숫자([0-9A-Fa-f]). */
  static std::uint32_t Isns_hex_digit(const std::uint32_t cp);
  /** @brief [37] ns-ascii-letter — ASCII 알파벳([A-Za-z]). */
  static std::uint32_t Isns_ascii_letter(const std::uint32_t cp);
  /** @brief [38] ns-word-char — 워드 문자(숫자 | 알파벳 | '-'). */
  static std::uint32_t Isns_word_char(const std::uint32_t cp);
  /** @brief [39] ns-uri-char — URI 문자(%XX | word-char | 예약기호)의 길이. */
  static std::size_t Isns_uri_char(std::span<const std::uint32_t> cps);
  /** @brief [40] ns-tag-char — (uri-char - '!' - flow-indicator)의 길이. */
  static std::size_t Isns_tag_char(std::span<const std::uint32_t> cps);
  /** @brief [41] c-escape — 이스케이프 시작 문자(역슬래시). */
  static constexpr std::uint32_t c_escape = '\\';
  /** @brief [42] ns-esc-null: '0' (NUL 이스케이프) */
  static constexpr std::uint32_t ns_esc_null = '0';
  /** @brief [43] ns-esc-bell: 'a' (벨 이스케이프) */
  static constexpr std::uint32_t ns_esc_bell = 'a';
  /** @brief [44] ns-esc-backspace: 'b' (백스페이스 이스케이프) */
  static constexpr std::uint32_t ns_esc_backspace = 'b';
  /** @brief [45] ns-esc-horizontal-tab — 't' | 실제 탭 문자. */
  static std::uint32_t Isns_esc_horizontal_tab(const std::uint32_t cp);
  /** @brief [46] ns-esc-line-feed: 'n' (LF 이스케이프) */
  static constexpr std::uint32_t ns_esc_line_feed = 'n';
  /** @brief [47] ns-esc-vertical-tab: 'v' (수직 탭 이스케이프) */
  static constexpr std::uint32_t ns_esc_vertical_tab = 'v';
  /** @brief [48] ns-esc-form-feed: 'f' (폼 피드 이스케이프) */
  static constexpr std::uint32_t ns_esc_form_feed = 'f';
  /** @brief [49] ns-esc-carriage-return: 'r' (CR 이스케이프) */
  static constexpr std::uint32_t ns_esc_carriage_return = 'r';
  /** @brief [50] ns-esc-escape: 'e' (ESC 이스케이프) */
  static constexpr std::uint32_t ns_esc_escape = 'e';
  /** @brief [51] ns-esc-space: ' ' (스페이스 이스케이프) */
  static constexpr std::uint32_t ns_esc_space = ' ';
  /** @brief [52] ns-esc-double-quote: '"' (겹따옴표 이스케이프) */
  static constexpr std::uint32_t ns_esc_double_quote = '\"';
  /** @brief [53] ns-esc-slash: '/' (슬래시 이스케이프) */
  static constexpr std::uint32_t ns_esc_slash = '/';
  /** @brief [54] ns-esc-backslash — 역슬래시 이스케이프(역슬래시 자체). */
  static constexpr std::uint32_t ns_esc_backslash = '\\';
  /** @brief [55] ns-esc-next-line: 'N' (NEL 이스케이프) */
  static constexpr std::uint32_t ns_esc_next_line = 'N';
  /** @brief [56] ns-esc-non-breaking-space: '_' (줄바꿈 없는 공백) */
  static constexpr std::uint32_t ns_esc_non_breaking_space = '_';
  /** @brief [57] ns-esc-line-separator: 'L' (라인 구분자 이스케이프) */
  static constexpr std::uint32_t ns_esc_line_separator = 'L';
  /** @brief [58] ns-esc-paragraph-separator: 'P' (문단 구분자 이스케이프) */
  static constexpr std::uint32_t ns_esc_paragraph_separator = 'P';
  /** @brief [59] ns-esc-8-bit — 'x' + 16진 2자의 길이(0=불일치). */
  static std::size_t Isns_esc_8_bit(std::span<const std::uint32_t> cps);
  /** @brief [60] ns-esc-16-bit — 'u' + 16진 4자의 길이(0=불일치). */
  static std::size_t Isns_esc_16_bit(std::span<const std::uint32_t> cps);
  /** @brief [61] ns-esc-32-bit — 'U' + 16진 8자의 길이(0=불일치). */
  static std::size_t Isns_esc_32_bit(std::span<const std::uint32_t> cps);
  /**
   * @brief [62] c-ns-esc-char — 전체 이스케이프 시퀀스(c-escape + 위
   * 대안 중 하나)의 길이(0=불일치).
   */
  static std::size_t Isc_ns_esc_char(std::span<const std::uint32_t> cps);
  /**
   * @brief [63] s-indent(n) — 정확히 n칸 들여쓰기.
   * n<=0은 빈 매치(항상 성공, 전진 없음).
   */
  static bool Iss_indent(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [64] s-indent-less-than(n) — n칸 미만 들여쓰기(m<n). */
  static bool Iss_indent_less_than(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [65] s-indent-less-or-equal(n) — n칸 이하 들여쓰기(m<=n). */
  static bool Iss_indent_less_or_equal(Cursor& cur, const std::ptrdiff_t n);
  /**
   * @brief [66] s-separate-in-line — 한 줄 내 분리(s-white+ | 줄 시작).
   * s-white가 없으면 <start-of-line>일 때만 빈 매치로 성공한다.
   */
  static bool Iss_separate_in_line(Cursor& cur);
  /** @brief [68] s-block-line-prefix(n) — 블록 줄 프리픽스(=s-indent(n)). */
  static bool Iss_block_line_prefix(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [69] s-flow-line-prefix(n) — 흐름 줄 프리픽스(들여쓰기+선택 분리). */
  static bool Iss_flow_line_prefix(Cursor& cur, const std::ptrdiff_t n);

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
    /** @brief 다형성 소멸자(vtable 앵커). */
    virtual ~State();
    /**
     * @brief [67] s-line-prefix(n,c) — 컨텍스트별 줄 프리픽스.
     * BLOCK-IN/OUT은 s-block-line-prefix, FLOW-IN/OUT은
     * s-flow-line-prefix로 갈라지는 지점(파생 State가 구현).
     */
    virtual bool Iss_line_prefix(Cursor& cur, const std::ptrdiff_t n) const = 0;
    /** @brief [70] l-empty(n,c) — 빈 줄(프리픽스 또는 부족 들여쓰기+줄바꿈). */
    bool Isl_empty(Cursor& cur, const std::ptrdiff_t n) const;

    /**
     * @brief [80] s-separate(n,c) — 컨텍스트별 분리.
     * 기본은 s-separate-lines(n)이고, BLOCK-KEY/FLOW-KEY만
     * s-separate-in-line으로 재정의한다.
     */
    virtual bool Iss_separate(Cursor& cur, const std::ptrdiff_t n) const;
    /**
     * @brief [110] nb-double-text(n,c) — 컨텍스트별 겹따옴표 내용.
     * FLOW-IN/OUT은 여러 줄(multi-line), BLOCK-KEY/FLOW-KEY는 한 줄
     * (one-line), BLOCK-IN/OUT은 정의 없음(기본 구현은 false).
     */
    virtual bool Isnb_double_text(Cursor& cur, const std::ptrdiff_t n) const {
      static_cast<void>(cur);
      static_cast<void>(n);
      return false;
    }
    /**
     * @brief [121] nb-single-text(c) — 컨텍스트별 홑따옴표 내용.
     * FLOW-IN/OUT은 여러 줄, BLOCK-KEY/FLOW-KEY는 한 줄, BLOCK-IN/OUT은
     * 정의 없음(기본 구현은 false).
     */
    virtual bool Isnb_single_text(Cursor& cur, const std::ptrdiff_t n) const {
      static_cast<void>(cur);
      static_cast<void>(n);
      return false;
    }
    /**
     * @brief [127] ns-plain-safe(c) — 컨텍스트별 plain 안전 문자.
     * FLOW-OUT/BLOCK-KEY는 safe-out, FLOW-IN/FLOW-KEY는 safe-in을
     * 사용한다(기본 구현은 0).
     */
    virtual std::uint32_t Isns_plain_safe(const std::uint32_t cp) const {
      static_cast<void>(cp);
      return 0;
    }
    /**
     * @brief [131] ns-plain(n,c) — 컨텍스트별 plain 스칼라.
     * FLOW-IN/OUT은 여러 줄, BLOCK-KEY/FLOW-KEY는 한 줄, BLOCK-IN/OUT은
     * 정의 없음(기본 구현은 false).
     */
    virtual bool Isns_plain(Cursor& cur, const std::ptrdiff_t n) const {
      static_cast<void>(cur);
      static_cast<void>(n);
      return false;
    }
    /**
     * @brief [136] in-flow(n,c)의 컨텍스트 매핑.
     * FLOW-OUT/FLOW-IN → FLOW-IN, BLOCK-KEY/FLOW-KEY → FLOW-KEY,
     * BLOCK-IN/OUT은 정의 없음(nullptr).
     */
    virtual const State* InFlow() const { return nullptr; }
    /**
     * @brief [201] seq-space(n,c) — BLOCK-OUT/IN에 따른 시퀀스 들여쓰기.
     * BLOCK-OUT은 l+block-sequence(n-1), BLOCK-IN은
     * l+block-sequence(n)이며, 그 외는 정의 없음(기본 구현은 false).
     */
    virtual bool Isseq_space(Cursor& cur, const std::ptrdiff_t n) const {
      static_cast<void>(cur);
      static_cast<void>(n);
      return false;
    }

    /** @brief [71] b-l-trimmed(n,c) — 줄바꿈 뒤 빈 줄들을 트리밍한 위치. */
    bool Isb_l_trimmed(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [73] b-l-folded(n,c) — 접힌 줄바꿈(트리밍 우선, 아니면 스페이스). */
    bool Isb_l_folded(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [96] c-ns-properties(n,c) — 노드 속성(태그·앵커, 순서 무관 조합). */
    bool Isc_ns_properties(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [109] c-double-quoted(n,c) — 겹따옴표로 감싼 스칼라. */
    bool Isc_double_quoted(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [120] c-single-quoted(n,c) — 홑따옴표로 감싼 스칼라. */
    bool Isc_single_quoted(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [126] ns-plain-first(c) — plain 스칼라의 첫 문자 규칙. */
    bool Isns_plain_first(Cursor& cur) const;
    /** @brief [130] ns-plain-char(c) — plain 내부 문자(':'/'#' 특례 포함). */
    bool Isns_plain_char(Cursor& cur) const;
    /** @brief [132] nb-ns-plain-in-line(c) — plain 한 줄 내 공백+문자 시퀀스. */
    bool Isnb_ns_plain_in_line(Cursor& cur) const;
    /** @brief [133] ns-plain-one-line(c) — plain 한 줄 스칼라. */
    bool Isns_plain_one_line(Cursor& cur) const;
    /** @brief [134] s-ns-plain-next-line(n,c) — plain의 다음 줄 이어짐. */
    bool Iss_ns_plain_next_line(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [135] ns-plain-multi-line(n,c) — plain 여러 줄 스칼라. */
    bool Isns_plain_multi_line(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [136] in-flow(n,c) — InFlow()로 매핑한 흐름 시퀀스 항목들. */
    bool Isin_flow(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [137] c-flow-sequence(n,c) — 흐름 시퀀스 '[' ... ']'. */
    bool Isc_flow_sequence(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [138] ns-s-flow-seq-entries(n,c) — 흐름 시퀀스 항목들(콤마 구분). */
    bool Isns_s_flow_seq_entries(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [139] ns-flow-seq-entry(n,c) — 흐름 시퀀스 항목(쌍 | 노드). */
    bool Isns_flow_seq_entry(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [140] c-flow-mapping(n,c) — 흐름 매핑 '{' ... '}'. */
    bool Isc_flow_mapping(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [141] ns-s-flow-map-entries(n,c) — 흐름 매핑 항목들(콤마 구분). */
    bool Isns_s_flow_map_entries(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [142] ns-flow-map-entry(n,c) — 흐름 매핑 항목(명시적 | 암시적). */
    bool Isns_flow_map_entry(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [143] ns-flow-map-explicit-entry(n,c) — 명시적('?') 매핑 항목. */
    bool Isns_flow_map_explicit_entry(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [144] ns-flow-map-implicit-entry(n,c) — 암시적 흐름 매핑 항목. */
    bool Isns_flow_map_implicit_entry(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [145] ns-flow-map-yaml-key-entry(n,c) — YAML 노드가 키인 항목. */
    bool Isns_flow_map_yaml_key_entry(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [146] c-ns-flow-map-empty-key-entry(n,c) — 빈 키 + 값 항목. */
    bool Isc_ns_flow_map_empty_key_entry(Cursor& cur,
                                         const std::ptrdiff_t n) const;
    /** @brief [147] c-ns-flow-map-separate-value(n,c) — ':' 뒤 분리된 값. */
    bool Isc_ns_flow_map_separate_value(Cursor& cur,
                                        const std::ptrdiff_t n) const;
    /** @brief [148] c-ns-flow-map-json-key-entry(n,c) — JSON 노드가 키인 항목. */
    bool Isc_ns_flow_map_json_key_entry(Cursor& cur,
                                        const std::ptrdiff_t n) const;
    /** @brief [149] c-ns-flow-map-adjacent-value(n,c) — ':' 뒤 인접한 값. */
    bool Isc_ns_flow_map_adjacent_value(Cursor& cur,
                                        const std::ptrdiff_t n) const;
    /** @brief [150] ns-flow-pair(n,c) — 흐름 시퀀스 안의 단일 쌍 매핑 항목. */
    bool Isns_flow_pair(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [151] ns-flow-pair-entry(n,c) — yaml-key | 빈 키 | json-key. */
    bool Isns_flow_pair_entry(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [152] ns-flow-pair-yaml-key-entry(n,c) — 암시적 YAML 키의 쌍. */
    bool Isns_flow_pair_yaml_key_entry(Cursor& cur,
                                       const std::ptrdiff_t n) const;
    /** @brief [153] c-ns-flow-pair-json-key-entry(n,c) — 암시적 JSON 키의 쌍. */
    bool Isc_ns_flow_pair_json_key_entry(Cursor& cur,
                                         const std::ptrdiff_t n) const;
    /**
     * @brief [154] ns-s-implicit-yaml-key(c) — 암시적 YAML 키(최대 1024자).
     * n은 단일 행이라 무의미(스펙 'n/a').
     */
    bool Isns_s_implicit_yaml_key(Cursor& cur) const;
    /** @brief [155] c-s-implicit-json-key(c) — 암시적 JSON 키(최대 1024자). */
    bool Isc_s_implicit_json_key(Cursor& cur) const;
    /** @brief [156] ns-flow-yaml-content(n,c) — 흐름 YAML 콘텐츠(=plain). */
    bool Isns_flow_yaml_content(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [157] c-flow-json-content(n,c) — 시퀀스/매핑/따옴표 스칼라. */
    bool Isc_flow_json_content(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [158] ns-flow-content(n,c) — 흐름 콘텐츠(yaml | json). */
    bool Isns_flow_content(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [159] ns-flow-yaml-node(n,c) — 별칭 | 콘텐츠 | 속성+콘텐츠. */
    bool Isns_flow_yaml_node(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [160] c-flow-json-node(n,c) — 선택적 속성 + JSON 콘텐츠. */
    bool Isc_flow_json_node(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [161] ns-flow-node(n,c) — 별칭 | 콘텐츠 | 속성+콘텐츠. */
    bool Isns_flow_node(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [185] s-l+block-indented(n,c) — 압축 시퀀스/매핑 | 블록 노드 | 빈 노드. */
    bool Iss_l_block_indented(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [196] s-l+block-node(n,c) — 블록-인-블록 | 흐름-인-블록. */
    bool Iss_l_block_node(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [198] s-l+block-in-block(n,c) — 블록 안의 블록(스칼라 | 컬렉션). */
    bool Iss_l_block_in_block(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [199] s-l+block-scalar(n,c) — 선택적 속성 + 리터럴 | 폴디드. */
    bool Iss_l_block_scalar(Cursor& cur, const std::ptrdiff_t n) const;
    /** @brief [200] s-l+block-collection(n,c) — 선택적 속성 + 시퀀스 | 매핑. */
    bool Iss_l_block_collection(Cursor& cur, const std::ptrdiff_t n) const;
  };

  /** @brief 컨텍스트 BLOCK-IN(블록 시퀀스 항목의 내용)을 표현하는 State. */
  struct BLOCK_IN_state final : State {
    /** @brief [67] s-line-prefix — BLOCK-IN: s-block-line-prefix(n). */
    bool Iss_line_prefix(Cursor& cur,
                         const std::ptrdiff_t n) const final override;
    /** @brief [201] seq-space — BLOCK-IN: l+block-sequence(n)(들여쓰기 그대로). */
    bool Isseq_space(Cursor& cur,
                     const std::ptrdiff_t n) const final override;
  };
  /** @brief 컨텍스트 BLOCK-OUT(블록 매핑 키/값의 내용)을 표현하는 State. */
  struct BLOCK_OUT_state final : State {
    /** @brief [67] s-line-prefix — BLOCK-OUT: s-block-line-prefix(n). */
    bool Iss_line_prefix(Cursor& cur,
                         const std::ptrdiff_t n) const final override;
    /**
     * @brief [201] seq-space — BLOCK-OUT: l+block-sequence(n-1).
     * 매핑 값이 시퀀스일 때 한 단계 덜 들여써도 허용하는 규칙.
     */
    bool Isseq_space(Cursor& cur,
                     const std::ptrdiff_t n) const final override;
  };
  /**
   * @brief 컨텍스트 BLOCK-KEY(블록 매핑의 암시적 키)를 표현하는 State.
   * 한 줄로 제한된다.
   */
  struct BLOCK_KEY_state final : State {
    /** @brief [67] s-line-prefix — BLOCK-KEY: 해당 없음(항상 빈 매치). */
    bool Iss_line_prefix(Cursor& cur,
                         const std::ptrdiff_t n) const final override {
      static_cast<void>(cur);
      static_cast<void>(n);
      return true;  // key 컨텍스트: 줄 프리픽스 해당 없음(빈 매치)
    }
    /** @brief [80] s-separate — BLOCK-KEY: s-separate-in-line(한 줄 내 분리만). */
    bool Iss_separate(Cursor& cur,
                      const std::ptrdiff_t n) const final override;
    /** @brief [110] nb-double-text — BLOCK-KEY: nb-double-one-line(한 줄만). */
    bool Isnb_double_text(Cursor& cur,
                          const std::ptrdiff_t n) const final override;
    /** @brief [121] nb-single-text — BLOCK-KEY: nb-single-one-line(한 줄만). */
    bool Isnb_single_text(Cursor& cur,
                          const std::ptrdiff_t n) const final override;
    /** @brief [127] ns-plain-safe — BLOCK-KEY: ns-plain-safe-out. */
    std::uint32_t Isns_plain_safe(const std::uint32_t cp) const final override;
    /** @brief [131] ns-plain — BLOCK-KEY: ns-plain-one-line(한 줄만). */
    bool Isns_plain(Cursor& cur,
                    const std::ptrdiff_t n) const final override;
    /** @brief [136] in-flow 컨텍스트 매핑 — BLOCK-KEY → FLOW-KEY. */
    const State* InFlow() const final override;
  };
  /** @brief 컨텍스트 FLOW-IN(흐름 컬렉션 내부)을 표현하는 State. */
  struct FLOW_IN_state final : State {
    /** @brief [67] s-line-prefix — FLOW-IN: s-flow-line-prefix. */
    bool Iss_line_prefix(Cursor& cur,
                         const std::ptrdiff_t n) const final override;
    /** @brief [110] nb-double-text — FLOW-IN: nb-double-multi-line(여러 줄). */
    bool Isnb_double_text(Cursor& cur,
                          const std::ptrdiff_t n) const final override;
    /** @brief [121] nb-single-text — FLOW-IN: nb-single-multi-line(여러 줄). */
    bool Isnb_single_text(Cursor& cur,
                          const std::ptrdiff_t n) const final override;
    /** @brief [127] ns-plain-safe — FLOW-IN: ns-plain-safe-in(흐름 지시자 제외). */
    std::uint32_t Isns_plain_safe(const std::uint32_t cp) const final override;
    /** @brief [131] ns-plain — FLOW-IN: ns-plain-multi-line(여러 줄). */
    bool Isns_plain(Cursor& cur,
                    const std::ptrdiff_t n) const final override;
    /** @brief [136] in-flow 컨텍스트 매핑 — FLOW-IN → FLOW-IN(자기 자신). */
    const State* InFlow() const final override;
  };
  /** @brief 컨텍스트 FLOW-OUT(흐름 노드가 블록 컨텍스트에 놓인 위치)를 표현. */
  struct FLOW_OUT_state final : State {
    /** @brief [67] s-line-prefix — FLOW-OUT: s-flow-line-prefix. */
    bool Iss_line_prefix(Cursor& cur,
                         const std::ptrdiff_t n) const final override;
    /** @brief [110] nb-double-text — FLOW-OUT: nb-double-multi-line(여러 줄). */
    bool Isnb_double_text(Cursor& cur,
                          const std::ptrdiff_t n) const final override;
    /** @brief [121] nb-single-text — FLOW-OUT: nb-single-multi-line(여러 줄). */
    bool Isnb_single_text(Cursor& cur,
                          const std::ptrdiff_t n) const final override;
    /** @brief [127] ns-plain-safe — FLOW-OUT: ns-plain-safe-out. */
    std::uint32_t Isns_plain_safe(const std::uint32_t cp) const final override;
    /** @brief [131] ns-plain — FLOW-OUT: ns-plain-multi-line(여러 줄). */
    bool Isns_plain(Cursor& cur,
                    const std::ptrdiff_t n) const final override;
    /** @brief [136] in-flow 컨텍스트 매핑 — FLOW-OUT → FLOW-IN. */
    const State* InFlow() const final override;
  };
  /**
   * @brief 컨텍스트 FLOW-KEY(흐름 매핑의 암시적 키)를 표현하는 State.
   * 한 줄로 제한된다.
   */
  struct FLOW_KEY_state final : State {
    /** @brief [67] s-line-prefix — FLOW-KEY: 해당 없음(항상 빈 매치). */
    bool Iss_line_prefix(Cursor& cur,
                         const std::ptrdiff_t n) const final override {
      static_cast<void>(cur);
      static_cast<void>(n);
      return true;  // key 컨텍스트: 줄 프리픽스 해당 없음(빈 매치)
    }
    /** @brief [80] s-separate — FLOW-KEY: s-separate-in-line(한 줄 내 분리만). */
    bool Iss_separate(Cursor& cur,
                      const std::ptrdiff_t n) const final override;
    /** @brief [110] nb-double-text — FLOW-KEY: nb-double-one-line(한 줄만). */
    bool Isnb_double_text(Cursor& cur,
                          const std::ptrdiff_t n) const final override;
    /** @brief [121] nb-single-text — FLOW-KEY: nb-single-one-line(한 줄만). */
    bool Isnb_single_text(Cursor& cur,
                          const std::ptrdiff_t n) const final override;
    /** @brief [127] ns-plain-safe — FLOW-KEY: ns-plain-safe-in(흐름 지시자 제외). */
    std::uint32_t Isns_plain_safe(const std::uint32_t cp) const final override;
    /** @brief [131] ns-plain — FLOW-KEY: ns-plain-one-line(한 줄만). */
    bool Isns_plain(Cursor& cur,
                    const std::ptrdiff_t n) const final override;
    /** @brief [136] in-flow 컨텍스트 매핑 — FLOW-KEY → FLOW-KEY(자기 자신). */
    const State* InFlow() const final override;
  };

  /** @brief BLOCK-IN 컨텍스트 싱글턴 인스턴스. */
  static BLOCK_IN_state _block_in_state;
  /** @brief BLOCK-OUT 컨텍스트 싱글턴 인스턴스. */
  static BLOCK_OUT_state _block_out_state;
  /** @brief BLOCK-KEY 컨텍스트 싱글턴 인스턴스. */
  static BLOCK_KEY_state _block_key_state;
  /** @brief FLOW-IN 컨텍스트 싱글턴 인스턴스. */
  static FLOW_IN_state _flow_in_state;
  /** @brief FLOW-OUT 컨텍스트 싱글턴 인스턴스. */
  static FLOW_OUT_state _flow_out_state;
  /** @brief FLOW-KEY 컨텍스트 싱글턴 인스턴스. */
  static FLOW_KEY_state _flow_key_state;

  /**
   * @brief 청킹 모드 t([164])를 표현하는 State 패턴의 기반 클래스.
   * 기본 구현이 CLIP이고, STRIP은 [165], KEEP은 [166]만 다시 구현한다.
   */
  struct ChompingState {
    /** @brief 다형성 소멸자(vtable 앵커). */
    virtual ~ChompingState();
    /**
     * @brief [165] b-chomped-last(t) — 기본(CLIP/KEEP).
     * b-as-line-feed | <end-of-input>.
     */
    virtual bool Isb_chomped_last(Cursor& cur) const;
    /** @brief [166] l-chomped-empty(n,t) — 기본(STRIP/CLIP): l-strip-empty(n). */
    virtual bool Isl_chomped_empty(Cursor& cur, const std::ptrdiff_t n) const;
  };
  /** @brief 청킹 모드 STRIP(트레일링 개행을 모두 제거)을 표현하는 State. */
  struct STRIP_state final : ChompingState {
    /** @brief [165] b-chomped-last — STRIP: b-non-content | <end-of-input>. */
    bool Isb_chomped_last(Cursor& cur) const final override;
  };
  /** @brief 청킹 모드 CLIP(기본값, 마지막 개행 하나만 유지)을 표현하는 State. */
  struct CLIP_state final : ChompingState {
    /** @brief vtable 앵커용 소멸자(동작은 전부 기본 구현). */
    ~CLIP_state() final override;
  };
  /** @brief 청킹 모드 KEEP(모든 트레일링 개행을 보존)을 표현하는 State. */
  struct KEEP_state final : ChompingState {
    /** @brief [166] l-chomped-empty — KEEP: l-keep-empty(n). */
    bool Isl_chomped_empty(Cursor& cur,
                           const std::ptrdiff_t n) const final override;
  };

  /** @brief STRIP 청킹 모드 싱글턴 인스턴스. */
  static STRIP_state _strip_state;
  /** @brief CLIP 청킹 모드 싱글턴 인스턴스. */
  static CLIP_state _clip_state;
  /** @brief KEEP 청킹 모드 싱글턴 인스턴스. */
  static KEEP_state _keep_state;

  /** @brief [72] b-as-space — 줄 나눔을 스페이스로 해석(b-break와 동일)의 길이. */
  static std::size_t Isb_as_space(std::span<const std::uint32_t> cps);
  /** @brief [74] s-flow-folded(n) — 흐름 컨텍스트의 접힌 줄바꿈. */
  static bool Iss_flow_folded(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [75] c-nb-comment-text — 주석 텍스트('#' + nb-char*). */
  static bool Isc_nb_comment_text(Cursor& cur);
  /**
   * @brief [76] b-comment — 주석 뒤 줄 나눔 또는 입력 끝.
   * <end-of-input>이면 빈 매치.
   */
  static bool Isb_comment(Cursor& cur);
  /** @brief [77] s-b-comment — 선택적 주석 + 줄 나눔/입력 끝. */
  static bool Iss_b_comment(Cursor& cur);
  /** @brief [78] l-comment — 한 줄 전체가 주석(분리+선택적 텍스트+줄 나눔). */
  static bool Isl_comment(Cursor& cur);
  /** @brief [79] s-l-comments — 주석 줄들의 연속. */
  static bool Iss_l_comments(Cursor& cur);
  /** @brief [81] s-separate-lines(n) — 여러 줄 분리(주석+프리픽스 | 한 줄 분리). */
  static bool Iss_separate_lines(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [82] l-directive — 지시자 한 줄('%' + YAML/TAG/예약 + 주석). */
  static bool Isl_directive(Cursor& cur);
  /** @brief [83] ns-reserved-directive — 예약(알 수 없는) 지시자. */
  static bool Isns_reserved_directive(Cursor& cur);
  /** @brief [84] ns-directive-name — 지시자 이름(ns-char+). */
  static bool Isns_directive_name(Cursor& cur);
  /** @brief [85] ns-directive-parameter — 지시자 매개변수(ns-char+). */
  static bool Isns_directive_parameter(Cursor& cur);
  /** @brief [86] ns-yaml-directive — "YAML" 버전 지시자. */
  static bool Isns_yaml_directive(Cursor& cur);
  /** @brief [87] ns-yaml-version — 버전 번호(정수.정수). */
  static bool Isns_yaml_version(Cursor& cur);
  /** @brief [88] ns-tag-directive — "TAG" 지시자(핸들 + 프리픽스). */
  static bool Isns_tag_directive(Cursor& cur);
  /** @brief [89] c-tag-handle — 태그 핸들(named | secondary | primary). */
  static bool Isc_tag_handle(Cursor& cur);
  /** @brief [90] c-primary-tag-handle — 기본 태그 핸들 '!'. */
  static bool Isc_primary_tag_handle(Cursor& cur);
  /** @brief [91] c-secondary-tag-handle — 보조 태그 핸들 "!!". */
  static bool Isc_secondary_tag_handle(Cursor& cur);
  /** @brief [92] c-named-tag-handle — 명명된 태그 핸들('!'+워드 문자+'!'). */
  static bool Isc_named_tag_handle(Cursor& cur);
  /** @brief [93] ns-tag-prefix — 태그 프리픽스(로컬 | 전역). */
  static bool Isns_tag_prefix(Cursor& cur);
  /** @brief [94] c-ns-local-tag-prefix — 로컬 태그 프리픽스('!'+uri-char*). */
  static bool Isc_ns_local_tag_prefix(Cursor& cur);
  /** @brief [95] ns-global-tag-prefix — 전역 태그 프리픽스. */
  static bool Isns_global_tag_prefix(Cursor& cur);
  /** @brief [97] c-ns-tag-property — verbatim | shorthand | non-specific. */
  static bool Isc_ns_tag_property(Cursor& cur);
  /** @brief [98] c-verbatim-tag — 완전 태그("!&lt;" + uri-char+ + '>'). */
  static bool Isc_verbatim_tag(Cursor& cur);
  /** @brief [99] c-ns-shorthand-tag — 축약 태그(핸들 + 태그 문자열). */
  static bool Isc_ns_shorthand_tag(Cursor& cur);
  /** @brief [100] c-non-specific-tag — 비특정 태그 '!'. */
  static bool Isc_non_specific_tag(Cursor& cur);
  /** @brief [101] c-ns-anchor-property — 앵커 속성('&' + 이름). */
  static bool Isc_ns_anchor_property(Cursor& cur);
  /** @brief [102] ns-anchor-char — 앵커 이름에 쓸 수 있는 문자. */
  static std::uint32_t Isns_anchor_char(const std::uint32_t cp);
  /** @brief [103] ns-anchor-name — 앵커 이름(ns-anchor-char+). */
  static bool Isns_anchor_name(Cursor& cur);
  /** @brief [104] c-ns-alias-node — 별칭 노드('*' + 앵커 이름). */
  static bool Isc_ns_alias_node(Cursor& cur);
  /** @brief [105] e-scalar — 빈 스칼라. 빈 매치로 항상 성공. */
  static bool Ise_scalar(Cursor& cur);
  /** @brief [106] e-node — 빈 노드(e-scalar와 동일). */
  static bool Ise_node(Cursor& cur);
  /**
   * @brief [107] nb-double-char — 겹따옴표 내 비공백 문자.
   * 이스케이프 포함 매치 길이 반환(0 = 불일치).
   */
  static std::size_t Isnb_double_char(std::span<const std::uint32_t> cps);
  /** @brief [108] ns-double-char — 겹따옴표 내 비-공백류 문자의 길이. */
  static std::size_t Isns_double_char(std::span<const std::uint32_t> cps);
  /** @brief [111] nb-double-one-line — 겹따옴표 한 줄 내용. */
  static bool Isnb_double_one_line(Cursor& cur);
  /** @brief [112] s-double-escaped(n) — 겹따옴표 내 이스케이프된 줄바꿈. */
  static bool Iss_double_escaped(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [113] s-double-break(n) — 겹따옴표 내 줄바꿈(이스케이프 | 접힘). */
  static bool Iss_double_break(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [114] nb-ns-double-in-line — 한 줄 내 공백+문자 시퀀스. */
  static bool Isnb_ns_double_in_line(Cursor& cur);
  /** @brief [115] s-double-next-line(n) — 겹따옴표의 다음 줄 이어짐. */
  static bool Iss_double_next_line(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [116] nb-double-multi-line(n) — 겹따옴표 여러 줄 내용. */
  static bool Isnb_double_multi_line(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [117] c-quoted-quote — 이스케이프된 홑따옴표("''")의 길이. */
  static std::size_t Isc_quoted_quote(std::span<const std::uint32_t> cps);
  /** @brief [118] nb-single-char — 홑따옴표 내 비공백 문자의 길이. */
  static std::size_t Isnb_single_char(std::span<const std::uint32_t> cps);
  /** @brief [119] ns-single-char — 홑따옴표 내 비-공백류 문자의 길이. */
  static std::size_t Isns_single_char(std::span<const std::uint32_t> cps);
  /** @brief [122] nb-single-one-line — 홑따옴표 한 줄 내용. */
  static bool Isnb_single_one_line(Cursor& cur);
  /** @brief [123] nb-ns-single-in-line — 한 줄 내 공백+문자 시퀀스. */
  static bool Isnb_ns_single_in_line(Cursor& cur);
  /** @brief [124] s-single-next-line(n) — 홑따옴표의 다음 줄 이어짐. */
  static bool Iss_single_next_line(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [125] nb-single-multi-line(n) — 홑따옴표 여러 줄 내용. */
  static bool Isnb_single_multi_line(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [128] ns-plain-safe-out — 블록/명시 키의 plain 안전 문자(=ns-char). */
  static std::uint32_t Isns_plain_safe_out(const std::uint32_t cp);
  /** @brief [129] ns-plain-safe-in — 흐름의 plain 안전 문자(흐름 지시자 제외). */
  static std::uint32_t Isns_plain_safe_in(const std::uint32_t cp);

  /**
   * @brief [162]의 결과.
   * 성공 여부(ok) + 들여쓰기 지시자 m(0 = 생략, 자동 감지) + chomping 상태 t.
   * ok가 true면 커서는 헤더 끝까지 전진해 있다.
   */
  struct BlockHeader {
    bool ok;
    std::ptrdiff_t m;
    const ChompingState* t;
  };
  /**
   * @brief [162] c-b-block-header(t) — 블록 스칼라 헤더.
   * 스펙 1.2.2 원문은 두 지시자를 필수처럼 표기하나(알려진 결함),
   * prose 8.1.1.1과 1.2.1의 [163]에 따라 둘 다 생략 가능하게 구현한다.
   */
  static BlockHeader Isc_b_block_header(Cursor& cur);
  /** @brief [163] c-indentation-indicator — '1'~'9'면 cp, 아니면 0. */
  static std::uint32_t Isc_indentation_indicator(const std::uint32_t cp);
  /**
   * @brief [164] c-chomping-indicator(t) — '-' | '+' | 생략(=CLIP).
   * 항상 성공(생략은 빈 매치)하며, 소비한 만큼 커서를 전진시키고 t를 반환한다.
   */
  static const ChompingState* Isc_chomping_indicator(Cursor& cur);
  /** @brief 청킹 상태 싱글턴 → ChompKind 열거값 변환(이벤트용). */
  static ChompKind ChompKindOf(const ChompingState* t);
  /** @brief [167] l-strip-empty(n) — STRIP/CLIP의 후행 빈 줄(버림). */
  static bool Isl_strip_empty(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [168] l-keep-empty(n) — KEEP의 후행 빈 줄(보존). */
  static bool Isl_keep_empty(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [169] l-trail-comments(n) — 후행 주석 줄들. */
  static bool Isl_trail_comments(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [170] c-l+literal(n) — 리터럴 블록 스칼라 전체('|'+헤더+내용). */
  static bool Isc_l_literal(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [171] l-nb-literal-text(n) — 리터럴 내용 한 줄(선행 빈 행 포함). */
  static bool Isl_nb_literal_text(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [172] b-nb-literal-next(n) — 리터럴 다음 줄로의 이어짐. */
  static bool Isb_nb_literal_next(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [173] l-literal-content(n,t) — 리터럴 전체 내용(텍스트+청킹). */
  static bool Isl_literal_content(Cursor& cur, const std::ptrdiff_t n,
                                  const ChompingState& t);
  /** @brief [174] c-l+folded(n) — 폴디드 블록 스칼라 전체('>'+헤더+내용). */
  static bool Isc_l_folded(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [175] s-nb-folded-text(n) — 폴디드 내용 한 줄(들여쓰기+첫문자+본문). */
  static bool Iss_nb_folded_text(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [176] l-nb-folded-lines(n) — 폴디드 줄들의 연속(접힌 줄바꿈). */
  static bool Isl_nb_folded_lines(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [177] s-nb-spaced-text(n) — 들여쓰기 뒤 공백으로 시작하는 폴디드 줄. */
  static bool Iss_nb_spaced_text(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [178] b-l-spaced(n) — spaced 줄 뒤 줄바꿈(보존, 접히지 않음). */
  static bool Isb_l_spaced(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [179] l-nb-spaced-lines(n) — spaced 줄들의 연속. */
  static bool Isl_nb_spaced_lines(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [180] l-nb-same-lines(n) — 같은 들여쓰기의 줄들(folded | spaced). */
  static bool Isl_nb_same_lines(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [181] l-nb-diff-lines(n) — 서로 다른 들여쓰기 그룹들의 연속. */
  static bool Isl_nb_diff_lines(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [182] l-folded-content(n,t) — 폴디드 전체 내용(줄들+청킹). */
  static bool Isl_folded_content(Cursor& cur, const std::ptrdiff_t n,
                                 const ChompingState& t);
  /**
   * @brief [183] l+block-sequence(n) — 블록 시퀀스(항목들의 연속).
   * m은 첫 항목 행의 들여쓰기에서 자동 감지한다.
   */
  static bool Isl_block_sequence(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [184] c-l-block-seq-entry(n) — 블록 시퀀스 항목('-'+들여쓰기 노드). */
  static bool Isc_l_block_seq_entry(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [186] ns-l-compact-sequence(n) — 한 줄에 이어지는 압축 시퀀스. */
  static bool Isns_l_compact_sequence(Cursor& cur, const std::ptrdiff_t n);
  /**
   * @brief [187] l+block-mapping(n) — 블록 매핑(항목들의 연속).
   * m은 첫 항목 행의 들여쓰기에서 자동 감지한다.
   */
  static bool Isl_block_mapping(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [188] ns-l-block-map-entry(n) — 블록 매핑 항목(명시적 | 암시적). */
  static bool Isns_l_block_map_entry(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [189] c-l-block-map-explicit-entry(n) — 명시적('?') 매핑 항목. */
  static bool Isc_l_block_map_explicit_entry(Cursor& cur,
                                             const std::ptrdiff_t n);
  /** @brief [190] c-l-block-map-explicit-key(n) — 명시적 블록 매핑 키. */
  static bool Isc_l_block_map_explicit_key(Cursor& cur,
                                           const std::ptrdiff_t n);
  /** @brief [191] l-block-map-explicit-value(n) — 명시적 값(':'+들여쓰기 노드). */
  static bool Isl_block_map_explicit_value(Cursor& cur,
                                           const std::ptrdiff_t n);
  /** @brief [192] ns-l-block-map-implicit-entry(n) — 암시적 항목(키+값). */
  static bool Isns_l_block_map_implicit_entry(Cursor& cur,
                                              const std::ptrdiff_t n);
  /** @brief [193] ns-s-block-map-implicit-key — 암시적 키(JSON | YAML). */
  static bool Isns_s_block_map_implicit_key(Cursor& cur);
  /** @brief [194] c-l-block-map-implicit-value(n) — 암시적 값(':'+노드|빈값). */
  static bool Isc_l_block_map_implicit_value(Cursor& cur,
                                             const std::ptrdiff_t n);
  /** @brief [195] ns-l-compact-mapping(n) — 한 줄에 이어지는 압축 매핑. */
  static bool Isns_l_compact_mapping(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [197] s-l+flow-in-block(n) — 블록 안의 흐름 노드. */
  static bool Iss_l_flow_in_block(Cursor& cur, const std::ptrdiff_t n);
  /** @brief [202] l-document-prefix — 문서 프리픽스(BOM? + 주석*). */
  static bool Isl_document_prefix(Cursor& cur);
  /** @brief [203] c-directives-end — 지시자 종료/문서 시작 표식 "---". */
  static bool Isc_directives_end(Cursor& cur);
  /**
   * @brief [204] c-document-end — 문서 종료 표식 "...".
   * "..." 뒤가 비공백 문자면 실패한다(스펙 주석).
   */
  static bool Isc_document_end(Cursor& cur);
  /** @brief [205] l-document-suffix — 문서 서픽스("..."+주석들). */
  static bool Isl_document_suffix(Cursor& cur);
  /**
   * @brief [206] c-forbidden — 문서 마커로 시작하는 금지된 콘텐츠(lookahead).
   * 커서를 소비하지 않는 순수 술어다.
   */
  static bool Isc_forbidden(const Cursor& cur);
  /**
   * @brief [207] l-bare-document — 베어(지시자 없는) 문서.
   * c-forbidden 내용 배제는 각 내용 프로덕션(plain/quoted/블록 스칼라의
   * 행 시작 지점)에서 수행한다.
   */
  static bool Isl_bare_document(Cursor& cur);
  /** @brief [208] l-explicit-document — 명시적("---"로 시작하는) 문서. */
  static bool Isl_explicit_document(Cursor& cur);
  /** @brief [209] l-directive-document — 지시자가 있는 문서. */
  static bool Isl_directive_document(Cursor& cur);
  /** @brief [210] l-any-document — 임의 종류의 문서. */
  static bool Isl_any_document(Cursor& cur);
};

}  // namespace bedrock::archive::yaml
