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
