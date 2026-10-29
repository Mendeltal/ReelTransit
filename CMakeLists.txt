cmake_minimum_required(VERSION 3.22)
project(ReelTransit VERSION 1.0.0)

add_subdirectory(JUCE)

juce_add_plugin(ReelTransit
    COMPANY_NAME "Tal Mendel"
    PLUGIN_MANUFACTURER_CODE TlMd
    PLUGIN_CODE Rtrn
    FORMATS VST3 AU Standalone
    BUNDLE_ID com.talmendel.reeltransit
    PRODUCT_NAME "Reel Transit"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion)

file(GLOB ASSET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/*.png)
juce_add_binary_data(ReelTransitAssets SOURCES ${ASSET_FILES})
set_target_properties(ReelTransitAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(ReelTransit PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(ReelTransit PRIVATE cxx_std_17)
target_compile_definitions(ReelTransit PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(ReelTransit PRIVATE
    ReelTransitAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
