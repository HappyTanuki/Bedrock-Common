#pragma once

#include <string_view>

// ISO3166_1(국가) / ISO639_1(언어) enum. tools/gen_locales.py 로 생성.
#include "common/i18n/iso_codes.gen.h"

namespace bedrock::locale {

enum class StringKey {
  kStatusSuccess,
  kStatusNoEnt,
  kStatusError,
  kStatusNullStream,
  kStatusCorruptedData,
  kStatusCorruptedWhileParsing
};

std::string_view GetI18nString(const StringKey& key, const ISO639_1& language,
                               const ISO3166_1& country);

}  // namespace bedrock::locale
