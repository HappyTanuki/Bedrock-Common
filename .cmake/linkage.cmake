# ============================================================
# Linkage – libc detection, license guard
# ============================================================
# 출력:
#   COMMON_LIBC             (var: glibc | musl | other | unknown)
#   COMMON_HAVE_GLIBC       (var: 1 / unset)
# ============================================================

# ============================================================
# Detect libc on Linux (glibc / musl / other)
# ============================================================
set(COMMON_LIBC "unknown")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckSymbolExists)
    check_symbol_exists(__GLIBC__ "features.h" COMMON_HAVE_GLIBC)

    if(COMMON_HAVE_GLIBC)
        set(COMMON_LIBC "glibc")
    else()
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -dumpmachine
            OUTPUT_VARIABLE _cc_triple
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_cc_triple MATCHES "musl")
            set(COMMON_LIBC "musl")
        else()
            file(GLOB _musl_loader "/lib/ld-musl-*.so*" "/lib64/ld-musl-*.so*")
            if(_musl_loader)
                set(COMMON_LIBC "musl")
            else()
                set(COMMON_LIBC "other")
            endif()
        endif()
    endif()
    message(STATUS "Common: detected libc = ${COMMON_LIBC}")
endif()

# ============================================================
# License guard: glibc + STATIC linkage is disallowed
# ============================================================
# glibc는 LGPL-2.1 라이선스라 정적 링크 시 재링크 가능 형태 배포 등
# 별도 의무가 발생합니다. 이 프로젝트는 해당 의무를 회피하기 위해
# glibc 환경에서의 STATIC 빌드를 차단합니다.
# 정적 빌드를 원하면 musl 기반 환경(예: Alpine Linux)에서 빌드하세요.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND BUILD_SHARED_LIBS STREQUAL "OFF"
   AND COMMON_LIBC STREQUAL "glibc")
    message(FATAL_ERROR
        "Common: STATIC linkage is not allowed on glibc.\n"
        "  Reason: glibc is licensed under LGPL-2.1, and static linking "
        "imposes redistribution obligations (relinkable object files, "
        "license notice, etc.) that this project chooses not to undertake.\n"
        "  Resolution: build on a musl-based environment (e.g. Alpine Linux), "
        "or use COMMON_LINKAGE=DYNAMIC.")
endif()
