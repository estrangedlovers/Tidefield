juce_add_console_app(tidefield_bench PRODUCT_NAME "tidefield_bench")

target_sources(tidefield_bench PRIVATE Main.cpp)

target_compile_definitions(tidefield_bench PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(tidefield_bench PRIVATE
    tidefield_engine
    tidefield_io
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

set_target_properties(tidefield_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
