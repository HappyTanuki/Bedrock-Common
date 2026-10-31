#=== Context
# The Bedrock product adds cmake/_BedrockBuildConfig before Common, so the
# package is already loaded when target Bedrock exists. Standalone Common must
# locate or fetch the config-only package itself.

include(FetchContent)

#=== Source selection
# Precedence: an explicit FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG, then the
# parent Bedrock checkout containing this Common, then a sibling Bedrock
# checkout, then the pinned revision. An explicit override is never replaced,
# and discovered paths are not cached so separate build trees stay independent.
if(NOT TARGET Bedrock)
    set(_bedrock_build_config_discovered FALSE)
    if(NOT FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG)
        set(_bedrock_build_config_candidates
            "${CMAKE_CURRENT_LIST_DIR}/../.."
            "${CMAKE_CURRENT_LIST_DIR}/../../Bedrock")
        foreach(_candidate IN LISTS _bedrock_build_config_candidates)
            if(EXISTS "${_candidate}/cmake/_BedrockBuildConfig/CMakeLists.txt")
                get_filename_component(_bedrock_build_config_source "${_candidate}" ABSOLUTE)
                set(FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG
                    "${_bedrock_build_config_source}")
                set(_bedrock_build_config_discovered TRUE)
                message(STATUS
                    "Using local Bedrock build config: ${FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG}")
                break()
            endif()
        endforeach()
    endif()

    FetchContent_Declare(_BedrockBuildConfig
        GIT_REPOSITORY https://github.com/HappyTanuki/Bedrock.git
        GIT_TAG ea9cac1c1a17734bdcd408996373a54cde94e212
        GIT_SUBMODULES ""
        GIT_SUBMODULES_RECURSE FALSE
        SOURCE_SUBDIR cmake/_BedrockBuildConfig)
    FetchContent_MakeAvailable(_BedrockBuildConfig)
    # FetchContent caches the override variable from its current value; drop a
    # discovered path so the next configure evaluates the layout again.
    if(_bedrock_build_config_discovered)
        unset(FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG CACHE)
    endif()
endif()

#=== Validation
# Check readiness through a real provider API, never a fixture-only symbol.
if(NOT COMMAND _BedrockBuildConfig_set_language_properties)
    if(TARGET Bedrock)
        message(FATAL_ERROR
            "Target Bedrock exists, but its build-config package is not loaded. "
            "Add cmake/_BedrockBuildConfig before adding Bedrock-Common.")
    elseif(FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG)
        message(FATAL_ERROR
            "The selected Bedrock source does not provide the build-config package: "
            "${FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG}/cmake/_BedrockBuildConfig/CMakeLists.txt "
            "is missing. Point FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG at a "
            "Bedrock repository root.")
    else()
        message(FATAL_ERROR
            "The pinned Bedrock revision does not provide the build-config package. "
            "Set FETCHCONTENT_SOURCE_DIR__BEDROCKBUILDCONFIG to a local Bedrock "
            "checkout containing cmake/_BedrockBuildConfig.")
    endif()
endif()
