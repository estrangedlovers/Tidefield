cmake_minimum_required(VERSION 3.24)

project(Tidefield VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    set(_tf_app_default ON)
else()
    set(_tf_app_default OFF)
endif()

option(TIDEFIELD_BUILD_APP    "Build the Tidefield standalone app" ${_tf_app_default})
option(TIDEFIELD_BUILD_TESTS  "Build unit tests"                   ON)
option(TIDEFIELD_BUILD_RENDER "Build the offline render harness"   ON)

include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# tidefield_dsp: pure C++20 DSP. No JUCE, no engine, no UI.
# ---------------------------------------------------------------------------
add_library(tidefield_dsp STATIC
    src/dsp/fx/Limiter.cpp
    src/dsp/fx/ProcessorFactory.cpp
    src/dsp/fx/delay/TapeDelay.cpp
    src/dsp/fx/medium/Medium.cpp
    src/dsp/fx/reverb/FdnReverb.cpp
    src/dsp/sources/bloom/BloomSampler.cpp
    src/dsp/sources/drone/DroneGenerator.cpp
    src/dsp/sources/granular/GranularCloud.cpp
    src/dsp/sources/resonator/ResonatorBank.cpp
)
target_include_directories(tidefield_dsp PUBLIC src)
tf_set_warnings(tidefield_dsp)

# ---------------------------------------------------------------------------
# tidefield_engine: parameters, control flow, mixer, master chain. No JUCE.
# ---------------------------------------------------------------------------
add_library(tidefield_engine STATIC
    src/engine/Engine.cpp
    src/engine/params/ParamRegistry.cpp
    src/engine/master/MasterChain.cpp
    src/engine/scene/SceneManager.cpp
    src/engine/mix/FxManager.cpp
    src/engine/capture/CatchManager.cpp
    src/engine/midi/MidiManager.cpp
)
target_link_libraries(tidefield_engine PUBLIC tidefield_dsp)
tf_set_warnings(tidefield_engine)

# ---------------------------------------------------------------------------
# tidefield_io: JUCE-based file and session code. An INTERFACE library: each
# executable compiles these sources against its own JUCE modules, so JUCE module
# code is never linked twice.
# ---------------------------------------------------------------------------
add_library(tidefield_io INTERFACE)
target_sources(tidefield_io INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/AudioFileIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/Session.cpp)
target_link_libraries(tidefield_io INTERFACE tidefield_engine)

if(TIDEFIELD_BUILD_RENDER)
    add_subdirectory(tools/render)
endif()

if(TIDEFIELD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TIDEFIELD_BUILD_APP)
    add_subdirectory(src/app)
endif()
