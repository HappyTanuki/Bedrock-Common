# Only include/common.h and include/common/** are external headers. Every other
# subtree under include/ is internal implementation detail.
set(COMMON_HEADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(COMMON_INTERNAL_INCLUDE_DIR "${COMMON_HEADER_SOURCE_DIR}")
set(COMMON_EXTERNAL_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/public_include")

if(NOT EXISTS "${COMMON_HEADER_SOURCE_DIR}/common.h" OR
   NOT EXISTS "${COMMON_HEADER_SOURCE_DIR}/common/archive.h")
    message(FATAL_ERROR "The external common header tree is incomplete.")
endif()

if(NOT EXISTS "${COMMON_HEADER_SOURCE_DIR}/archive/node.h")
    message(FATAL_ERROR "The internal archive header tree is incomplete.")
endif()

# Give consumers a physical include root containing only the external surface.
# configure_file tracks changes and CMake's CONFIGURE_DEPENDS tracks additions.
file(REMOVE_RECURSE "${COMMON_EXTERNAL_INCLUDE_DIR}")
file(GLOB_RECURSE COMMON_EXTERNAL_HEADER_PATHS
    CONFIGURE_DEPENDS
    RELATIVE "${COMMON_HEADER_SOURCE_DIR}"
    "${COMMON_HEADER_SOURCE_DIR}/common/*")
list(APPEND COMMON_EXTERNAL_HEADER_PATHS "common.h")
foreach(relative_path IN LISTS COMMON_EXTERNAL_HEADER_PATHS)
    get_filename_component(relative_dir "${relative_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${COMMON_EXTERNAL_INCLUDE_DIR}/${relative_dir}")
    configure_file(
        "${COMMON_HEADER_SOURCE_DIR}/${relative_path}"
        "${COMMON_EXTERNAL_INCLUDE_DIR}/${relative_path}"
        COPYONLY)
endforeach()

function(common_verify_header_visibility target_name)
    get_target_property(interface_include_dirs
        ${target_name} INTERFACE_INCLUDE_DIRECTORIES)
    if(COMMON_HEADER_SOURCE_DIR IN_LIST interface_include_dirs)
        message(FATAL_ERROR
            "${target_name} exposes internal headers to consumers.")
    endif()
endfunction()
