cmake_minimum_required (VERSION 3.25)
# ============================================================
# Policy
# ============================================================
include("${CMAKE_CURRENT_SOURCE_DIR}/.cmake/policy.cmake")

# ============================================================
# Project
# ============================================================
if(NOT DEFINED ROOT_PROJECT_NAME)
    set(ROOT_PROJECT_NAME "Root")
endif()

set(SUB_PROJECT_NAME "Common")
project(${SUB_PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD             23)
set(CMAKE_CXX_STANDARD_REQUIRED    ON)
set(CMAKE_CXX_EXTENSIONS          OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS  ON)

# ============================================================
# Architecture detection (config.h 생성에 사용)
# ============================================================
# BEDROCK_ARCH_X86 / BEDROCK_ARCH_ARM / BEDROCK_ARCH_NAME 변수를 설정합니다.
# 반드시 아래 configure_file(config.h.in -> config.h) 호출 전에 include 되어야 합니다.
include("${CMAKE_CURRENT_SOURCE_DIR}/.cmake/architecture.cmake")

# ============================================================
# Linkage option (cross-platform abstraction of MT/MD)
# ============================================================
# DYNAMIC: 결과물 SHARED + 시스템 런타임 동적 (Win=MD, Linux=.so + libstdc++.so)
# STATIC : 결과물 STATIC + 시스템 런타임 정적 (Win=MT, Linux=musl만 허용)
include("${CMAKE_CURRENT_SOURCE_DIR}/.cmake/linkage.cmake")

# ============================================================
# Target – Common
# ============================================================
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cc")

if(ENCRYPTION_LINKAGE MATCHES "DYNAMIC")
    add_library(${SUB_PROJECT_NAME} SHARED ${SOURCES})
else()
    add_library(${SUB_PROJECT_NAME} STATIC ${SOURCES})
endif()

target_include_directories(${SUB_PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Alias for parent projects
add_library(${ROOT_PROJECT_NAME}::${SUB_PROJECT_NAME} ALIAS ${SUB_PROJECT_NAME})

# Compiler options
include("${CMAKE_CURRENT_SOURCE_DIR}/.cmake/compiler_options.cmake")

# ISA 명령어셋 활성화 플래그(-march=native 등). 감지는 런타임에서 수행.
include("${CMAKE_CURRENT_SOURCE_DIR}/.cmake/isa_features.cmake")

# ============================================================
# compile_commands.json copy (clangd/IDE intellisense용)
# ============================================================
# symlink는 Windows에서 관리자 권한이 필요하므로 copy_if_different로 처리.
#
# CMake 단계 순서(configure → generate) 때문에 단일 cmake 호출로는 자기 generate
# 결과를 만지지 못합니다. 이중 안전망으로 처리:
#   (1) configure 시점 execute_process: 이전 configure가 남긴 compile_commands.json이
#       있으면 즉시 copy. 첫 configure에는 source가 없어 silent fail (정상).
#       두 번째 configure부터 intellisense 즉시 활성화.
#   (2) add_custom_target(ALL): 빌드 시점 보장. 첫 빌드 후 항상 최신.
if (PROJECT_IS_TOP_LEVEL AND CMAKE_EXPORT_COMPILE_COMMANDS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/compile_commands.json
                ${CMAKE_BINARY_DIR}/../compile_commands.json
        OUTPUT_QUIET
        ERROR_QUIET
    )
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_BINARY_DIR}/compile_commands.json
                ${CMAKE_BINARY_DIR}/../compile_commands.json
        COMMENT "Copying compile_commands.json to project root"
        VERBATIM
    )
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/.cmake/post_build.cmake")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/.cmake/cmake_include/bedrock_common_config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/bedrock_common_config.h" @ONLY)
target_include_directories(${SUB_PROJECT_NAME} SYSTEM PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
