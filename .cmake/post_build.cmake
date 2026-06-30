# ============================================================
# Post-Build – Test directory copies + final artifact copies
# ============================================================
# 입력:
#   SUB_PROJECT_NAME
#   COMMON_LINKAGE
# ============================================================

# ============================================================
# Tests (top-level only)
# ============================================================
if (${PROJECT_IS_TOP_LEVEL})
    set(BUILD_TESTING ON)

    include(CTest)
    enable_testing()

    add_subdirectory(test)

    foreach(TEST_SUBDIR "test")
        # STATIC 빌드면 결과물이 .a / .lib이라 실행 디렉터리에 둘 필요 없음
        if(COMMON_LINKAGE STREQUAL "DYNAMIC")
            add_custom_command(TARGET ${SUB_PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${SUB_PROJECT_NAME}>"
                    "${CMAKE_BINARY_DIR}/${TEST_SUBDIR}"
                COMMENT "Copying ${SUB_PROJECT_NAME} DLL/so to ${TEST_SUBDIR} output directory"
            )
        endif()
    endforeach()
endif()

# ============================================================
# Post-Build – Copy artifacts to binary root
# ============================================================
# STATIC 빌드 시엔 결과물이 .a/.lib이라 복사 불필요.
if(COMMON_LINKAGE STREQUAL "DYNAMIC")
    add_custom_command(TARGET ${SUB_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${SUB_PROJECT_NAME}>"
            "${CMAKE_BINARY_DIR}"
        COMMENT "Copying ${SUB_PROJECT_NAME} DLL/so to binary output directory"
    )
endif()
