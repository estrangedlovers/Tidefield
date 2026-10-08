set(USER_EFFECT_SOURCES
)

add_library(tidefield_fx_juce INTERFACE)
target_sources(tidefield_fx_juce INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/UserEffects.cpp
    ${USER_EFFECT_SOURCES})
target_include_directories(tidefield_fx_juce INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tidefield_fx_juce INTERFACE tidefield_dsp)
