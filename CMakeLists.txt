# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  CMakeLists.txt  -  Cross-platform OBS plugin build.
#
#    Windows  : MSVC  (/utf-8 /W4), x64
#    macOS    : Apple Clang, Universal2 (arm64 + x86_64)
#    Linux    : GCC/Clang (used for CI + headless DSP verification)
#
#  Options:
#    -DMODEL600_PRO_EDITION=ON   build the Pro binary (adds UNIT 05 + voice).
#                          =OFF  Standard binary; Pro code is physically removed.
# =============================================================================
cmake_minimum_required(VERSION 3.21)

project(model600
    VERSION 1.0.0
    DESCRIPTION "SEVENTHWELL MODEL 600 // BROADCAST SUITE"
    LANGUAGES CXX)

option(MODEL600_PRO_EDITION "Build the Pro edition (UNIT 05 + voice director)" ON)
option(MODEL600_BUILD_TESTS "Build the headless Layer-1/2 self-tests" OFF)
option(MODEL600_BUILD_GUI_SAMPLES "Build the GUI sample renderer (dev tool)" OFF)
option(MODEL600_BUILD_PLUGIN "Build the OBS plugin module (needs the libobs SDK)" ON)
option(MODEL600_BUILD_PORTABLE_CHECK
       "Compile every OBS-independent source with warnings as errors (CI on Windows / macOS)" OFF)
# Voice director engine. macOS: embedded whisper.cpp (the OS speech service is
# unusable inside OBS, see README). Windows: SAPI. Linux: none by default.
if(APPLE)
    set(m600_whisper_default ON)
else()
    set(m600_whisper_default OFF)
endif()
option(MODEL600_VOICE_WHISPER "Voice director uses the embedded whisper.cpp recognizer (Pro)" ${m600_whisper_default})
option(MODEL600_WHISPER_TESTS "Build the whisper end-to-end test (needs MODEL600_BUILD_TESTS)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# MSVC: sources and strings are UTF-8 (Japanese UI text); big objects for the
# embedded faceplate assets.
if(MSVC)
    add_compile_options(/utf-8 /bigobj)
endif()

# Strict warnings for our own code; third-party headers (Qt, SDKs) are external.
function(m600_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc /external:anglebrackets /external:W0)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

if(APPLE)
    # Universal2 fat binary. (Honoured only by the Apple Clang toolchain.)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

# --- Qt ----------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Older Qt 6 builds (e.g. the obs-deps Qt) add "-framework AGL" to
# WrapOpenGL::WrapOpenGL even when AGL is absent; the macOS 26 SDK (Xcode 26)
# no longer ships AGL, so the link fails. Nothing here uses AGL: drop it.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(m600_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(m600_gl_libs)
        list(FILTER m600_gl_libs EXCLUDE REGEX "AGL")
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${m600_gl_libs}")
    endif()
endif()

# --- Embedded speech recognizer (whisper.cpp) --------------------------------
set(M600_PLUGIN_WHISPER OFF)
if(MODEL600_BUILD_PLUGIN AND MODEL600_PRO_EDITION AND MODEL600_VOICE_WHISPER)
    set(M600_PLUGIN_WHISPER ON)
endif()
if(M600_PLUGIN_WHISPER OR (MODEL600_BUILD_TESTS AND MODEL600_WHISPER_TESTS)
   OR (MODEL600_BUILD_PORTABLE_CHECK AND MODEL600_PRO_EDITION AND MODEL600_VOICE_WHISPER))
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/whisper.cmake)
endif()

if(MODEL600_BUILD_PLUGIN)
# --- libobs + frontend-api ---------------------------------------------------
# Prefer the upstream CMake package; fall back to a manual search so the build
# works on distro packages (e.g. Ubuntu libobs-dev) that ship no config file.
find_package(libobs QUIET)
find_package(obs-frontend-api QUIET)

if(NOT TARGET OBS::libobs)
    find_path(OBS_INCLUDE_DIR obs-module.h
        PATH_SUFFIXES obs libobs
        PATHS /usr/include /usr/local/include /opt/homebrew/include)
    find_library(OBS_LIB NAMES obs libobs
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib /usr/lib/x86_64-linux-gnu)
    find_library(OBS_FRONTEND_LIB NAMES obs-frontend-api
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib /usr/lib/x86_64-linux-gnu)
    if(NOT OBS_INCLUDE_DIR OR NOT OBS_LIB)
        message(FATAL_ERROR "libobs not found. Install the OBS Studio SDK / libobs-dev, "
                            "or point CMAKE_PREFIX_PATH at your OBS build.")
    endif()
    add_library(OBS::libobs UNKNOWN IMPORTED)
    set_target_properties(OBS::libobs PROPERTIES
        IMPORTED_LOCATION "${OBS_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${OBS_INCLUDE_DIR}")
endif()

# --- Sources -----------------------------------------------------------------
set(MODEL600_SOURCES
    src/plugin-main.cpp
    src/audio-filter.cpp
    src/custom-ui-dialog.cpp
    src/hw-panel.cpp
    src/ui-widgets.cpp)

set(MODEL600_HEADERS
    src/shared-state.hpp
    src/dsp-core.hpp
    src/voice-command-multilingual.hpp
    src/license-manager.hpp
    src/update-checker.hpp
    src/os-conflict-shield.hpp
    src/custom-ui-dialog.hpp
    src/hw-panel.hpp
    src/ui-theme.hpp
    src/ui-widgets.hpp)

# Bundled UI assets: Inter + Noto Sans JP subset (OFL 1.1) and the Blender-
# rendered parts (tools/blender/render_parts.py). Served as :/m600/...
set(MODEL600_UI_RESOURCES
    resources/fonts/Inter-Regular.otf
    resources/fonts/Inter-Medium.otf
    resources/fonts/Inter-SemiBold.otf
    resources/fonts/Inter-Bold.otf
    resources/fonts/NotoSansJP-Subset-Regular.otf
    resources/fonts/NotoSansJP-Subset-Bold.otf
    resources/parts/fader_cap_v.png
    resources/parts/fader_cap_h.png
    resources/parts/knob.png
    resources/parts/screw.png
    resources/parts/led_off.png
    resources/parts/led_green.png
    resources/parts/led_amber.png
    resources/parts/led_red.png
    resources/parts/key_default.png
    resources/parts/key_active.png
    resources/parts/key_danger.png
    resources/parts/key_primary.png)

# GUI v2 hardware faceplates (docs/design/v2): Blender backgrounds, knob
# filmstrips and part sprites, packed by docs/design/v2/pack_assets.py, which
# also writes the file list. Served as :/m600/hw/...
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/hw/FILES.txt")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/resources/hw/FILES.txt" MODEL600_HW_FILES)
    list(TRANSFORM MODEL600_HW_FILES PREPEND "resources/")
    list(APPEND MODEL600_UI_RESOURCES ${MODEL600_HW_FILES})
else()
    message(WARNING "resources/hw/FILES.txt is missing: run docs/design/v2/pack_assets.py. "
                    "The console will fall back to plain drawing.")
endif()

if(M600_PLUGIN_WHISPER)
    list(APPEND MODEL600_SOURCES src/voice-whisper.cpp src/voice-whisper.hpp src/voice-segmenter.hpp)
elseif(WIN32)
    list(APPEND MODEL600_SOURCES src/voice-command-win.cpp)
endif()

add_library(model600 MODULE ${MODEL600_SOURCES} ${MODEL600_HEADERS})

if(M600_PLUGIN_WHISPER)
    target_link_libraries(model600 PRIVATE whisper)
    target_compile_definitions(model600 PRIVATE MODEL600_VOICE_WHISPER=1)
    if(NOT APPLE AND NOT MSVC)
        # Keep the embedded ggml/whisper symbols private to this module, so
        # another plugin bundling its own whisper.cpp can never bind to ours.
        target_link_options(model600 PRIVATE "LINKER:--exclude-libs,ALL")
    endif()
    if(APPLE)
        # Ships in model600.plugin/Contents/Resources (the module data path).
        target_sources(model600 PRIVATE "${M600_WHISPER_MODEL}")
        set_source_files_properties("${M600_WHISPER_MODEL}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
endif()

target_include_directories(model600 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
qt_add_resources(model600 "m600_ui" PREFIX "/m600" BASE "resources" FILES ${MODEL600_UI_RESOURCES})

target_link_libraries(model600 PRIVATE
    OBS::libobs
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

if(TARGET OBS::obs-frontend-api)
    target_link_libraries(model600 PRIVATE OBS::obs-frontend-api)
elseif(OBS_FRONTEND_LIB)
    target_link_libraries(model600 PRIVATE ${OBS_FRONTEND_LIB})
endif()

if(MODEL600_PRO_EDITION)
    target_compile_definitions(model600 PRIVATE MODEL600_PRO_EDITION=1)
endif()

# --- Warnings / platform specifics -------------------------------------------
if(MSVC)
    target_compile_options(model600 PRIVATE /utf-8 /W4 /permissive- /EHsc)
    target_compile_definitions(model600 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(model600 PRIVATE sapi ole32 oleaut32 advapi32 shell32)
else()
    target_compile_options(model600 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
    # Loadable bundle, the layout OBS scans in ~/Library/Application Support/obs-studio/plugins:
    # model600.plugin/Contents/{Info.plist, MacOS/model600}.
    string(TIMESTAMP M600_YEAR "%Y")
    set_target_properties(model600 PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION plugin
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.seventhwell.model600"
        MACOSX_BUNDLE_BUNDLE_NAME "model600"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "(c) ${M600_YEAR} SEVENTHWELL")
    # Licence notices travel inside the signed bundle.
    target_sources(model600 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
                                PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
    set_target_properties(model600 PROPERTIES PREFIX "" SUFFIX ".dll")
else()
    set_target_properties(model600 PROPERTIES PREFIX "")
endif()

# --- Install (standard OBS plugin layout) ------------------------------------
if(WIN32)
    # A MODULE library is a LIBRARY artifact on every platform, .dll included.
    install(TARGETS model600 LIBRARY DESTINATION obs-plugins/64bit COMPONENT model600)
    set(m600_data_dir data/obs-plugins/model600)
elseif(APPLE)
    install(TARGETS model600 LIBRARY DESTINATION . COMPONENT model600)
else()
    install(TARGETS model600 LIBRARY DESTINATION lib/obs-plugins COMPONENT model600)
    set(m600_data_dir share/obs/obs-plugins/model600)
endif()
# On macOS the model is already inside the bundle.
if(M600_PLUGIN_WHISPER AND NOT APPLE)
    install(FILES "${M600_WHISPER_MODEL}" DESTINATION ${m600_data_dir} COMPONENT model600)
endif()

endif() # MODEL600_BUILD_PLUGIN

# --- Optional headless self-tests (Layer 1/2, no OBS/GUI) --------------------
if(MODEL600_BUILD_TESTS)
    enable_testing()
    add_executable(m600_test_dsp tests/test_dsp.cpp)
    target_include_directories(m600_test_dsp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    find_package(Threads REQUIRED)
    target_link_libraries(m600_test_dsp PRIVATE Threads::Threads)
    if(NOT WIN32 AND NOT APPLE)
        target_link_libraries(m600_test_dsp PRIVATE rt)
    endif()
    add_test(NAME dsp COMMAND m600_test_dsp)

    add_executable(m600_test_voice tests/test_voice.cpp)
    target_include_directories(m600_test_voice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME voice COMMAND m600_test_voice)

    # Loads the built plugin into the real libobs (headless Qt), like OBS does.
    if(MODEL600_BUILD_PLUGIN AND UNIX AND NOT APPLE)
        add_executable(m600_test_obs_load tests/test_obs_load.cpp)
        find_package(X11 REQUIRED)
        target_link_libraries(m600_test_obs_load PRIVATE OBS::libobs Qt6::Widgets X11::X11)
        add_dependencies(m600_test_obs_load model600)
        if(MODEL600_PRO_EDITION)
            set(m600_expected_filters 5)
        else()
            set(m600_expected_filters 4)
        endif()
        set(m600_obs_dir "${CMAKE_CURRENT_BINARY_DIR}/obs-load-test")
        file(MAKE_DIRECTORY "${m600_obs_dir}/data" "${m600_obs_dir}/config")
        # Needs an X display: CI runs ctest under xvfb-run.
        add_test(NAME obs_load COMMAND m600_test_obs_load $<TARGET_FILE:model600>
                 "${m600_obs_dir}/data" "${m600_obs_dir}/config" ${m600_expected_filters})
        set_tests_properties(obs_load PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    endif()

    if(MODEL600_WHISPER_TESTS)
        add_executable(m600_test_whisper tests/test_whisper.cpp src/voice-whisper.cpp)
        target_include_directories(m600_test_whisper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(m600_test_whisper PRIVATE whisper Threads::Threads)
        m600_strict_warnings(m600_test_whisper)
        add_test(NAME whisper COMMAND m600_test_whisper "${M600_WHISPER_MODEL}"
                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/voice")
        set_tests_properties(whisper PROPERTIES TIMEOUT 600)
    endif()
endif()

# --- Optional portable compile check (no OBS SDK needed) ---------------------
if(MODEL600_BUILD_PORTABLE_CHECK)
    set(M600_PORTABLE_SOURCES
        tools/portable_check/portable_check.cpp
        src/custom-ui-dialog.cpp src/custom-ui-dialog.hpp
        src/hw-panel.cpp src/hw-panel.hpp
        src/ui-widgets.cpp src/ui-widgets.hpp
        src/update-checker.hpp)
    if(WIN32)
        list(APPEND M600_PORTABLE_SOURCES src/voice-command-win.cpp)
    endif()
    if(MODEL600_PRO_EDITION AND MODEL600_VOICE_WHISPER)
        list(APPEND M600_PORTABLE_SOURCES src/voice-whisper.cpp)
    endif()
    add_library(m600_portable_check OBJECT ${M600_PORTABLE_SOURCES})
    if(MODEL600_PRO_EDITION AND MODEL600_VOICE_WHISPER)
        target_link_libraries(m600_portable_check PRIVATE whisper)
    endif()
    target_include_directories(m600_portable_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(m600_portable_check PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
    if(MODEL600_PRO_EDITION)
        target_compile_definitions(m600_portable_check PRIVATE MODEL600_PRO_EDITION=1)
    endif()
    m600_strict_warnings(m600_portable_check)
endif()

# --- Optional GUI sample renderer (renders every console screen to PNG) ------
if(MODEL600_BUILD_GUI_SAMPLES)
    add_executable(m600_render_gui_samples
        tools/gui_samples/render_gui_samples.cpp
        src/custom-ui-dialog.cpp
        src/custom-ui-dialog.hpp
        src/hw-panel.cpp
        src/hw-panel.hpp
        src/ui-widgets.cpp
        src/ui-widgets.hpp)
    target_include_directories(m600_render_gui_samples PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # The renderer documents every screen, UNIT 05 included.
    target_compile_definitions(m600_render_gui_samples PRIVATE MODEL600_PRO_EDITION=1)
    qt_add_resources(m600_render_gui_samples "m600_ui_samples" PREFIX "/m600" BASE "resources"
                     FILES ${MODEL600_UI_RESOURCES})
    find_package(Threads REQUIRED)
    target_link_libraries(m600_render_gui_samples PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Threads::Threads)
    if(NOT WIN32 AND NOT APPLE)
        target_link_libraries(m600_render_gui_samples PRIVATE rt)
    endif()
endif()
