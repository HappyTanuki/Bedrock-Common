file(GLOB INTERNAL_TEST_SOURCES CONFIGURE_DEPENDS "src/*.cc")
list(FILTER INTERNAL_TEST_SOURCES EXCLUDE REGEX "snowflake_static_copy\.cc$")
if(NOT WIN32)
    # The independently linked static-copy DLL suite is Win32-only; the
    # process suite uses the portable launcher in ../snowflake_child.cc.
    list(FILTER INTERNAL_TEST_SOURCES EXCLUDE REGEX "snowflake_static_copy_test\\.cc$")
endif()

set(INTERNAL_ARCHIVE_SOURCES ${SOURCES})
list(FILTER INTERNAL_ARCHIVE_SOURCES EXCLUDE REGEX "[/\\\\]allocator\\.cc$")
add_library(common_internal_archive STATIC ${INTERNAL_ARCHIVE_SOURCES})
target_compile_definitions(common_internal_archive PRIVATE BEDROCK_OS_IO_TESTING)
target_include_directories(common_internal_archive
    PRIVATE
        "${COMMON_EXTERNAL_INCLUDE_DIR}"
        "${COMMON_INTERNAL_INCLUDE_DIR}")
target_include_directories(common_internal_archive SYSTEM PRIVATE
    "${COMMON_GENERATED_INCLUDE_DIR}")
target_link_libraries(common_internal_archive PRIVATE _BedrockBuildConfig::Cxx23 _BedrockBuildConfig::Utf8)
_BedrockBuildConfig_set_language_properties(common_internal_archive)

# Internal test targets use the dynamic CRT regardless of the selected production preset.
# They link a private GTest build and the shared custom runner, never preset gtest_main.
add_library(common_internal_gtest STATIC
    "${googletest_SOURCE_DIR}/googletest/src/gtest-all.cc")
target_include_directories(common_internal_gtest SYSTEM
    PUBLIC "${googletest_SOURCE_DIR}/googletest/include"
    PRIVATE "${googletest_SOURCE_DIR}/googletest")
if(TARGET Threads::Threads)
    target_link_libraries(common_internal_gtest PUBLIC Threads::Threads)
endif()
target_link_libraries(common_internal_gtest PRIVATE _BedrockBuildConfig::Cxx23 _BedrockBuildConfig::Utf8)
_BedrockBuildConfig_set_language_properties(common_internal_gtest)

add_executable(common_internal_tests ${INTERNAL_TEST_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../test_main.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../snowflake_child.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../snowflake_process_helper.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scoped_snowflake_config.cc")
target_compile_definitions(common_internal_tests PRIVATE BEDROCK_OS_IO_TESTING)
target_include_directories(common_internal_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${COMMON_EXTERNAL_INCLUDE_DIR}"
    "${COMMON_INTERNAL_INCLUDE_DIR}")
target_link_libraries(common_internal_tests PRIVATE
    common_internal_archive
    common_internal_gtest)
target_link_libraries(common_internal_tests PRIVATE _BedrockBuildConfig::Cxx23 _BedrockBuildConfig::Utf8)
if(WIN32)
    # Helper children parse their native wide command line (CommandLineToArgvW).
    target_link_libraries(common_internal_tests PRIVATE shell32)
endif()
_BedrockBuildConfig_set_language_properties(common_internal_tests)

if(WIN32)
    foreach(copy IN ITEMS a b)
        add_library(snowflake_static_copy_${copy} SHARED
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/data_types/snowflake.cc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/platform/os_io.cc"
            "${CMAKE_CURRENT_SOURCE_DIR}/../internal/src/snowflake_static_copy.cc")
        target_include_directories(snowflake_static_copy_${copy} PRIVATE
            "${COMMON_EXTERNAL_INCLUDE_DIR}" "${COMMON_INTERNAL_INCLUDE_DIR}")
        target_link_libraries(snowflake_static_copy_${copy} PRIVATE
            _BedrockBuildConfig::Cxx23 _BedrockBuildConfig::Utf8 shell32 ole32)
        _BedrockBuildConfig_set_language_properties(snowflake_static_copy_${copy})
        _BedrockBuildConfig_set_msvc_runtime(snowflake_static_copy_${copy}
            VALUE "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endforeach()
    add_dependencies(common_internal_tests snowflake_static_copy_a snowflake_static_copy_b)
endif()

if(MSVC)
    foreach(_internal_target common_internal_archive common_internal_gtest common_internal_tests)
        _BedrockBuildConfig_set_msvc_runtime(${_internal_target} VALUE "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endforeach()
else()
    target_link_libraries(common_internal_archive PRIVATE _BedrockBuildConfig::NoExceptions _BedrockBuildConfig::NoRtti)
    target_link_libraries(common_internal_gtest PRIVATE _BedrockBuildConfig::NoExceptions _BedrockBuildConfig::NoRtti _BedrockBuildConfig::TestWarnings)
    target_link_libraries(common_internal_tests PRIVATE _BedrockBuildConfig::NoExceptions _BedrockBuildConfig::NoRtti _BedrockBuildConfig::TestWarnings)
endif()

gtest_discover_tests(common_internal_tests
    TEST_PREFIX "internal."
    WORKING_DIRECTORY ${COMMON_GENERATED_INCLUDE_DIR}
    PROPERTIES WORKING_DIRECTORY ${COMMON_GENERATED_INCLUDE_DIR}
    DISCOVERY_MODE PRE_TEST)
