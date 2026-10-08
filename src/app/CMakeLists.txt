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
    PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string></dict></plist>")

file(GLOB _library_samples CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/samples/library/*.wav)
juce_add_binary_data(TidefieldBinaryData SOURCES
    ${CMAKE_SOURCE_DIR}/resources/samples/glass.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/chord.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/pluck.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/breath.wav
    ${_library_samples}
    ${CMAKE_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/Inter-Medium.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf
    ${CMAKE_SOURCE_DIR}/resources/fonts/Quicksand-Medium.ttf)

add_library(tidefield_app_core INTERFACE)
target_sources(tidefield_app_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/AppCore.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Installation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PerformanceController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FactoryContent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiInputs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginHost.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Remote.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LinkSync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/Controls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/MainView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/Model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/Pages.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/Style.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/TerrainView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/Timeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/Widgets.cpp)
target_link_libraries(tidefield_app_core INTERFACE tidefield_fx_juce tidefield_engine tidefield_io TidefieldBinaryData juce::juce_osc)

target_sources(Tidefield PRIVATE
    Main.cpp
    AudioHost.cpp)
if(TIDEFIELD_WITH_LINK)
    include(FetchContent)
    FetchContent_Declare(link GIT_REPOSITORY https://github.com/Ableton/link.git GIT_TAG Link-3.1.5 GIT_SHALLOW TRUE GIT_SUBMODULES modules/asio-standalone)
    FetchContent_MakeAvailable(link)
    include(${link_SOURCE_DIR}/AbletonLinkConfig.cmake)
    target_link_libraries(Tidefield PRIVATE Ableton::Link)
    target_compile_definitions(Tidefield PRIVATE TIDEFIELD_WITH_LINK=1)
endif()

target_compile_definitions(Tidefield PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=$<IF:$<BOOL:${APPLE}>,1,0>
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Tidefield,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Tidefield,JUCE_VERSION>")

target_link_libraries(Tidefield PRIVATE
    tidefield_app_core
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

option(TIDEFIELD_BUILD_PLUGIN "Build the AU/VST3 plugin" ON)
if(TIDEFIELD_BUILD_PLUGIN)
    set(_tf_formats VST3)
    if(APPLE)
        list(APPEND _tf_formats AU)
    endif()
    juce_add_plugin(TidefieldPlugin
        PRODUCT_NAME "Tidefield"
        COMPANY_NAME "Tidefield"
        COMPANY_COPYRIGHT "Copyright 2026 Tidefield"
        BUNDLE_ID "com.tidefield.plugin"
        VERSION ${PROJECT_VERSION}
        ICON_BIG ${CMAKE_SOURCE_DIR}/resources/icon/icon_1024.png
        PLUGIN_MANUFACTURER_CODE Tdfd
        PLUGIN_CODE Tdfl
        FORMATS ${_tf_formats}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(TidefieldPlugin PRIVATE ${CMAKE_SOURCE_DIR}/src/plugin/PluginProcessor.cpp)
    target_include_directories(TidefieldPlugin PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(TidefieldPlugin PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(TidefieldPlugin PRIVATE
        tidefield_app_core
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

    add_subdirectory(${CMAKE_SOURCE_DIR}/tools/plugincheck ${CMAKE_BINARY_DIR}/tools/plugincheck)
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/tools/icon ${CMAKE_BINARY_DIR}/tools/icon)

add_subdirectory(${CMAKE_SOURCE_DIR}/tools/testfx ${CMAKE_BINARY_DIR}/tools/testfx)
add_subdirectory(${CMAKE_SOURCE_DIR}/tools/hostcheck ${CMAKE_BINARY_DIR}/tools/hostcheck)
