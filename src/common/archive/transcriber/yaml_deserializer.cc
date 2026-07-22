/**
 * @file yaml_deserializer.cc
 * @brief YAMLDeserializer 구현부(포맷 훅: Parse+Compose).
 */
#include "common/archive/transcriber/yaml_deserializer.h"

#include <format>
#include <istream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "common/archive/yaml/compose.h"
#include "common/archive/yaml/grammar.h"
#include "common/i18n/locales.h"
#include "common/util/unicode.h"

namespace bedrock::archive::transcriber {

namespace {

/** @brief "…를 파싱하는 동안 손상" 상태(위치·대상 이름 포함). */
Status CorruptedAt(std::string_view what) {
  const std::string_view fmt =
      GetI18nString(locale::StringKey::kStatusCorruptedWhileParsing,
                    locale::ISO639_1::kKO, locale::ISO3166_1::kKR);
  return Status(TranscriberError::kCorrupted,
                std::vformat(fmt, std::make_format_args(what)));
}

/** @brief 성공 상태. */
Status Ok() { return Status(make_error_code(TranscriberError::kSuccess)); }

}  // namespace

YAMLDeserializer::~YAMLDeserializer() = default;

Status YAMLDeserializer::LoadDocument(std::istream& in, Node& out) {
  std::string text{std::istreambuf_iterator<char>(in),
                   std::istreambuf_iterator<char>()};
  if (in.bad()) {
    return Status(
        TranscriberError::kNullStream,
        std::string(GetI18nString(locale::StringKey::kStatusNullStream,
                                  locale::ISO639_1::kKO,
                                  locale::ISO3166_1::kKR)));
  }
  std::vector<std::uint32_t> buf;
  if (!util::DecodeUtf8(text, buf)) {
    return CorruptedAt("UTF-8");
  }
  yaml::Events events;
  yaml::Diag diag;
  yaml::Cursor cur{
      std::span<const std::uint32_t>(buf.data(), static_cast<std::size_t>(0)),
      std::span<const std::uint32_t>(buf.data(), buf.size()), &events, &diag};
  static_cast<void>(yaml::Grammar::Isl_yaml_stream(cur));
  if (cur.before.size() != buf.size()) {
    // 부분 매치 — 최심 도달 지점을 행:열로 보고
    const std::size_t off =
        diag.furthest > cur.before.size() ? diag.furthest : cur.before.size();
    const yaml::LineCol lc = yaml::OffsetToLineCol(buf, off);
    return CorruptedAt(std::format("YAML {}:{}", lc.line, lc.col));
  }
  yaml::ComposeResult r = yaml::Compose(buf, events.list);
  if (!r.ok) {
    return CorruptedAt(r.error);
  }
  if (r.docs.empty()) {
    return CorruptedAt("empty document");
  }
  out = std::move(r.docs.front());
  return Ok();
}

bool YAMLDeserializer::IsBinaryScalar(const Node& n) const {
  return n.tag == "!!binary";
}

}  // namespace bedrock::archive::transcriber
