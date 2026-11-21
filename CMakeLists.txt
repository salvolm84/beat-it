cmake_minimum_required(VERSION 3.22)

# Apple Silicon native build, macOS 11+ (first macOS with arm64 support)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(BeatIt VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # for clangd / VS Code
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BEATIT_BUILD_APP   "Build the GUI app" ON)
option(BEATIT_BUILD_TESTS "Build the headless analyzer test" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------- JUCE
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.15.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------------ libsamplerate
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_INSTALL  OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING          OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libsamplerate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG        0.2.2
    GIT_SHALLOW    TRUE)
# libsamplerate 0.2.2 declares cmake_minimum_required < 3.5, which CMake 4 rejects
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(libsamplerate)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# -------------------------------------------------------------- BTrack
# BTrack's own CMake hard-codes a Homebrew libsamplerate, so we only fetch
# the sources and compile them ourselves (Kiss FFT backend, bundled).
FetchContent_Declare(btrack
    GIT_REPOSITORY https://github.com/adamstark/BTrack.git
    GIT_TAG        9d6127618a5679e9caa74c594b88f1d74f0e035f
    SOURCE_SUBDIR  _no_cmake_)   # populate only, don't run its CMakeLists
FetchContent_MakeAvailable(btrack)

add_library(btrack STATIC
    ${btrack_SOURCE_DIR}/src/BTrack.cpp
    ${btrack_SOURCE_DIR}/src/OnsetDetectionFunction.cpp
    ${btrack_SOURCE_DIR}/libs/kiss_fft130/kiss_fft.c)
target_include_directories(btrack PUBLIC
    ${btrack_SOURCE_DIR}/src
    ${btrack_SOURCE_DIR}/libs/kiss_fft130)
target_compile_definitions(btrack PUBLIC USE_KISS_FFT)
target_link_libraries(btrack PUBLIC samplerate)
target_compile_options(btrack PRIVATE -w)

# ------------------------------------------- analysis core (no GUI deps)
# Compiled directly into each target (JUCE modules are INTERFACE libraries,
# so wrapping them in a static lib would duplicate module code).
set(BEATIT_CORE_SOURCES
    src/core/BeatAnalyzer.cpp
    src/core/TempoMap.cpp
    src/core/MidiExporter.cpp
    src/core/AudioPadder.cpp)

set(BEATIT_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# ----------------------------------------------------------------- App
if(BEATIT_BUILD_APP)
juce_add_gui_app(BeatIt
    PRODUCT_NAME "Beat It"
    BUNDLE_ID "com.beatit.tempomapper"
    COMPANY_NAME "Beat It"
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_1024.png
    ICON_SMALL ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon_1024.png
    DOCUMENT_EXTENSIONS wav mp3 aif aiff flac m4a
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(BeatIt PRIVATE
    ${BEATIT_CORE_SOURCES}
    src/app/Main.cpp
    src/app/MainComponent.cpp
    src/app/AudioEngine.cpp
    src/app/WaveformView.cpp
    src/app/TempoCurveView.cpp)

# JUCE's generated .icns only contains the large sizes; replace it with the
# complete set (16..1024 px) made by tools/make_icon.py
add_custom_command(TARGET BeatIt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns
            "$<TARGET_BUNDLE_CONTENT_DIR:BeatIt>/Resources/AppIcon.icns"
    COMMENT "Installing full-size app icon")

target_include_directories(BeatIt PRIVATE src/app src/core)
target_compile_definitions(BeatIt PRIVATE ${BEATIT_COMMON_DEFS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BeatIt,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BeatIt,JUCE_VERSION>")

target_link_libraries(BeatIt PRIVATE
    btrack
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- Test
if(BEATIT_BUILD_TESTS)
    juce_add_console_app(BeatItTest PRODUCT_NAME "BeatItTest")
    target_sources(BeatItTest PRIVATE ${BEATIT_CORE_SOURCES} src/app/AudioEngine.cpp tests/AnalyzerTest.cpp)
    target_include_directories(BeatItTest PRIVATE src/core src/app)
    target_compile_definitions(BeatItTest PRIVATE ${BEATIT_COMMON_DEFS})
    target_link_libraries(BeatItTest PRIVATE
        btrack
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)
endif()
