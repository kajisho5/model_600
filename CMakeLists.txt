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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    # Universal2 fat binary. (Honoured only by the Apple Clang toolchain.)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
    enable_language(OBJCXX)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

# --- Qt ----------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)

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
    src/ui-widgets.cpp)

set(MODEL600_HEADERS
    src/shared-state.hpp
    src/dsp-core.hpp
    src/voice-command-multilingual.hpp
    src/license-manager.hpp
    src/update-checker.hpp
    src/os-conflict-shield.hpp
    src/custom-ui-dialog.hpp
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

if(WIN32)
    list(APPEND MODEL600_SOURCES src/voice-command-win.cpp)
elseif(APPLE)
    list(APPEND MODEL600_SOURCES src/voice-command-mac.mm)
endif()

add_library(model600 MODULE ${MODEL600_SOURCES} ${MODEL600_HEADERS})

target_include_directories(model600 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
qt_add_resources(model600 "m600_ui" PREFIX "/m600" BASE "resources" FILES ${MODEL600_UI_RESOURCES})

target_link_libraries(model600 PRIVATE
    OBS::libobs
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Multimedia)

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
    target_link_libraries(model600 PRIVATE
        "-framework Foundation" "-framework Speech" "-framework AVFoundation")
    set_target_properties(model600 PROPERTIES PREFIX "" SUFFIX ".so")
elseif(WIN32)
    set_target_properties(model600 PROPERTIES PREFIX "" SUFFIX ".dll")
else()
    set_target_properties(model600 PROPERTIES PREFIX "")
endif()

# --- Install (standard OBS plugin layout) ------------------------------------
if(WIN32)
    install(TARGETS model600 RUNTIME DESTINATION obs-plugins/64bit)
elseif(APPLE)
    install(TARGETS model600 LIBRARY DESTINATION "model600.plugin/Contents/MacOS")
else()
    install(TARGETS model600 LIBRARY DESTINATION lib/obs-plugins)
endif()

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
endif()

# --- Optional GUI sample renderer (renders every console screen to PNG) ------
if(MODEL600_BUILD_GUI_SAMPLES)
    add_executable(m600_render_gui_samples
        tools/gui_samples/render_gui_samples.cpp
        src/custom-ui-dialog.cpp
        src/custom-ui-dialog.hpp
        src/ui-widgets.cpp
        src/ui-widgets.hpp)
    target_include_directories(m600_render_gui_samples PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    qt_add_resources(m600_render_gui_samples "m600_ui_samples" PREFIX "/m600" BASE "resources"
                     FILES ${MODEL600_UI_RESOURCES})
    find_package(Threads REQUIRED)
    target_link_libraries(m600_render_gui_samples PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Threads::Threads)
    if(NOT WIN32 AND NOT APPLE)
        target_link_libraries(m600_render_gui_samples PRIVATE rt)
    endif()
endif()
