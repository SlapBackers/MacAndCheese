cmake_minimum_required(VERSION 3.22)
project(MatchMaster VERSION 2.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

add_subdirectory(libs/JUCE)

set(CORE_SOURCES Source/Core/Matcher.cpp)

# ---------------------------------------------------------------- plugin (VST3)
juce_add_plugin(MatchMaster
    COMPANY_NAME "SlapBackers"
    PLUGIN_MANUFACTURER_CODE Slpb
    PLUGIN_CODE Mtch
    FORMATS VST3
    PRODUCT_NAME "MatchMaster"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Mastering
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MatchMaster PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${CORE_SOURCES})

target_compile_definitions(MatchMaster PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MatchMaster
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# JUCE signs the bundle before writing moduleinfo.json, which breaks the seal
# ("damaged" on download). Re-sign last, after every JUCE post-build step.
if(APPLE)
    add_custom_command(TARGET MatchMaster_VST3 POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:MatchMaster_VST3>"
        COMMAND codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:MatchMaster_VST3>"
        VERBATIM)
endif()

# ---------------------------------------------------------------- test / offline tool
juce_add_console_app(MatchMasterCLI PRODUCT_NAME "matchmaster-cli")
target_sources(MatchMasterCLI PRIVATE Tools/cli.cpp ${CORE_SOURCES})
target_include_directories(MatchMasterCLI PRIVATE Source)
target_compile_definitions(MatchMasterCLI PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(MatchMasterCLI
    PRIVATE juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- end-to-end plugin test (no DAW)
juce_add_gui_app(MatchMasterFlowTest PRODUCT_NAME "MatchMasterFlowTest")
target_sources(MatchMasterFlowTest PRIVATE Tools/flowtest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp ${CORE_SOURCES})
target_include_directories(MatchMasterFlowTest PRIVATE Source)
target_compile_definitions(MatchMasterFlowTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(MatchMasterFlowTest
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
