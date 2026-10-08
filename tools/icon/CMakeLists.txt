# tidefield_icon: draws the app icon and the logo from gui/Logo.h, so the shipped
# PNGs are generated from the same paths the interface draws. Run it after changing
# the mark: tidefield_icon resources/icon resources/fonts/Quicksand-Medium.ttf
juce_add_console_app(tidefield_icon PRODUCT_NAME "tidefield_icon")
target_sources(tidefield_icon PRIVATE Main.cpp)
target_include_directories(tidefield_icon PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(tidefield_icon PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(tidefield_icon PRIVATE
    juce::juce_graphics
    juce::juce_recommended_config_flags)
