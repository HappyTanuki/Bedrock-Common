# ============================================================
# Linkage – option, libc detection, license guard, MSVC runtime
# ============================================================
# 출력:
#   COMMON_LINKAGE          (cache: DYNAMIC | STATIC)
#   COMMON_LIBC             (var: glibc | musl | other | unknown)
#   COMMON_HAVE_GLIBC       (var: 1 / unset)
#   CMAKE_MSVC_RUNTIME_LIBRARY  (var, Win32에서만 자동 지정)
# ============================================================

# ============================================================
# Linkage option (cross-platform abstraction of MT/MD)
# ============================================================
# DYNAMIC: 결과물 SHARED + 시스템 런타임 동적 (Win=MD, Linux=.so + libstdc++.so)
# STATIC : 결과물 STATIC + 시스템 런타임 정적 (Win=MT, Linux=musl만 허용)
set(COMMON_LINKAGE "DYNAMIC" CACHE STRING
    "Library and runtime linkage: DYNAMIC or STATIC")
set_property(CACHE COMMON_LINKAGE PROPERTY STRINGS DYNAMIC STATIC)

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
   AND COMMON_LINKAGE STREQUAL "STATIC"
   AND COMMON_LIBC STREQUAL "glibc")
    message(FATAL_ERROR
        "Common: STATIC linkage is not allowed on glibc.\n"
        "  Reason: glibc is licensed under LGPL-2.1, and static linking "
        "imposes redistribution obligations (relinkable object files, "
        "license notice, etc.) that this project chooses not to undertake.\n"
        "  Resolution: build on a musl-based environment (e.g. Alpine Linux), "
        "or use COMMON_LINKAGE=DYNAMIC.")
endif()

# ============================================================
# Apply MSVC runtime based on COMMON_LINKAGE
# (preset이 직접 지정한 경우엔 건드리지 않음)
# ============================================================
if(WIN32 AND NOT CMAKE_MSVC_RUNTIME_LIBRARY)
    if(COMMON_LINKAGE STREQUAL "STATIC")
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endif()
