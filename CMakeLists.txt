cmake_minimum_required (VERSION 3.16)
set(SUB_PROJECT_NAME "Common")
project(${SUB_PROJECT_NAME} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SOURCES "src/*.cc")

add_library(${SUB_PROJECT_NAME} STATIC ${SOURCES})
target_include_directories(${SUB_PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(DEFINED ROOT_PROJECT_NAME)
    add_library(${ROOT_PROJECT_NAME}::${SUB_PROJECT_NAME} ALIAS ${SUB_PROJECT_NAME})
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    # Additional flags for GCC
    add_compile_options(-Wuseless-cast)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # Additional flags for Clang
    add_compile_options(-Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-unused-macros -Wno-padded)
endif ()
if(NOT WIN32)
    add_compile_options(-Werror -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wundef -Wunreachable-code -Wstrict-aliasing -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wcast-qual -Wcast-align)
endif()

find_program(CLANG_TIDY_EXE NAMES clang-tidy)

if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY
    $<$<COMPILE_LANGUAGE:CXX>:clang-tidy;-checks=-*,google-readability-casting;-fix;-fix-errors>
)
endif()

if (PROJECT_IS_TOP_LEVEL AND CMAKE_EXPORT_COMPILE_COMMANDS)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_BINARY_DIR}/../compile_commands.json
  )
endif()

option(BEDROCK_COMMON_BUILD_TESTS
  "Build Bedrock tests"
  ${PROJECT_IS_TOP_LEVEL}
)

if (BEDROCK_COMMON_BUILD_TESTS)
  set(BUILD_TESTING ON)

  include(CTest)
  enable_testing()

  add_subdirectory(test)
endif()