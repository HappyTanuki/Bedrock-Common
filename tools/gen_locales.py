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
    """country-codes.csv를 한 행씩 읽어 (alpha2, 숫자 코드, 주석) 튜플을
    yield한다.

    alpha2 또는 numeric 코드가 비어 있는 행은 건너뛴다. name은
    official_name_en이 비어 있으면 CLDR display name으로 대체한다.
    """
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
    """language-codes.csv를 한 행씩 읽어 (alpha2, None, 영어 이름)
    튜플을 yield한다.

    alpha2 길이가 2가 아닌 행(비표준 코드)은 건너뛴다. 코드값은 항상
    None이며, 순차값 부여는 emit()에 맡긴다.
    """
    path = os.path.join(DATA, "language-codes.csv")
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            alpha2 = r["alpha2"].strip().lower()
            if len(alpha2) != 2:
                continue
            yield alpha2.upper(), None, r["English"].strip()


def emit(name, underlying, rows):
    """rows로부터 `enum class {name} : {underlying} {...};` 블록을
    표준 출력에 인쇄한다.

    rows는 (enumerator, value, comment) 3튜플의 이터러블이다. value가
    None이면 컴파일러가 매기는 순차값을 쓰고, 있으면 3자리 폭 우측
    정렬로 세로줄을 맞춘다. 각 줄 끝에는 comment를 // 주석으로 붙인다.
    """
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
