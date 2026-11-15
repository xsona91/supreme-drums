cmake_minimum_required(VERSION 3.22)
project(SupremeDrums VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
# JUCE is downloaded as a zip: no Git or GitHub account needed.
# (A local copy can be used with -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE.)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

if(CMAKE_CROSSCOMPILING)
    set(SD_VST3_MANIFEST FALSE)
else()
    set(SD_VST3_MANIFEST TRUE)
endif()

juce_add_plugin(SupremeDrums
    COMPANY_NAME "Giulia Casu"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Giulia Casu. All rights reserved."
    PRODUCT_NAME "Supreme Drums"
    BUNDLE_ID com.giuliacasu.supremedrums
    PLUGIN_MANUFACTURER_CODE Supa
    PLUGIN_CODE Spdr
    # VST3 on Windows and macOS, AU on macOS (JUCE skips AU on Windows). 64-bit builds.
    FORMATS VST3 AU Standalone
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum Sampler
    # moduleinfo.json is optional; it cannot be generated when cross-compiling.
    VST3_AUTO_MANIFEST ${SD_VST3_MANIFEST})

juce_add_binary_data(SupremeDrumsUI SOURCES ui/index.html)

target_sources(SupremeDrums PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SupremeDrums PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# WebView2: official headers are bundled; the loader DLL is loaded at run time from inside
# the .vst3 bundle, so neither NuGet nor a static library is needed (works with MSVC and MinGW).
if(NOT MINGW) # MinGW builds get compatible WebView2/WRL headers from their toolchain overlay
    target_include_directories(SupremeDrums PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/WebView2/include)
endif()
if(WIN32)
    set(SD_WV2_DLL ${CMAKE_CURRENT_SOURCE_DIR}/third_party/WebView2/x64/WebView2Loader.dll)
    foreach(fmt VST3 Standalone)
        if(TARGET SupremeDrums_${fmt})
            add_custom_command(TARGET SupremeDrums_${fmt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SD_WV2_DLL} $<TARGET_FILE_DIR:SupremeDrums_${fmt}>)
        endif()
    endforeach()
endif()
if(MINGW)
    # Self-contained binary: no MinGW runtime DLLs needed on the user's PC.
    target_link_options(SupremeDrums PUBLIC -static -static-libgcc -static-libstdc++ -lpthread)
    # JUCE 8 no longer tests MinGW: provide the C headers MSVC includes implicitly.
    target_compile_definitions(SupremeDrums PUBLIC JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
    target_sources(SupremeDrums PRIVATE Source/mingw/sd_mingw_shims.c)
endif()

target_link_libraries(SupremeDrums
    PRIVATE
        SupremeDrumsUI
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
