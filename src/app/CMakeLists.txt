juce_add_gui_app(Tidefield
    PRODUCT_NAME "Tidefield"
    COMPANY_NAME "Tidefield"
    BUNDLE_ID "com.tidefield.app"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Tidefield uses audio input for the live input channel."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input
    NEEDS_WEB_BROWSER TRUE)

# Built-in sounds (synthesized by tools/scripts/make_samples.py), compiled in so a
# first launch has something to play.
juce_add_binary_data(TidefieldBinaryData SOURCES
    ${CMAKE_SOURCE_DIR}/resources/samples/glass.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/chord.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/pluck.wav
    ${CMAKE_SOURCE_DIR}/resources/samples/breath.wav)

target_sources(Tidefield PRIVATE
    Main.cpp
    AppCore.cpp
    AudioHost.cpp
    ClassicUI.cpp
    MidiInputs.cpp
    SessionController.cpp
    TerrainPad.cpp
    ui/KnobPanel.cpp
    ui/Pages.cpp
    ui/ParamKnob.cpp
    web/WebUI.cpp)

target_compile_definitions(Tidefield PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    TIDEFIELD_UI_SOURCE_DIST="${CMAKE_SOURCE_DIR}/ui/dist"
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

# ---------------------------------------------------------------------------
# Web UI (React + Vite in ui/). Built with npm when available and copied next to
# the app (macOS: Contents/Resources/ui). Without npm the app falls back to the JUCE
# panel, or set TIDEFIELD_UI_DEV=http://localhost:5173 and run `npm run dev` in ui/.
# ---------------------------------------------------------------------------
option(TIDEFIELD_BUILD_UI "Build the React UI with npm" ON)
find_program(TIDEFIELD_NPM npm)
if(TIDEFIELD_BUILD_UI AND TIDEFIELD_NPM)
    set(_ui_dir ${CMAKE_SOURCE_DIR}/ui)
    # Recursive glob for src/ only: a recursive pattern like ui/index.html would also
    # match ui/dist/index.html (the output) and everything in node_modules.
    file(GLOB_RECURSE _ui_sources CONFIGURE_DEPENDS ${_ui_dir}/src/*)
    list(APPEND _ui_sources ${_ui_dir}/index.html ${_ui_dir}/package.json ${_ui_dir}/package-lock.json ${_ui_dir}/vite.config.ts)
    add_custom_command(
        OUTPUT ${_ui_dir}/dist/index.html
        COMMAND ${TIDEFIELD_NPM} ci --no-audit --no-fund
        COMMAND ${TIDEFIELD_NPM} run build
        WORKING_DIRECTORY ${_ui_dir}
        DEPENDS ${_ui_sources}
        COMMENT "Building the Tidefield web UI"
        VERBATIM)
    add_custom_target(tidefield_ui DEPENDS ${_ui_dir}/dist/index.html)
    add_dependencies(Tidefield tidefield_ui)

    if(APPLE)
        set(_ui_dest "$<TARGET_BUNDLE_CONTENT_DIR:Tidefield>/Resources/ui")
    else()
        set(_ui_dest "$<TARGET_FILE_DIR:Tidefield>/ui")
    endif()
    add_custom_command(TARGET Tidefield POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_ui_dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_ui_dir}/dist "${_ui_dest}"
        COMMENT "Copying the web UI into the app")
endif()
