juce_add_console_app(tidefield_hostcheck PRODUCT_NAME "tidefield_hostcheck")
target_sources(tidefield_hostcheck PRIVATE Main.cpp ${CMAKE_SOURCE_DIR}/src/app/PluginHost.cpp)
target_include_directories(tidefield_hostcheck PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(tidefield_hostcheck PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=$<IF:$<BOOL:${APPLE}>,1,0>
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(tidefield_hostcheck PRIVATE
    tidefield_engine
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)
add_dependencies(tidefield_hostcheck TidefieldTestGain_VST3)
if(TIDEFIELD_BUILD_TESTS)
    if(APPLE)
        add_test(NAME hosting COMMAND tidefield_hostcheck $<TARGET_BUNDLE_DIR:TidefieldTestGain_VST3>/..)
    else()
        add_test(NAME hosting COMMAND tidefield_hostcheck $<TARGET_FILE_DIR:TidefieldTestGain_VST3>)
    endif()
endif()
if(APPLE)
    target_link_libraries(tidefield_hostcheck PRIVATE "-framework CoreAudioKit")
endif()
