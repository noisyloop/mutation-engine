cmake_minimum_required(VERSION 3.22)
project(MutationEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
  # Apple Silicon + Intel universal binary
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

option(ME_BUILD_PLUGIN "Build the JUCE plugin (fetches JUCE)" ON)
option(ME_BUILD_TESTS  "Build the JUCE-free DSP unit tests" ON)

# ---------------------------------------------------------------------------
# DSP core: header-only, no JUCE dependency, so it is testable on its own.
# ---------------------------------------------------------------------------
add_library(me_dsp INTERFACE)
target_include_directories(me_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_compile_features(me_dsp INTERFACE cxx_std_17)

if(ME_BUILD_TESTS)
  enable_testing()
  add_executable(me_dsp_tests Tests/DspTests.cpp)
  target_link_libraries(me_dsp_tests PRIVATE me_dsp)
  target_compile_options(me_dsp_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)
  add_test(NAME dsp COMMAND me_dsp_tests)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(ME_BUILD_PLUGIN)
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
  FetchContent_MakeAvailable(JUCE)

  set(ME_FORMATS VST3)
  if(APPLE)
    list(APPEND ME_FORMATS AU)   # optional AU on macOS
  endif()

  juce_add_plugin(MutationEngine
    COMPANY_NAME            "NoisyLoop"
    PLUGIN_MANUFACTURER_CODE Nsyl
    PLUGIN_CODE             Mtne
    FORMATS                 ${ME_FORMATS}
    PRODUCT_NAME            "Mutation Engine"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         Fx)

  target_sources(MutationEngine PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

  target_compile_definitions(MutationEngine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

  target_link_libraries(MutationEngine
    PRIVATE
      me_dsp
      juce::juce_audio_utils
      juce::juce_dsp
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)
endif()
