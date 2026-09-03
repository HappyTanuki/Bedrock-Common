# ============================================================
# 테스트 구성
#  - common_external_tests: 공개 API와 레거시 동작을 production library로 검증
#  - common_internal_tests: internal 구현을 전용 static library로 white-box 검증
# ============================================================

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
if(MSVC)
    if(CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL$")
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    else()
        set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    endif()
endif()
set(_saved_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(googletest)
set(BUILD_SHARED_LIBS "${_saved_BUILD_SHARED_LIBS}")

if(NOT WIN32)
    target_compile_options(gtest PRIVATE -fno-exceptions -fno-rtti)
    target_compile_options(gtest_main PRIVATE -fno-exceptions -fno-rtti)
endif()

include(GoogleTest)

add_subdirectory(external)

add_subdirectory(internal)
