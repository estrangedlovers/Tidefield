juce_add_plugin(TidefieldTestGain
    PRODUCT_NAME "Tidefield Test Gain"
    COMPANY_NAME "Tidefield"
    BUNDLE_ID "com.tidefield.testgain"
    PLUGIN_MANUFACTURER_CODE Tdfl
    PLUGIN_CODE Tdtg
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(TidefieldTestGain PRIVATE TestGain.cpp)
target_compile_definitions(TidefieldTestGain PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TidefieldTestGain PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

juce_add_plugin(TidefieldTestSine
    PRODUCT_NAME "Tidefield Test Sine"
    COMPANY_NAME "Tidefield"
    BUNDLE_ID "com.tidefield.testsine"
    PLUGIN_MANUFACTURER_CODE Tdfl
    PLUGIN_CODE Tdts
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(TidefieldTestSine PRIVATE TestSine.cpp)
target_compile_definitions(TidefieldTestSine PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TidefieldTestSine PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
