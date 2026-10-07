juce_add_gui_app(Tidefield
    PRODUCT_NAME "Tidefield"
    COMPANY_NAME "Tidefield"
    COMPANY_COPYRIGHT "Copyright 2026 Tidefield"
    BUNDLE_ID "com.tidefield.app"
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_SOURCE_DIR}/resources/icon/icon_1024.png
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Tidefield uses audio input for the live input channel."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input
    # Finder refuses to open the app on older macOS with a clear message instead of
    # it failing at launch.
    PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string></dict></plist>")

# Built-in sounds (synthesized by tools/scripts/make_samples.py) and the Inter
# typeface (SIL Open Font License, resources/fonts/Inter-LICENSE.txt), compiled in.
file(GLOB _library_samples CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/samples/library/*.wav)
juce_add_binary_data(TidefieldBinaryData SOURCES
    ${CMAKE_SOURCE_DIR}/resources/samples/glass.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/chord.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/pluck.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/breath.wav
    ${_library_samples}
    ${CMAKE_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/Inter-Medium.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf)

target_sources(Tidefield PRIVATE
    Main.cpp
    AppCore.cpp
    AudioHost.cpp
    FactoryContent.cpp
    MidiInputs.cpp
    SessionController.cpp
    gui/Controls.cpp
    gui/MainView.cpp
    gui/Model.cpp
    gui/Pages.cpp
    gui/Style.cpp
    gui/TerrainView.cpp
    gui/Widgets.cpp)

target_compile_definitions(Tidefield PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Tidefield,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Tidefield,JUCE_VERSION>")

target_link_libraries(Tidefield PRIVATE
    tidefield_fx_juce
    tidefield_engine
    tidefield_io
    TidefieldBinaryData
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
