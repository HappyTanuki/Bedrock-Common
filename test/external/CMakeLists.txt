file(GLOB EXTERNAL_TEST_SOURCES CONFIGURE_DEPENDS "src/*.cc")

add_executable(common_external_tests ${EXTERNAL_TEST_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../test_main.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../snowflake_child.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../snowflake_process_helper.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scoped_snowflake_config.cc")
target_include_directories(common_external_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(common_external_tests PRIVATE
    Common
    GTest::gtest
    _BedrockBuildConfig::Utf8)
# Common's cxx_std_23 requirement sets only the minimum standard; set the
# project language properties so GCC/Clang compile ISO C++23, not gnu++23.
_BedrockBuildConfig_set_language_properties(common_external_tests)
if(WIN32)
    # Helper children parse their native wide command line (CommandLineToArgvW).
    target_link_libraries(common_external_tests PRIVATE shell32)
endif()

if(NOT WIN32)
    target_link_libraries(common_external_tests PRIVATE _BedrockBuildConfig::NoExceptions _BedrockBuildConfig::NoRtti _BedrockBuildConfig::TestWarnings)
endif()

if(WIN32 AND BUILD_SHARED_LIBS)
    add_custom_command(
        TARGET common_external_tests
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:common_external_tests>
            $<TARGET_FILE_DIR:common_external_tests>
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs for common_external_tests")
endif()

gtest_discover_tests(common_external_tests
    TEST_PREFIX "external."
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST)
