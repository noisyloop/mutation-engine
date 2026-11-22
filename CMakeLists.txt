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

  add_executable(me_mutation_tests Tests/MutationTests.cpp)
  target_link_libraries(me_mutation_tests PRIVATE me_dsp)
  target_compile_options(me_mutation_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)
  add_test(NAME mutation COMMAND me_mutation_tests)

  add_executable(me_preset_tests Tests/PresetTests.cpp)
  target_link_libraries(me_preset_tests PRIVATE me_dsp)
  target_compile_definitions(me_preset_tests PRIVATE ME_PRESET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Presets")
  target_compile_options(me_preset_tests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)
  add_test(NAME presets COMMAND me_preset_tests)
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

  # Factory presets are embedded in the binary.
  file(GLOB ME_FACTORY_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Presets/*.mepreset)
  juce_add_binary_data(MutationEngineData SOURCES ${ME_FACTORY_PRESETS})
  set(ME_PLUGIN_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Presets/PresetManager.cpp)

  target_sources(MutationEngine PRIVATE ${ME_PLUGIN_SOURCES})

  target_compile_definitions(MutationEngine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

  target_link_libraries(MutationEngine
    PRIVATE
      me_dsp
      MutationEngineData
      juce::juce_audio_utils
      juce::juce_dsp
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)

  # Headless integration test of the real processor (no host / GUI needed).
  option(ME_BUILD_SMOKE_TEST "Build the processor smoke test" ON)
  if(ME_BUILD_SMOKE_TEST)
    juce_add_console_app(MutationEngineSmoke PRODUCT_NAME "MutationEngineSmoke")
    target_sources(MutationEngineSmoke PRIVATE
      Tests/ProcessorSmokeTest.cpp ${ME_PLUGIN_SOURCES})
    target_include_directories(MutationEngineSmoke PRIVATE Source)
    target_compile_definitions(MutationEngineSmoke PRIVATE
      JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MutationEngineSmoke PRIVATE
      me_dsp MutationEngineData juce::juce_audio_utils juce::juce_dsp
      juce::juce_recommended_config_flags)
  endif()

  # Renders the real editor to a PNG (used for the README screenshot).
  option(ME_BUILD_SHOT_TOOL "Build the editor screenshot tool" ON)
  if(ME_BUILD_SHOT_TOOL)
    juce_add_console_app(MutationEngineShot PRODUCT_NAME "MutationEngineShot")
    target_sources(MutationEngineShot PRIVATE Tests/ShotTool.cpp ${ME_PLUGIN_SOURCES})
    target_include_directories(MutationEngineShot PRIVATE Source)
    target_compile_definitions(MutationEngineShot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MutationEngineShot PRIVATE me_dsp MutationEngineData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
  endif()
endif()
