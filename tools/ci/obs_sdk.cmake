# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  tools/ci/obs_sdk.cmake  -  Build the OBS plugin SDK (libobs + obs-frontend-api)
#  on Windows / macOS the same way obsproject/obs-plugintemplate does:
#
#    1. download the obs-studio source tag, obs-deps and the obs-deps Qt6
#       (versions and SHA-256 hashes from obs-plugintemplate's buildspec.json),
#    2. configure obs-studio with plugins and the frontend disabled,
#    3. build only obs-frontend-api (which pulls in libobs) and install the
#       "Development" component.
#
#    cmake -DOUT=<dir> -P tools/ci/obs_sdk.cmake
#
#  Result: <OUT>/sdk (install prefix), <OUT>/deps, <OUT>/qt6. Pass all three in
#  CMAKE_PREFIX_PATH when configuring MODEL 600.
# =============================================================================
cmake_minimum_required(VERSION 3.24)

if(NOT OUT)
    message(FATAL_ERROR "pass -DOUT=<directory>")
endif()

# Pinned to obs-plugintemplate (master, buildspec.json): a combination the OBS
# project builds plugins against.
set(OBS_VERSION "31.1.1")
set(DEPS_VERSION "2025-07-11")

if(CMAKE_HOST_WIN32)
    set(platform windows-x64)
    set(obs_file "${OBS_VERSION}.zip")
    set(obs_hash "2c8427c10b55ac6d68008df2e9a3e82f4647aaad18f105e30d4713c2de678ccf")
    set(deps_file "windows-deps-${DEPS_VERSION}-x64.zip")
    set(deps_hash "c8c642c1070dc31ce9a0f1e4cef5bb992f4bff4882255788b5da12129e85caa7")
    set(qt_file "windows-deps-qt6-${DEPS_VERSION}-x64.zip")
    set(qt_hash "0e76bf0555dd5382838850b748d3dcfab44a1e1058441309ab54e1a65b156d0a")
elseif(CMAKE_HOST_APPLE)
    set(platform macos)
    set(obs_file "${OBS_VERSION}.tar.gz")
    set(obs_hash "39751f067bacc13d44b116c5138491b5f1391f91516d3d590d874edd21292291")
    set(deps_file "macos-deps-${DEPS_VERSION}-universal.tar.xz")
    set(deps_hash "495687e63383d1a287684b6e2e9bfe246bb8f156fe265926afb1a325af1edd2a")
    set(qt_file "macos-deps-qt6-${DEPS_VERSION}-universal.tar.xz")
    set(qt_hash "d3f5f04b6ea486e032530bdf0187cbda9a54e0a49621a4c8ba984c5023998867")
else()
    message(FATAL_ERROR "Linux uses the distribution's libobs-dev package")
endif()

set(dl "${OUT}/downloads")
file(MAKE_DIRECTORY "${dl}")

function(fetch url file hash dest)
    if(NOT EXISTS "${dl}/${file}")
        message(STATUS "Downloading ${url}")
        file(DOWNLOAD "${url}" "${dl}/${file}" EXPECTED_HASH SHA256=${hash} STATUS st SHOW_PROGRESS)
        list(GET st 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${dl}/${file}")
            message(FATAL_ERROR "Download failed: ${url} (${st})")
        endif()
    endif()
    file(MAKE_DIRECTORY "${dest}")
    file(ARCHIVE_EXTRACT INPUT "${dl}/${file}" DESTINATION "${dest}")
endfunction()

fetch("https://github.com/obsproject/obs-studio/archive/refs/tags/${obs_file}" "${obs_file}" "${obs_hash}" "${OUT}/src")
fetch("https://github.com/obsproject/obs-deps/releases/download/${DEPS_VERSION}/${deps_file}" "${deps_file}" "${deps_hash}" "${OUT}/deps")
fetch("https://github.com/obsproject/obs-deps/releases/download/${DEPS_VERSION}/${qt_file}" "${qt_file}" "${qt_hash}" "${OUT}/qt6")

set(obs_src "${OUT}/src/obs-studio-${OBS_VERSION}")
set(obs_build "${OUT}/build")
set(args
    -S "${obs_src}" -B "${obs_build}"
    -DOBS_CMAKE_VERSION=3.0.0
    -DENABLE_PLUGINS=OFF
    -DENABLE_FRONTEND=OFF
    -DENABLE_SCRIPTING=OFF
    -DOBS_VERSION_OVERRIDE=${OBS_VERSION}
    # OBS turns warnings into errors unless this is already defined; a newer
    # Xcode (e.g. runway's default Xcode 26) must not fail the SDK on new warnings.
    -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
    -C "${OUT}/init.cmake")
# List-valued cache entries go through an initial-cache script: passed on the
# command line, execute_process would split them at the ';'.
set(init "set(CMAKE_PREFIX_PATH \"${OUT}/deps;${OUT}/qt6\" CACHE PATH \"\")\n")
if(CMAKE_HOST_WIN32)
    list(APPEND args -G "Visual Studio 17 2022" -A x64)
else()
    string(APPEND init "set(CMAKE_OSX_ARCHITECTURES \"arm64;x86_64\" CACHE STRING \"\")\n")
    list(APPEND args -G Xcode -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0)
endif()
file(WRITE "${OUT}/init.cmake" "${init}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Command failed (${rc}): ${ARGN}")
    endif()
endfunction()

run("${CMAKE_COMMAND}" ${args})
run("${CMAKE_COMMAND}" --build "${obs_build}" --target obs-frontend-api --config Release --parallel)
run("${CMAKE_COMMAND}" --install "${obs_build}" --component Development --config Release --prefix "${OUT}/sdk")
message(STATUS "OBS ${OBS_VERSION} SDK ready in ${OUT}/sdk")
