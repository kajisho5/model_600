# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  cmake/whisper.cmake - embedded speech recognizer for the voice director.
#
#  * whisper.cpp v1.9.4 (MIT), pinned by full commit id, built as static
#    CPU-only libraries (no Metal / CUDA / OpenMP / BLAS, no -march=native),
#    so one binary runs on every supported Mac, Intel and Apple silicon.
#  * Model ggml-base-q5_1.bin (OpenAI Whisper "base", MIT, 5-bit quantized by
#    the whisper.cpp project), downloaded at configure time and verified by
#    SHA-256. MODEL600_WHISPER_MODEL_PATH points at a local copy instead
#    (offline builds); it is verified the same way.
#
#  Provides: target `whisper` (static), variable M600_WHISPER_MODEL (absolute
#  path). whisper.cpp's own install rules may exist in single-arch builds;
#  packaging installs only COMPONENT model600.
# =============================================================================
include(FetchContent)
enable_language(C)              # ggml is C + C++.

set(M600_WHISPER_GIT    "https://github.com/ggml-org/whisper.cpp.git")
set(M600_WHISPER_COMMIT "927cfce34f31707e17f2bff35c349632fb9e2c3a")   # tag v1.9.4
set(M600_WHISPER_MODEL_NAME   "ggml-base-q5_1.bin")
set(M600_WHISPER_MODEL_URL    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base-q5_1.bin")
set(M600_WHISPER_MODEL_SHA256 "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898")

# whisper.cpp / ggml build switches, shared by both build paths below.
set(m600_whisper_opts
    BUILD_SHARED_LIBS=OFF
    WHISPER_BUILD_TESTS=OFF WHISPER_BUILD_EXAMPLES=OFF WHISPER_BUILD_SERVER=OFF
    WHISPER_CURL=OFF WHISPER_SDL2=OFF WHISPER_COREML=OFF WHISPER_OPENVINO=OFF WHISPER_ALL_WARNINGS=OFF
    GGML_NATIVE=OFF GGML_OPENMP=OFF GGML_METAL=OFF GGML_BLAS=OFF GGML_CUDA=OFF GGML_VULKAN=OFF
    GGML_BACKEND_DL=OFF GGML_CCACHE=OFF GGML_ALL_WARNINGS=OFF)

set(m600_whisper_split OFF)
if(APPLE)
    list(LENGTH CMAKE_OSX_ARCHITECTURES m600_n_arch)
    if(m600_n_arch GREATER 1)
        set(m600_whisper_split ON)
    endif()
endif()

if(NOT m600_whisper_split)
    # Single architecture: build whisper.cpp in-tree. Its old
    # cmake_minimum_required would leave CMP0077 at OLD, where option()
    # ignores these normal variables (BUILD_SHARED_LIBS=OFF silently lost).
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    foreach(kv IN LISTS m600_whisper_opts)
        string(REPLACE "=" ";" kv "${kv}")
        list(GET kv 0 k)
        list(GET kv 1 v)
        set(${k} ${v})
    endforeach()
    set(m600_fc_opts "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
        list(APPEND m600_fc_opts SYSTEM)            # whisper/ggml headers as system headers.
    endif()
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        list(APPEND m600_fc_opts EXCLUDE_FROM_ALL)  # Build only what we link.
    endif()
    FetchContent_Declare(m600_whisper
        GIT_REPOSITORY ${M600_WHISPER_GIT}
        GIT_TAG        ${M600_WHISPER_COMMIT}
        ${m600_fc_opts})
    FetchContent_MakeAvailable(m600_whisper)
else()
    # Universal2: ggml only enables its NEON / AVX kernels when it builds for
    # exactly one architecture (a multi-arch configure falls back to generic
    # scalar code). Build it once per slice and merge the archives with lipo.
    #   arm64  : compiler defaults (Apple M1 baseline: NEON, dot product, fp16)
    #   x86_64 : SSE4.2 + AVX + F16C, no AVX2/FMA, so 2013 Ivy Bridge Macs that
    #            still run macOS 12 do not fault.
    include(ExternalProject)
    FetchContent_Declare(m600_whisper_src
        GIT_REPOSITORY ${M600_WHISPER_GIT}
        GIT_TAG        ${M600_WHISPER_COMMIT}
        SOURCE_SUBDIR  m600-no-cmake)            # Fetch only; built below.
    FetchContent_MakeAvailable(m600_whisper_src)
    set(m600_wsrc "${m600_whisper_src_SOURCE_DIR}")

    set(m600_libs src/libwhisper.a ggml/src/libggml.a ggml/src/libggml-cpu.a ggml/src/libggml-base.a)
    set(m600_universal_dir "${CMAKE_BINARY_DIR}/whisper-universal")
    set(m600_lipo_outputs "")
    set(m600_arch_targets "")
    foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        set(bin "${CMAKE_BINARY_DIR}/whisper-${arch}")
        set(args -DCMAKE_BUILD_TYPE=Release
                 -DCMAKE_OSX_ARCHITECTURES=${arch}
                 -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
                 -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
        foreach(kv IN LISTS m600_whisper_opts)
            list(APPEND args "-D${kv}")
        endforeach()
        if(arch STREQUAL "x86_64")
            list(APPEND args -DGGML_SSE42=ON -DGGML_AVX=ON -DGGML_F16C=ON
                             -DGGML_AVX2=OFF -DGGML_FMA=OFF -DGGML_BMI2=OFF -DGGML_AVX_VNNI=OFF -DGGML_AVX512=OFF)
        endif()
        set(byproducts "")
        foreach(lib IN LISTS m600_libs)
            list(APPEND byproducts "${bin}/${lib}")
        endforeach()
        ExternalProject_Add(m600_whisper_${arch}
            SOURCE_DIR "${m600_wsrc}"
            BINARY_DIR "${bin}"
            CMAKE_ARGS ${args}
            BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target whisper --parallel
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS ${byproducts})
        list(APPEND m600_arch_targets m600_whisper_${arch})
    endforeach()

    foreach(lib IN LISTS m600_libs)
        get_filename_component(name "${lib}" NAME)
        set(inputs "")
        foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
            list(APPEND inputs "${CMAKE_BINARY_DIR}/whisper-${arch}/${lib}")
        endforeach()
        add_custom_command(OUTPUT "${m600_universal_dir}/${name}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${m600_universal_dir}"
            COMMAND lipo -create ${inputs} -output "${m600_universal_dir}/${name}"
            DEPENDS ${m600_arch_targets} ${inputs}
            COMMENT "lipo ${name}"
            VERBATIM)
        list(APPEND m600_lipo_outputs "${m600_universal_dir}/${name}")
    endforeach()
    add_custom_target(m600_whisper_universal DEPENDS ${m600_lipo_outputs})

    # Same consumer-facing name as the in-tree build.
    add_library(whisper INTERFACE)
    target_include_directories(whisper SYSTEM INTERFACE "${m600_wsrc}/include" "${m600_wsrc}/ggml/include")
    target_link_libraries(whisper INTERFACE ${m600_lipo_outputs} "-framework Accelerate")
    add_dependencies(whisper m600_whisper_universal)
endif()

# --- Model -------------------------------------------------------------------
set(MODEL600_WHISPER_MODEL_PATH "" CACHE FILEPATH "Local ggml-base-q5_1.bin (skips the download)")
if(MODEL600_WHISPER_MODEL_PATH)
    file(SHA256 "${MODEL600_WHISPER_MODEL_PATH}" m600_model_hash)
    if(NOT m600_model_hash STREQUAL M600_WHISPER_MODEL_SHA256)
        message(FATAL_ERROR "${MODEL600_WHISPER_MODEL_PATH}: SHA-256 ${m600_model_hash} != ${M600_WHISPER_MODEL_SHA256}")
    endif()
    set(M600_WHISPER_MODEL "${MODEL600_WHISPER_MODEL_PATH}")
else()
    set(M600_WHISPER_MODEL "${CMAKE_BINARY_DIR}/models/${M600_WHISPER_MODEL_NAME}")
    # Skipped when the file is already there with the right hash.
    file(DOWNLOAD "${M600_WHISPER_MODEL_URL}" "${M600_WHISPER_MODEL}"
         EXPECTED_HASH SHA256=${M600_WHISPER_MODEL_SHA256}
         TLS_VERIFY ON
         STATUS m600_dl)
    list(GET m600_dl 0 m600_dl_code)
    if(NOT m600_dl_code EQUAL 0)
        list(GET m600_dl 1 m600_dl_msg)
        message(FATAL_ERROR "Whisper model download failed: ${m600_dl_msg}. "
                            "Set MODEL600_WHISPER_MODEL_PATH to a local ${M600_WHISPER_MODEL_NAME}.")
    endif()
endif()
message(STATUS "MODEL 600 voice model: ${M600_WHISPER_MODEL}")
