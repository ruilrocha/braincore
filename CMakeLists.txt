cmake_minimum_required(VERSION 3.5...4.0)
project(brainio)

set(CMAKE_CXX_STANDARD 23)

# ── Build options ───────────────────────────────────────────────────────
option(BRAINIO_BUILD_IO       "Build audio file I/O adapter (dr_libs)" ON)
option(BRAINIO_BUILD_PLAYBACK "Build miniaudio playback adapter"       ON)
option(BRAINIO_BUILD_UI       "Build WebSocket UI layer"               ON)
option(BRAINIO_BUILD_VIDEO    "Build video I/O adapter (FFmpeg)"       ON)
option(BRAINIO_BUILD_CLI      "Build CLI executable"                   ON)

# ── Find packages ──────────────────────────────────────────────────────
find_package(pocketfft REQUIRED)

if(BRAINIO_BUILD_IO)
    find_package(dr_libs REQUIRED)
endif()

if(BRAINIO_BUILD_PLAYBACK)
    find_package(miniaudio REQUIRED)
    find_package(readerwriterqueue REQUIRED)
endif()

if(BRAINIO_BUILD_UI)
    find_package(ixwebsocket REQUIRED)
endif()

if(BRAINIO_BUILD_VIDEO)
    find_package(avcpp REQUIRED)
endif()

# ═══════════════════════════════════════════════════════════════════════
# brainio-core  (domain + usecase + aquila — zero external deps)
# ═══════════════════════════════════════════════════════════════════════
file(GLOB_RECURSE AQUILA_SOURCES "src/aquila/*.cpp")

add_library(brainio-core STATIC
    src/domain/Brain.cpp
    src/domain/Sound.cpp
    src/usecase/SoundProcessor.cpp
    src/usecase/StreamProcessor.cpp
    src/usecase/EffectHelpers.cpp
    ${AQUILA_SOURCES}
)

target_include_directories(brainio-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ═══════════════════════════════════════════════════════════════════════
# brainio-fft  (PocketFFT adapter — always needed for analysis)
# ═══════════════════════════════════════════════════════════════════════
add_library(brainio-fft STATIC src/adapter/fft/PocketfftBackend.cpp)
target_link_libraries(brainio-fft PUBLIC brainio-core pocketfft::pocketfft)
target_include_directories(brainio-fft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ═══════════════════════════════════════════════════════════════════════
# brainio-io  (File I/O adapter — optional, uses dr_libs)
# ═══════════════════════════════════════════════════════════════════════
if(BRAINIO_BUILD_IO)
    add_library(brainio-io STATIC
        src/adapter/gateway/DrLibsGateway.cpp
        src/adapter/gateway/DrLibsRecorder.cpp)
    target_link_libraries(brainio-io PUBLIC brainio-core dr_libs::dr_libs)
    target_include_directories(brainio-io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# ═══════════════════════════════════════════════════════════════════════
# brainio-playback  (Audio output — optional)
# ═══════════════════════════════════════════════════════════════════════
if(BRAINIO_BUILD_PLAYBACK)
    add_library(brainio-playback STATIC src/adapter/playback/MiniaudioOutput.cpp)
    target_include_directories(brainio-playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(brainio-playback PUBLIC
        brainio-core miniaudio::miniaudio readerwriterqueue::readerwriterqueue)
    target_compile_definitions(brainio-playback PUBLIC BRAINIO_HAS_PLAYBACK)
endif()

# ═══════════════════════════════════════════════════════════════════════
# brainio-ui  (WebSocket control — optional)
# ═══════════════════════════════════════════════════════════════════════
if(BRAINIO_BUILD_UI)
    add_library(brainio-ui STATIC src/adapter/control/WebSocketParamController.cpp)
    target_include_directories(brainio-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(brainio-ui PUBLIC brainio-core ixwebsocket::ixwebsocket)
    target_compile_definitions(brainio-ui PUBLIC BRAINIO_HAS_UI)
endif()

# ═══════════════════════════════════════════════════════════════════════
# brainio-video  (Video I/O — optional, uses avcpp + FFmpeg)
# ═══════════════════════════════════════════════════════════════════════
if(BRAINIO_BUILD_VIDEO)
    add_library(brainio-video STATIC
        src/adapter/video/FfmpegVideoSource.cpp
        src/adapter/video/FfmpegVideoOutput.cpp)
    target_include_directories(brainio-video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(brainio-video PUBLIC
        brainio-core
        avcpp::avcpp)
    target_compile_definitions(brainio-video PUBLIC BRAINIO_HAS_VIDEO)
endif()

# ═══════════════════════════════════════════════════════════════════════
# brainio-capi  (C-compatible API for XCFramework / Swift consumption)
# ═══════════════════════════════════════════════════════════════════════
add_library(brainio-capi STATIC
    include/brainio.cpp
    src/adapter/analysis/MfccAnalyser.cpp
    src/adapter/search/ClosestSearch.cpp
)
target_include_directories(brainio-capi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(brainio-capi PUBLIC brainio-core brainio-fft)

# ═══════════════════════════════════════════════════════════════════════
# brainio  (CLI executable — optional)
# ═══════════════════════════════════════════════════════════════════════
if(BRAINIO_BUILD_CLI)
    add_executable(brainio
        src/main.cpp
        src/adapter/analysis/MfccAnalyser.cpp
        src/adapter/search/ClosestSearch.cpp
        src/adapter/search/SynapticSearch.cpp
        src/adapter/search/MarkovChainSearch.cpp
        src/adapter/search/MomentumSearch.cpp
        src/adapter/effects/FftwSpectralMorph.cpp
    )

    target_compile_definitions(brainio PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

    target_link_libraries(brainio PRIVATE brainio-core brainio-fft)

    if(BRAINIO_BUILD_IO)
        target_link_libraries(brainio PRIVATE brainio-io)
    endif()

    if(BRAINIO_BUILD_PLAYBACK)
        target_link_libraries(brainio PRIVATE brainio-playback)
    endif()

    if(BRAINIO_BUILD_UI)
        target_link_libraries(brainio PRIVATE brainio-ui)
    endif()

    if(BRAINIO_BUILD_VIDEO)
        target_link_libraries(brainio PRIVATE brainio-video)
        target_compile_definitions(brainio PRIVATE BRAINIO_HAS_VIDEO)
    endif()

    # Platform-specific libs.
    if(UNIX AND NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(brainio PRIVATE Threads::Threads dl m)
    endif()

    # Suppress "ignoring duplicate libraries" warning introduced by avcpp pulling
    # libc++ through multiple transitive paths on Apple platforms.
    if(APPLE)
        target_link_options(brainio PRIVATE "-Wl,-no_warn_duplicate_libraries")
    endif()
endif()

