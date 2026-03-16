cmake_minimum_required(VERSION 3.22...4.2)
project(SEQMC4 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/JUCE)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(SEQMC4
    COMPANY_NAME "Benedict Singleton"
    BUNDLE_ID "com.benedictsingleton.seq-mc-4"
    PLUGIN_MANUFACTURER_CODE Bdsn
    PLUGIN_CODE Sqmc
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "SEQ-MC-4"

    IS_SYNTH FALSE
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE

    COPY_PLUGIN_AFTER_BUILD TRUE

    VST3_CATEGORIES "Fx" "Tools"
    AU_MAIN_TYPE "kAudioUnitType_MIDIProcessor"
)

clap_juce_extensions_plugin(
    TARGET SEQMC4
    CLAP_ID "com.benedictsingleton.seq-mc-4"
    CLAP_FEATURES "note-effect" "sequencer"
)

target_sources(SEQMC4
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PlaybackEngine.cpp
)

target_include_directories(SEQMC4
    PRIVATE
        Source
)

target_compile_definitions(SEQMC4
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SEQMC4
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
