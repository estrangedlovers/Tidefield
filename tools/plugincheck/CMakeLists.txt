juce_add_console_app(tidefield_plugincheck PRODUCT_NAME "tidefield_plugincheck")
target_sources(tidefield_plugincheck PRIVATE Main.cpp)
target_compile_definitions(tidefield_plugincheck PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=$<IF:$<BOOL:${APPLE}>,1,0>
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(tidefield_plugincheck PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
add_dependencies(tidefield_plugincheck TidefieldPlugin_VST3)
