cmake_minimum_required(VERSION 3.22.0)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
        "Oldest macOS version supported by the compiled binaries")
endif()

project(ohmsick VERSION 0.1.0 LANGUAGES C CXX)

# load fetcher
include(FetchContent)

# juce framework, used for vst3 operation and compilation
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)

# jive extention to juce, used for xml based gui
FetchContent_Declare(
    JIVE
    GIT_REPOSITORY https://github.com/ImJimmi/JIVE.git
    GIT_TAG v1.3.0
    GIT_SHALLOW TRUE
)

# load content dependencies
FetchContent_MakeAvailable(JUCE)
FetchContent_MakeAvailable(JIVE)

add_library(hardware_backend STATIC
    src/backend.h
    src/backend/protocol.cpp
    src/backend/protocol.h
    src/backend/serial_port.cpp
    src/backend/serial_port.h
    src/backend/serial_reader.cpp
    src/backend/serial_reader.h
)
set_target_properties(hardware_backend PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(hardware_backend PUBLIC src)
target_compile_features(hardware_backend PUBLIC cxx_std_17)

add_executable(hardware_parser_tests
    src/hardware_tests.cpp
)
target_compile_features(hardware_parser_tests PRIVATE cxx_std_17)
target_link_libraries(hardware_parser_tests PRIVATE hardware_backend)

juce_add_plugin(ohmsick_vst3
    PRODUCT_NAME "Ohmsick"
    PLUGIN_NAME "Ohmsick"
    DESCRIPTION "Ohmsick"
    COMPANY_NAME "vaight"
    COMPANY_WEBSITE "https://example.com"
    COMPANY_EMAIL "support@example.com"
    BUNDLE_ID "com.vaight.ohmsick"
    PLUGIN_MANUFACTURER_CODE Vaig
    PLUGIN_CODE Vat1
    FORMATS VST3
    VST3_CATEGORIES Instrument External
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
)

juce_add_binary_data(ohmsick_gui_assets
    SOURCES
        src/vst3/gui/layout.xml
)

target_sources(ohmsick_vst3 PRIVATE
    src/vst3.h
    src/vst3/plugin.cpp
    src/vst3/display.cpp
)
target_include_directories(ohmsick_vst3 PRIVATE src)
target_compile_features(ohmsick_vst3 PRIVATE cxx_std_17)
target_compile_definitions(ohmsick_vst3
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    PRIVATE
        JIVE_GUI_ITEMS_HAVE_STYLE_SHEETS=1
)
target_link_libraries(ohmsick_vst3
    PRIVATE
        hardware_backend
        ohmsick_gui_assets
        jive::jive_layouts
        jive::jive_style_sheets
        juce::juce_audio_utils
)

juce_add_gui_app(debug_host
    PRODUCT_NAME "Ohmsick Debug Host"
    COMPANY_NAME "vaight"
    BUNDLE_ID "com.vaight.ohmsick.debughost"
)
target_sources(debug_host PRIVATE
    src/vst3/debugging_host.cpp
)
target_compile_features(debug_host PRIVATE cxx_std_17)
target_compile_definitions(debug_host
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        DEBUG_HOST_BUILD_DIR="${CMAKE_BINARY_DIR}"
)
target_link_libraries(debug_host
    PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
add_dependencies(debug_host ohmsick_vst3)

if(APPLE)
    # Aggregate the deliverables expected from an explicit macOS build.
    add_custom_target(ohmsick_macos
        DEPENDS
            ohmsick_vst3
            debug_host
            hardware_parser_tests
    )
endif()
