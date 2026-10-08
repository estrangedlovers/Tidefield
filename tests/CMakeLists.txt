include(Catch)

add_executable(tidefield_tests
    support/AllocationGuard.cpp
    dsp/SmootherTests.cpp
    dsp/FilterTests.cpp
    dsp/LimiterTests.cpp
    dsp/DroneTests.cpp
    dsp/Phase3DspTests.cpp
    dsp/Phase8FxTests.cpp
    dsp/AutoMasterTests.cpp
    engine/SpscQueueTests.cpp
    engine/ParamTests.cpp
    engine/MasterChainTests.cpp
    engine/EngineTests.cpp
    engine/TerrainTests.cpp
    engine/Phase3EngineTests.cpp
    engine/Phase4Tests.cpp
    engine/MidiTests.cpp
    engine/Phase7Tests.cpp
    engine/Phase8SourcesTests.cpp
    engine/Phase8PerformTests.cpp
    engine/PathTests.cpp
    engine/TempoTests.cpp
    engine/GestureTests.cpp
    engine/ModulationTests.cpp
    engine/MonkeyTests.cpp)

target_include_directories(tidefield_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tidefield_tests PRIVATE tidefield_engine Catch2::Catch2WithMain)

catch_discover_tests(tidefield_tests)

if(TARGET juce::juce_core)
    juce_add_console_app(tidefield_io_tests PRODUCT_NAME "tidefield_io_tests")
    target_sources(tidefield_io_tests PRIVATE io/SessionTests.cpp io/UiProtocolTests.cpp io/RecorderTests.cpp io/JuceEffectTests.cpp)
    target_include_directories(tidefield_io_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(tidefield_io_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(tidefield_io_tests PRIVATE
        tidefield_io
        tidefield_fx_juce
        Catch2::Catch2WithMain
        juce::juce_audio_processors
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)
    set_target_properties(tidefield_io_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    catch_discover_tests(tidefield_io_tests)
endif()
