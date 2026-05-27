cmake_minimum_required(VERSION 3.5...4.0)
project(brainio)

set(CMAKE_CXX_STANDARD 23)

# ── Find packages ──────────────────────────────────────────────────────
find_package(pocketfft REQUIRED)
find_package(dr_libs REQUIRED)
find_package(miniaudio REQUIRED)
find_package(ixwebsocket REQUIRED)
find_package(avcpp REQUIRED)
find_package(SDL3 REQUIRED)
find_package(GTest REQUIRED)

# ═══════════════════════════════════════════════════════════════════════
# brainio-core  (domain + usecase + aquila — zero external deps)
# ═══════════════════════════════════════════════════════════════════════
file(GLOB_RECURSE AQUILA_SOURCES "src/aquila/*.cpp")

add_library(brainio-core STATIC
    src/domain/Brain.cpp
    src/domain/PlayHead.cpp
    src/domain/Sound.cpp
    src/usecase/AudioPipeline.cpp
    src/usecase/SoundProcessor.cpp
    src/usecase/StreamProcessor.cpp
    src/usecase/EffectHelpers.cpp
    src/usecase/stages/AnalysisStage.cpp
    src/usecase/stages/SearchStage.cpp
    src/usecase/stages/SynthesisStage.cpp
    src/usecase/stages/OutputStage.cpp
    ${AQUILA_SOURCES}
)

target_include_directories(brainio-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ═══════════════════════════════════════════════════════════════════════
# brainio-fft  (PocketFFT adapter)
# ═══════════════════════════════════════════════════════════════════════
add_library(brainio-fft STATIC src/adapter/fft/PocketfftBackend.cpp)
target_link_libraries(brainio-fft PUBLIC brainio-core pocketfft::pocketfft)
target_include_directories(brainio-fft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ═══════════════════════════════════════════════════════════════════════
# brainio-io  (File I/O — dr_libs)
# ═══════════════════════════════════════════════════════════════════════
add_library(brainio-io STATIC
    src/adapter/gateway/DrLibsGateway.cpp
    src/adapter/gateway/DrLibsRecorder.cpp)
target_link_libraries(brainio-io PUBLIC brainio-core dr_libs::dr_libs)
target_include_directories(brainio-io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ═══════════════════════════════════════════════════════════════════════
# brainio-playback  (Audio output — miniaudio)
# ═══════════════════════════════════════════════════════════════════════
add_library(brainio-playback STATIC src/adapter/playback/MiniaudioOutput.cpp)
target_include_directories(brainio-playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(brainio-playback PUBLIC
    brainio-core miniaudio::miniaudio)

# ═══════════════════════════════════════════════════════════════════════
# brainio-ui  (WebSocket control — ixwebsocket)
# ═══════════════════════════════════════════════════════════════════════
add_library(brainio-ui STATIC src/adapter/control/WebSocketParamController.cpp)
target_include_directories(brainio-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(brainio-ui PUBLIC brainio-core ixwebsocket::ixwebsocket)

# ═══════════════════════════════════════════════════════════════════════
# brainio-video  (Video I/O — avcpp + FFmpeg)
# ═══════════════════════════════════════════════════════════════════════
add_library(brainio-video STATIC
    src/adapter/video/FfmpegVideoSource.cpp
    src/adapter/video/FfmpegVideoOutput.cpp)
target_include_directories(brainio-video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(brainio-video PUBLIC brainio-core avcpp::avcpp)

# ═══════════════════════════════════════════════════════════════════════
# brainio-display  (Real-time SDL3 video display)
# ═══════════════════════════════════════════════════════════════════════
add_library(brainio-display STATIC
    src/adapter/display/SdlVideoDisplay.cpp
    src/adapter/display/VideoDisplayOutput.cpp)
target_include_directories(brainio-display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(brainio-display PUBLIC
    brainio-core
    SDL3::SDL3)

# ═══════════════════════════════════════════════════════════════════════
# brainio  (CLI executable)
# ═══════════════════════════════════════════════════════════════════════
add_executable(brainio
    src/main.cpp
    src/adapter/analysis/MfccAnalyser.cpp
    src/adapter/search/ClosestSearch.cpp
    src/adapter/search/SynapticSearch.cpp
    src/adapter/search/MarkovChainSearch.cpp
    src/adapter/search/VpTreeSearch.cpp
    src/adapter/search/MomentumSearch.cpp
    src/adapter/effects/FftwSpectralMorph.cpp
)

target_compile_definitions(brainio PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(brainio PRIVATE
    brainio-core
    brainio-fft
    brainio-io
    brainio-playback
    brainio-ui
    brainio-video
    brainio-display
)

# Platform-specific libs.
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(brainio PRIVATE Threads::Threads dl m)
endif()

# Suppress "ignoring duplicate libraries" warning on Apple platforms.
if(APPLE)
    target_link_options(brainio PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

# ═══════════════════════════════════════════════════════════════════════
# brainio-tests  (Google Test unit tests)
# ═══════════════════════════════════════════════════════════════════════
enable_testing()

file(GLOB_RECURSE TEST_SOURCES "tests/**/*.cpp")
add_executable(brainio-tests ${TEST_SOURCES})
target_include_directories(brainio-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(brainio-tests PRIVATE
    brainio-core
    brainio-fft
    GTest::gtest_main
)

# ClosestSearch is part of the adapter layer — compile it directly into the test binary.
target_sources(brainio-tests PRIVATE
    src/adapter/search/ClosestSearch.cpp
)
include(GoogleTest)
gtest_discover_tests(brainio-tests)

# ═══════════════════════════════════════════════════════════════════════
# format  (auto-format all project sources with clang-format)
# Usage: cmake --build --preset conan-debug --target format
# ═══════════════════════════════════════════════════════════════════════
find_program(CLANG_FORMAT_EXE NAMES clang-format)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_SOURCES
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_SOURCE_DIR}/tests/*.h"
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_SOURCES}
        COMMENT "clang-format: formatting ${CMAKE_SOURCE_DIR}/src and tests/"
        VERBATIM
    )
else()
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found — install it and re-run cmake."
        COMMENT "clang-format not available"
    )
endif()
