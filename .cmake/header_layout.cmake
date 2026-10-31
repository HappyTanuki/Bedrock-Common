# Select and validate Common's public surface; staging is owned by the shared policy.
set(COMMON_HEADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(COMMON_INTERNAL_INCLUDE_DIR "${COMMON_HEADER_SOURCE_DIR}")
if(NOT EXISTS "${COMMON_HEADER_SOURCE_DIR}/common.h" OR
   NOT EXISTS "${COMMON_HEADER_SOURCE_DIR}/common/archive.h")
    message(FATAL_ERROR "The external common header tree is incomplete.")
endif()
if(NOT EXISTS "${COMMON_HEADER_SOURCE_DIR}/archive/node.h")
    message(FATAL_ERROR "The internal archive header tree is incomplete.")
endif()

function(common_verify_header_visibility target_name)
    _BedrockBuildConfig_assert_header_visibility(${target_name} SOURCE_ROOT "${COMMON_HEADER_SOURCE_DIR}")
endfunction()
