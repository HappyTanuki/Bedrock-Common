file(GLOB EXTERNAL_TEST_SOURCES CONFIGURE_DEPENDS "src/*.cc")

add_executable(common_external_tests ${EXTERNAL_TEST_SOURCES})
target_include_directories(common_external_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(common_external_tests PRIVATE
    ${SUB_PROJECT_NAME}
    GTest::gtest_main)

if(NOT WIN32)
    target_compile_options(common_external_tests PRIVATE
        -fno-exceptions -fno-rtti
        -Wno-global-constructors
        -Wno-exit-time-destructors
        -Wno-weak-vtables
        -Wno-used-but-marked-unused
        -Wno-covered-switch-default
        -Wno-disabled-macro-expansion
        -Wno-zero-as-null-pointer-constant)
endif()

if(BUILD_SHARED_LIBS)
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
