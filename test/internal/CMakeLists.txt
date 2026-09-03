file(GLOB INTERNAL_TEST_SOURCES CONFIGURE_DEPENDS "src/*.cc")

set(INTERNAL_ARCHIVE_SOURCES ${SOURCES})
list(FILTER INTERNAL_ARCHIVE_SOURCES EXCLUDE REGEX "[/\\\\]allocator\\.cc$")
add_library(common_internal_archive STATIC ${INTERNAL_ARCHIVE_SOURCES})
target_include_directories(common_internal_archive
    PRIVATE
        "${COMMON_EXTERNAL_INCLUDE_DIR}"
        "${COMMON_INTERNAL_INCLUDE_DIR}")
target_include_directories(common_internal_archive SYSTEM PRIVATE
    "${CMAKE_BINARY_DIR}")

# Internal test 세 타깃은 preset과 무관하게 동일한 /MD[d] CRT를 사용한다.
# 따라서 preset CRT로 빌드된 GTest::gtest_main과 섞지 않고 전용 gtest를 둔다.
add_library(common_internal_gtest STATIC
    "${googletest_SOURCE_DIR}/googletest/src/gtest-all.cc"
    "${googletest_SOURCE_DIR}/googletest/src/gtest_main.cc")
target_include_directories(common_internal_gtest SYSTEM
    PUBLIC "${googletest_SOURCE_DIR}/googletest/include"
    PRIVATE "${googletest_SOURCE_DIR}/googletest")
if(TARGET Threads::Threads)
    target_link_libraries(common_internal_gtest PUBLIC Threads::Threads)
endif()

add_executable(common_internal_tests ${INTERNAL_TEST_SOURCES})
target_include_directories(common_internal_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${COMMON_EXTERNAL_INCLUDE_DIR}"
    "${COMMON_INTERNAL_INCLUDE_DIR}")
target_link_libraries(common_internal_tests PRIVATE
    common_internal_archive
    common_internal_gtest)

if(MSVC)
    set_property(TARGET
        common_internal_archive
        common_internal_gtest
        common_internal_tests
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    target_compile_options(common_internal_archive PRIVATE
        -fno-exceptions -fno-rtti)
    target_compile_options(common_internal_gtest PRIVATE
        -fno-exceptions -fno-rtti)
    target_compile_options(common_internal_tests PRIVATE
        -fno-exceptions -fno-rtti
        -Wno-global-constructors
        -Wno-exit-time-destructors
        -Wno-weak-vtables
        -Wno-used-but-marked-unused
        -Wno-covered-switch-default
        -Wno-disabled-macro-expansion
        -Wno-zero-as-null-pointer-constant)
endif()

gtest_discover_tests(common_internal_tests
    TEST_PREFIX "internal."
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST)
