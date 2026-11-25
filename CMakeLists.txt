cmake_minimum_required(VERSION 3.22)
project(Fracture VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

option(FRACTURE_COPY_PLUGIN "Install plugin after build" ON)
include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Fracture
  COMPANY_NAME "Fracture Audio"
  PLUGIN_MANUFACTURER_CODE Frac
  PLUGIN_CODE Frc1
  FORMATS VST3 Standalone
  PRODUCT_NAME "Fracture"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${FRACTURE_COPY_PLUGIN}
  VST3_CATEGORIES Instrument Drum)

juce_generate_juce_header(Fracture)

target_sources(Fracture PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_definitions(Fracture PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(Fracture
  PRIVATE juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags
         juce::juce_recommended_lto_flags
         juce::juce_recommended_warning_flags)
