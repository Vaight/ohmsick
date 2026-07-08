cmake_minimum_required(VERSION 3.22.0)
project(vst3arduinothing VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

add_library(hardware_backend STATIC
    src/hardware.cpp
)
set_target_properties(hardware_backend PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(hardware_backend PUBLIC src)
target_compile_features(hardware_backend PUBLIC cxx_std_17)

add_executable(vst3arduinothing
    src/cli.cpp
)
target_compile_features(vst3arduinothing PRIVATE cxx_std_17)
target_link_libraries(vst3arduinothing PRIVATE hardware_backend)

add_executable(hardware_parser_tests
    src/hardware_tests.cpp
)
target_compile_features(hardware_parser_tests PRIVATE cxx_std_17)
target_link_libraries(hardware_parser_tests PRIVATE hardware_backend)

juce_add_plugin(vst3arduinothing_vst3
    PRODUCT_NAME "VST3 Arduino Thing"
    PLUGIN_NAME "VST3 Arduino Thing"
    COMPANY_NAME "vaight"
    COMPANY_WEBSITE "https://example.com"
    COMPANY_EMAIL "support@example.com"
    BUNDLE_ID "com.vaight.vst3arduinothing"
    PLUGIN_MANUFACTURER_CODE Vaig
    PLUGIN_CODE Vat1
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

target_sources(vst3arduinothing_vst3 PRIVATE
    src/vst3/plugin.cpp
)
target_include_directories(vst3arduinothing_vst3 PRIVATE src)
target_compile_features(vst3arduinothing_vst3 PRIVATE cxx_std_17)
target_compile_definitions(vst3arduinothing_vst3
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(vst3arduinothing_vst3
    PRIVATE
        hardware_backend
        juce::juce_audio_utils
)
