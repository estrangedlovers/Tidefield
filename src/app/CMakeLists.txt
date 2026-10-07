juce_add_gui_app(Tidefield
    PRODUCT_NAME "Tidefield"
    COMPANY_NAME "Tidefield"
    BUNDLE_ID "com.tidefield.app"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Tidefield uses audio input for the live input channel."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input)

target_sources(Tidefield PRIVATE
    Main.cpp
    AudioHost.cpp
    MainComponent.cpp
    TerrainPad.cpp
    ui/KnobPanel.cpp
    ui/Pages.cpp
    ui/ParamKnob.cpp)

target_compile_definitions(Tidefield PRIVATE
    JUCE_WEB_BROWSER=0            # turned on in phase 6 for the React UI
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Tidefield,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Tidefield,JUCE_VERSION>")

target_link_libraries(Tidefield PRIVATE
    tidefield_engine
    tidefield_io
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
