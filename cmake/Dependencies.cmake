include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# JUCE is only needed by the app, the render harness and io code.
if(TIDEFIELD_BUILD_APP OR TIDEFIELD_BUILD_RENDER)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

if(TIDEFIELD_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.9.1
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
