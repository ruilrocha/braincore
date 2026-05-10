cmake_minimum_required(VERSION 3.5...4.0)
project(brainio)

set(CMAKE_CXX_STANDARD 23)

# ── Build options ───────────────────────────────────────────────────────
set(BRAINIO_FFT_BACKEND "pocketfft" CACHE STRING "FFT backend: pocketfft or fftw")
set_property(CACHE BRAINIO_FFT_BACKEND PROPERTY STRINGS "pocketfft" "fftw")

set(BRAINIO_IO_BACKEND "drlibs" CACHE STRING "Audio I/O backend: drlibs, libsndfile, or none")
set_property(CACHE BRAINIO_IO_BACKEND PROPERTY STRINGS "drlibs" "libsndfile" "none")

option(BRAINIO_BUILD_PLAYBACK "Build miniaudio playback adapter" ON)
option(BRAINIO_BUILD_UI       "Build WebSocket UI layer"         OFF)
option(BRAINIO_BUILD_CLI      "Build CLI executable"             ON)

# ── Find packages based on options ─────────────────────────────────────
if(BRAINIO_FFT_BACKEND STREQUAL "pocketfft")
    find_package(pocketfft REQUIRED)
elseif(BRAINIO_FFT_BACKEND STREQUAL "fftw")
    find_package(FFTW3 REQUIRED)
endif()

if(BRAINIO_IO_BACKEND STREQUAL "drlibs")
    find_package(dr_libs REQUIRED)
elseif(BRAINIO_IO_BACKEND STREQUAL "libsndfile")
    find_package(SndFile REQUIRED)
endif()

if(BRAINIO_BUILD_PLAYBACK)
    find_package(miniaudio REQUIRED)
    find_package(readerwriterqueue REQUIRED)
endif()

if(BRAINIO_BUILD_UI)
    find_package(ixwebsocket REQUIRED)
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
# brainio-fft  (FFT adapter — always needed for analysis)
# ═══════════════════════════════════════════════════════════════════════
if(BRAINIO_FFT_BACKEND STREQUAL "pocketfft")
    add_library(brainio-fft STATIC src/adapter/fft/PocketfftBackend.cpp)
    target_link_libraries(brainio-fft PUBLIC brainio-core pocketfft::pocketfft)
    target_compile_definitions(brainio-fft PUBLIC BRAINIO_FFT_POCKETFFT)
elseif(BRAINIO_FFT_BACKEND STREQUAL "fftw")
    add_library(brainio-fft STATIC src/adapter/fft/FftwBackend.cpp)
    target_link_libraries(brainio-fft PUBLIC brainio-core fftw::fftw)
    target_compile_definitions(brainio-fft PUBLIC BRAINIO_FFT_FFTW)
endif()

target_include_directories(brainio-fft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ═══════════════════════════════════════════════════════════════════════
# brainio-io  (File I/O adapter — optional)
# ═══════════════════════════════════════════════════════════════════════
if(BRAINIO_IO_BACKEND STREQUAL "drlibs")
    add_library(brainio-io STATIC
        src/adapter/gateway/DrLibsGateway.cpp
        src/adapter/gateway/DrLibsRecorder.cpp)
    target_link_libraries(brainio-io PUBLIC brainio-core dr_libs::dr_libs)
    target_compile_definitions(brainio-io PUBLIC BRAINIO_IO_DRLIBS)
elseif(BRAINIO_IO_BACKEND STREQUAL "libsndfile")
    add_library(brainio-io STATIC
        src/adapter/gateway/LibSndFileGateway.cpp
        src/adapter/gateway/LibSndFileRecorder.cpp)
    target_link_libraries(brainio-io PUBLIC brainio-core SndFile::sndfile)
    target_compile_definitions(brainio-io PUBLIC BRAINIO_IO_LIBSNDFILE)
endif()

if(NOT BRAINIO_IO_BACKEND STREQUAL "none")
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
# brainio-ui  (WebSocket control — optional, OFF by default)
# ═══════════════════════════════════════════════════════════════════════
if(BRAINIO_BUILD_UI)
    add_library(brainio-ui STATIC src/adapter/control/WebSocketParamController.cpp)
    target_include_directories(brainio-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(brainio-ui PUBLIC brainio-core ixwebsocket::ixwebsocket)
    target_compile_definitions(brainio-ui PUBLIC BRAINIO_HAS_UI)
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
        # Search strategies (needed by CLI)
        src/adapter/analysis/MfccAnalyser.cpp
        src/adapter/search/ClosestSearch.cpp
        src/adapter/search/ReverseSearch.cpp
        src/adapter/search/SynapticSearch.cpp
        src/adapter/search/RandomSearch.cpp
        src/adapter/search/WeightedRandomSearch.cpp
        src/adapter/search/MarkovChainSearch.cpp
        src/adapter/search/MomentumSearch.cpp
        # Effects (uses IFft)
        src/adapter/effects/FftwSpectralMorph.cpp
    )

    target_compile_definitions(brainio PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

    target_link_libraries(brainio PRIVATE brainio-core brainio-fft)

    if(NOT BRAINIO_IO_BACKEND STREQUAL "none")
        target_link_libraries(brainio PRIVATE brainio-io)
    endif()

    if(BRAINIO_BUILD_PLAYBACK)
        target_link_libraries(brainio PRIVATE brainio-playback)
    endif()

    if(BRAINIO_BUILD_UI)
        target_link_libraries(brainio PRIVATE brainio-ui)
    endif()

    # Platform-specific libs.
    if(UNIX AND NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(brainio PRIVATE Threads::Threads dl m)
    endif()
endif()

