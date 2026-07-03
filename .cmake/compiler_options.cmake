# ============================================================
# Compiler Options
# ============================================================
# 입력:
#   SUB_PROJECT_NAME   (이미 add_library 된 타겟 이름)
#   COMMON_LINKAGE / COMMON_LIBC  (from linkage.cmake)
# 부수효과:
#   ${SUB_PROJECT_NAME}에 PCH/컴파일 옵션/링크 라이브러리 부착
#   CommonLinkOptions INTERFACE 타겟 생성
# ============================================================

if (TARGET ${SUB_PROJECT_NAME})
    target_precompile_headers(${SUB_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/common/pch.h"
    )
    if(MSVC)
        target_compile_options(${SUB_PROJECT_NAME} PRIVATE
        /MP
        /utf-8           # 소스/실행 인코딩을 UTF-8로 명시 (한글 주석이 CP949로 오인되어 토큰화가 깨지는 사고 방지)
        /W4              # 합리적인 모든 경고
        /WX              # 경고를 에러로 (Linux의 -Werror)
        /permissive-     # 표준 엄격 모드 (Microsoft 확장 거부)
        /w14242 /w14254 /w14263 /w14265 /w14287 /we4289
        /w14296 /w14311 /w14545 /w14546 /w14547 /w14549
        /w14555 /w14619 /w14640 /w14826 /w14905 /w14906
        /w14928
        /Zc:__cplusplus  # __cplusplus 매크로 정확히 보고
        /Zc:preprocessor # 표준 준수 전처리기
        )
    endif()
endif()

if(UNIX AND NOT APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(${SUB_PROJECT_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH            "$ORIGIN"
        SKIP_BUILD_RPATH         FALSE
        BUILD_RPATH              "$ORIGIN"
    )
endif()

# ============================================================
# Cross-platform link options carrier
# (transitive하게 따라붙도록 INTERFACE 타겟으로 운반)
# ============================================================
if(NOT TARGET CommonLinkOptions)
    add_library(CommonLinkOptions INTERFACE)
endif()

if(NOT WIN32 AND COMMON_LINKAGE STREQUAL "STATIC")
    if(COMMON_LIBC STREQUAL "musl")
        # musl: full-static OK
        target_link_options(CommonLinkOptions INTERFACE -static)
        message(STATUS "Common linkage: STATIC (musl, full-static)")
    else()
        # 도달 시점은 COMMON_LIBC == "other" 등 (glibc는 위에서 FATAL_ERROR)
        target_link_options(CommonLinkOptions INTERFACE
            -static-libgcc -static-libstdc++)
        message(STATUS "Common linkage: STATIC (libc=${COMMON_LIBC}, runtime static)")
    endif()
endif()

target_link_libraries(${SUB_PROJECT_NAME} PUBLIC CommonLinkOptions)

# ============================================================
# ISA(instruction set) 플래그
# ============================================================
# 명령어셋 활성화(-march=native 류)는 .cmake/isa_features.cmake 에서 처리합니다
# (CMakeLists 에서 이 파일 이후 include).
# 여기서는 예외/RTTI 비활성화 등 일반 옵션만 둡니다.

if (NOT WIN32)
    # 전역에는 ISA 플래그를 걸지 않습니다(위 주석 참고). 예외/RTTI 비활성만 유지.
    target_compile_options(
        ${SUB_PROJECT_NAME} PRIVATE
        -fno-exceptions -fno-rtti
    )
else()
    add_compile_options(/utf-8)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-Wuseless-cast)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(
        -Weverything
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-unused-macros
        -Wno-padded
    )
endif ()

if(WIN32)
    set_target_properties(${SUB_PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    add_compile_options(
        -Werror
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wundef
        -Wunreachable-code
        -Wstrict-aliasing
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wcast-qual
        -Wcast-align
    )
endif()
