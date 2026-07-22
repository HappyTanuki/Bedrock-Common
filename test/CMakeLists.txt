# ============================================================
# 테스트 구성
#  - *_test.cc  : GoogleTest 단위 테스트 → common_unit_tests 하나로 통합
#  - 그 외 *.cc : 레거시 스탠드얼론 테스트(main 직접 구현) → 파일당 실행 파일
# ============================================================
file(GLOB TEST_SOURCES "*.cc")
file(GLOB UNIT_TEST_SOURCES "*_test.cc")
if(UNIT_TEST_SOURCES)
    list(REMOVE_ITEM TEST_SOURCES ${UNIT_TEST_SOURCES})
endif()

foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE ${PROJECT_NAME})
    # Common 라이브러리가 -fno-rtti/-fno-exceptions 로 빌드되므로 테스트도 동일하게
    # 맞춥니다. 그렇지 않으면 RTTI-on 으로 컴파일된 파생 클래스(예: Data:Archive)의
    # typeinfo 가 라이브러리에 없는 base typeinfo 를 참조해 링크 에러가 납니다.
    if(NOT WIN32)
        target_compile_options(${test_name} PRIVATE -fno-exceptions -fno-rtti)
    endif()
    # 테스트는 런타임 감지 API(GetCPUFeatures/IsCpuEnabledFeature)만 사용하므로
    # ISA 플래그가 필요 없습니다. -march=native / -maes 등은 (1) 비-x86에서
    # 컴파일 실패를 유발하고 (2) 빌드 머신에 종속되므로 제거했습니다.
    # 특정 명령어셋이 필요한 테스트가 생기면 bedrock_set_isa_flags 로
    # 해당 소스에만 플래그를 부착하십시오.
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endforeach()

# ============================================================
# GoogleTest 단위 테스트 (하나의 바이너리로 통합)
# ============================================================
if(TARGET GTest::gtest_main AND UNIT_TEST_SOURCES)
    add_executable(common_unit_tests ${UNIT_TEST_SOURCES})
    target_link_libraries(common_unit_tests PRIVATE ${PROJECT_NAME} GTest::gtest_main)
    if(NOT WIN32)
        target_compile_options(common_unit_tests PRIVATE
            -fno-exceptions -fno-rtti
            # gtest 매크로 전개가 -Weverything과 충돌하는 지점만 끕니다
            # (TEST() 등록 = 전역 생성자, 픽스처 = weak vtable 등)
            -Wno-global-constructors
            -Wno-exit-time-destructors
            -Wno-weak-vtables
            -Wno-used-but-marked-unused
            -Wno-covered-switch-default
            -Wno-disabled-macro-expansion
            -Wno-zero-as-null-pointer-constant
        )
    endif()
    include(GoogleTest)
    # PRE_TEST: 멀티-컨피그에서 마지막 빌드 구성이 테스트 목록을 덮어쓰는
    # POST_BUILD의 한계를 피해, ctest 실행 시점에 해당 구성의 바이너리로
    # 테스트를 열거한다.
    gtest_discover_tests(common_unit_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DISCOVERY_MODE PRE_TEST
    )
endif()
