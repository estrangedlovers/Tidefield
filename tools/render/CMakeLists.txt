juce_add_console_app(tidefield_render PRODUCT_NAME "tidefield_render")

target_sources(tidefield_render PRIVATE
    Main.cpp
    Score.cpp
    Analysis.cpp)

target_compile_definitions(tidefield_render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(tidefield_render PRIVATE
    tidefield_engine
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

# Put the binary at build/<preset>/tools/render/tidefield_render instead of JUCE's
# per-config artefacts folder, so scripts and docs can use one path.
set_target_properties(tidefield_render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
