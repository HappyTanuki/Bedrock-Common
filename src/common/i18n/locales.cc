#include "common/i18n/locales.h"

#include <cstdint>
#include <unordered_map>

#include "common/i18n/language/ko.h"

namespace bedrock::locale {
namespace {

// (언어, 국가) -> 하나의 해시 키로 패킹. 언어 8비트 + 국가 16비트라 충돌 없음.
constexpr std::uint32_t RegionKey(ISO639_1 lang, ISO3166_1 country) {
  return (static_cast<std::uint32_t>(lang) << 16) |
         static_cast<std::uint32_t>(country);
}

// StringKey 하나에 대한 번역 묶음.
//   by_language : 언어별 기본 문자열 (기본은 여기 하나만 둔다)
//   by_region   : 일부 (언어,국가) 조합만 override
struct LocaleEntry {
  std::unordered_map<ISO639_1, std::string_view> by_language;
  std::unordered_map<std::uint32_t, std::string_view> by_region;
};

// 폴백 언어: 요청 언어에 없으면 이 언어의 기본을 쓴다.
constexpr ISO639_1 kFallbackLanguage = ISO639_1::kEN;

}  // namespace

// 함수-로컬 static: 첫 호출 시 초기화 -> TU 초기화 순서 문제(SIOF) 없음.
const std::unordered_map<StringKey, LocaleEntry> table = {
    {StringKey::kStatusSuccess,
     {.by_language = {{ISO639_1::kKO, language::ko::status::kSuccess}},
      .by_region = {}}},
    {StringKey::kStatusNoEnt,
     {.by_language = {{ISO639_1::kKO, language::ko::status::kNoEnt}},
      .by_region = {}}},
    {StringKey::kStatusError,
     {.by_language = {{ISO639_1::kKO, language::ko::status::kError}},
      .by_region = {}}},
    {StringKey::kStatusNullStream,
     {.by_language = {{ISO639_1::kKO, language::ko::status::kNullStream}},
      .by_region = {}}},
    {StringKey::kStatusCorruptedData,
     {.by_language = {{ISO639_1::kKO, language::ko::status::kCorruptedData}},
      .by_region = {}}},
    {StringKey::kStatusCorruptedWhileParsing,
     {.by_language = {{ISO639_1::kKO,
                       language::ko::status::kCorruptedWhileParsing}},
      .by_region = {}}}};

std::string_view GetI18nString(const StringKey& key, const ISO639_1& language,
                               const ISO3166_1& country) {
  const auto entry = table.find(key);
  if (entry == table.end()) return {};
  const LocaleEntry& e = entry->second;

  // 1) 지역별 override (일부만 존재)
  if (const auto it = e.by_region.find(RegionKey(language, country));
      it != e.by_region.end()) {
    return it->second;
  }
  // 2) 언어별 기본
  if (const auto it = e.by_language.find(language); it != e.by_language.end()) {
    return it->second;
  }
  // 3) 폴백 언어
  if (const auto it = e.by_language.find(kFallbackLanguage);
      it != e.by_language.end()) {
    return it->second;
  }
  return {};
}

}  // namespace bedrock::locale
