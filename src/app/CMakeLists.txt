juce_add_gui_app(Tidefield
    PRODUCT_NAME "Tidefield"
    COMPANY_NAME "Tidefield"
    BUNDLE_ID "com.tidefield.app"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Tidefield uses audio input for the live input channel."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input)

# Built-in sounds (synthesized by tools/scripts/make_samples.py), compiled in so a
# first launch has something to play.
juce_add_binary_data(TidefieldBinaryData SOURCES
    ${CMAKE_SOURCE_DIR}/resources/samples/glass.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/chord.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/pluck.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/breath.wav)

target_sources(Tidefield PRIVATE
    Main.cpp
    AudioHost.cpp
    MainComponent.cpp
    SessionController.cpp
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
    TidefieldBinaryData
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
