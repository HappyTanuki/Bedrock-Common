file(GLOB TEST_SOURCES "*.cc")

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