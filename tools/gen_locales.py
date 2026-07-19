#!/usr/bin/env python3
"""ISO3166_1 / ISO639_1 enum 헤더 생성기 (일회성).

사용:
    python3 tools/gen_locales.py > include/common/i18n/iso_codes.gen.h

데이터 출처(레포에 커밋됨):
    tools/data/country-codes.csv   (datasets/country-codes)
    tools/data/language-codes.csv  (datasets/language-codes)
"""
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")


def country_rows():
    path = os.path.join(DATA, "country-codes.csv")
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            alpha2 = r["ISO3166-1-Alpha-2"].strip()
            numeric = r["ISO3166-1-numeric"].strip()
            if not alpha2 or not numeric:
                continue
            name = r["official_name_en"].strip() or r["CLDR display name"].strip()
            code = int(numeric)
            # 값은 정수, 주석엔 ISO 관례대로 3자리 표기(004, 008, ...).
            yield alpha2.upper(), code, f"{code:03d} {name}"


def language_rows():
    path = os.path.join(DATA, "language-codes.csv")
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            alpha2 = r["alpha2"].strip().lower()
            if len(alpha2) != 2:
                continue
            yield alpha2.upper(), None, r["English"].strip()


def emit(name, underlying, rows):
    print(f"enum class {name} : {underlying} {{")
    for enumerator, value, comment in rows:
        # value 가 None 이면 순차값(0,1,2,...)을 컴파일러에 맡긴다.
        # 값이 있으면 3칸 폭 우측 정렬로 세로줄을 맞춘다(앞자리 0은 8진수라 불가).
        assign = f" = {value:>3}" if value is not None else ""
        print(f"  k{enumerator}{assign},  // {comment}")
    print("};\n")


print("""#pragma once
// clang-format off
//
// 이 파일은 tools/gen_locales.py 가 생성했습니다. 직접 수정하지 마세요.
// 재생성: python3 tools/gen_locales.py > include/common/i18n/iso_codes.gen.h
//
#include <cstdint>

namespace bedrock::locale {
""")

# ISO3166_1: 값 = ISO 3166-1 숫자 코드 (255 초과 -> uint16 필요)
print("// https://github.com/datasets/country-codes/blob/main/data/country-codes.csv")
emit("ISO3166_1", "std::uint16_t", country_rows())

# ISO639_1: 순차값(0,1,2,...). 183개라 uint8 로 충분.
print("// https://github.com/datasets/language-codes/blob/main/data/language-codes.csv")
emit("ISO639_1", "std::uint8_t", language_rows())

print("}  // namespace bedrock::locale")
print("// clang-format on")
