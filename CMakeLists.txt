cmake_minimum_required(VERSION 3.22)
project(Seda VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE (cualquier 8.x sirve; también compila con 7.x) -----------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)
# Alternativa si ya tienes JUCE clonado:  add_subdirectory(/ruta/a/JUCE JUCE)

option(SEDA_COPY_AFTER_BUILD "Copy the plugin to the system plugin folder after building" ON)

set(SEDA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SEDA_FORMATS AU)
endif()

juce_add_plugin(Seda
    COMPANY_NAME "TuNombre"
    PLUGIN_MANUFACTURER_CODE Tnom
    PLUGIN_CODE Seda
    FORMATS ${SEDA_FORMATS}
    PRODUCT_NAME "Seda Vocal Reverb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${SEDA_COPY_AFTER_BUILD})

target_sources(Seda PRIVATE
    Source/PluginProcessor.cpp)

target_compile_definitions(Seda PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Seda
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Pruebas del DSP (sin JUCE):  cmake --build build --target seda_dsp_test ----
add_executable(seda_dsp_test EXCLUDE_FROM_ALL Tests/dsp_test.cpp)
target_include_directories(seda_dsp_test PRIVATE Source)
