cmake_minimum_required(VERSION 3.22)

project(MeloDIG VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

add_subdirectory(libs/JUCE)

set(MELODIG_FORMATS VST3)
if(APPLE)
    list(APPEND MELODIG_FORMATS AU)
endif()

juce_add_plugin(MeloDIG
    COMPANY_NAME "Carlito"
    BUNDLE_ID "org.melodig.melodig"
    PLUGIN_MANUFACTURER_CODE Mdig
    PLUGIN_CODE Mdg1
    FORMATS ${MELODIG_FORMATS}
    PRODUCT_NAME "Carlitaccuracy"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Pitch_Shift
    AU_MAIN_TYPE kAudioUnitType_Effect
    DESCRIPTION "Carlitaccuracy: note-by-note pitch, timing, formant and volume editing of a monophonic recording")

target_sources(MeloDIG PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Style.cpp)

# Fonts (SIL Open Font License), fetched into fonts/ by the CI workflow.
juce_add_binary_data(MeloDIGData SOURCES
    fonts/BagelFatOne-Latin.ttf
    fonts/Anton-Regular.ttf
    fonts/SpaceGrotesk-Medium.ttf
    fonts/SpaceGrotesk-Bold.ttf)
set_target_properties(MeloDIGData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(MeloDIG PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MeloDIG
    PRIVATE
        MeloDIGData
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
