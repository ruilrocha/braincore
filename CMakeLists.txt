cmake_minimum_required(VERSION 3.5...4.0)
project(brainio)

set(CMAKE_CXX_STANDARD 23)

# ── Build options ───────────────────────────────────────────────────────
option(BRAINIO_USE_FFTW       "Enable FFTW FFT backend"        OFF)
option(BRAINIO_USE_POCKETFFT  "Enable PocketFFT FFT backend"   ON)
option(BRAINIO_USE_LIBSNDFILE "Enable libsndfile audio I/O"    OFF)
option(BRAINIO_USE_DRLIBS     "Enable dr_libs audio I/O"       ON)
option(BRAINIO_USE_MINIAUDIO  "Enable miniaudio playback"      ON)
option(BRAINIO_USE_WEBSOCKET  "Enable WebSocket param control"  ON)

# ── Find packages based on options ─────────────────────────────────────
if(BRAINIO_USE_FFTW)
    find_package(FFTW3 REQUIRED)
endif()
if(BRAINIO_USE_LIBSNDFILE)
    find_package(SndFile REQUIRED)
endif()
if(BRAINIO_USE_MINIAUDIO)
    find_package(miniaudio REQUIRED)
    find_package(readerwriterqueue REQUIRED)
endif()
if(BRAINIO_USE_WEBSOCKET)
    find_package(ixwebsocket REQUIRED)
endif()

# ── brainio-core (domain + usecase + aquila) ───────────────────────────
# Zero external dependencies — pure C++23. iOS-compilable.
file(GLOB_RECURSE AQUILA_SOURCES "src/aquila/*.cpp")

add_library(brainio-core STATIC
    # Domain
    src/domain/Brain.cpp
    src/domain/Sound.cpp

    # Use-case
    src/usecase/SoundProcessor.cpp
    src/usecase/StreamProcessor.cpp
    src/usecase/EffectHelpers.cpp

    # Aquila (Mel filter bank only)
    ${AQUILA_SOURCES}
)

target_include_directories(brainio-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── brainio-adapters (platform-specific implementations) ────────────────
set(ADAPTER_SOURCES
    # Analysis
    src/adapter/analysis/MfccAnalyser.cpp

    # Search strategies
    src/adapter/search/ClosestSearch.cpp
    src/adapter/search/ReverseSearch.cpp
    src/adapter/search/SynapticSearch.cpp
    src/adapter/search/RandomSearch.cpp
    src/adapter/search/WeightedRandomSearch.cpp
    src/adapter/search/MarkovChainSearch.cpp
    src/adapter/search/MomentumSearch.cpp
)

set(ADAPTER_LIBS brainio-core)

# FFT backends
if(BRAINIO_USE_POCKETFFT)
    list(APPEND ADAPTER_SOURCES src/adapter/fft/PocketfftBackend.cpp)
endif()
if(BRAINIO_USE_FFTW)
    list(APPEND ADAPTER_SOURCES src/adapter/fft/FftwBackend.cpp)
    list(APPEND ADAPTER_LIBS fftw::fftw)
endif()

# Audio I/O backends
if(BRAINIO_USE_DRLIBS)
    list(APPEND ADAPTER_SOURCES
        src/adapter/gateway/DrLibsGateway.cpp
        src/adapter/gateway/DrLibsRecorder.cpp)
endif()
if(BRAINIO_USE_LIBSNDFILE)
    list(APPEND ADAPTER_SOURCES
        src/adapter/gateway/LibSndFileGateway.cpp
        src/adapter/gateway/LibSndFileRecorder.cpp)
    list(APPEND ADAPTER_LIBS SndFile::sndfile)
endif()

# Effects (uses IFft)
list(APPEND ADAPTER_SOURCES src/adapter/effects/FftwSpectralMorph.cpp)

# Playback
if(BRAINIO_USE_MINIAUDIO)
    list(APPEND ADAPTER_SOURCES src/adapter/playback/MiniaudioOutput.cpp)
    list(APPEND ADAPTER_LIBS miniaudio::miniaudio readerwriterqueue::readerwriterqueue)
endif()

# WebSocket control
if(BRAINIO_USE_WEBSOCKET)
    list(APPEND ADAPTER_SOURCES src/adapter/control/WebSocketParamController.cpp)
    list(APPEND ADAPTER_LIBS ixwebsocket::ixwebsocket)
endif()

add_library(brainio-adapters STATIC ${ADAPTER_SOURCES})

target_include_directories(brainio-adapters PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Compile definitions for conditional compilation
if(BRAINIO_USE_POCKETFFT)
    target_compile_definitions(brainio-adapters PUBLIC BRAINIO_HAS_POCKETFFT)
endif()
if(BRAINIO_USE_FFTW)
    target_compile_definitions(brainio-adapters PUBLIC BRAINIO_HAS_FFTW)
endif()
if(BRAINIO_USE_DRLIBS)
    target_compile_definitions(brainio-adapters PUBLIC BRAINIO_HAS_DRLIBS)
endif()
if(BRAINIO_USE_LIBSNDFILE)
    target_compile_definitions(brainio-adapters PUBLIC BRAINIO_HAS_LIBSNDFILE)
endif()
if(BRAINIO_USE_MINIAUDIO)
    target_compile_definitions(brainio-adapters PUBLIC BRAINIO_HAS_MINIAUDIO)
endif()
if(BRAINIO_USE_WEBSOCKET)
    target_compile_definitions(brainio-adapters PUBLIC BRAINIO_HAS_WEBSOCKET)
endif()

target_link_libraries(brainio-adapters PUBLIC ${ADAPTER_LIBS})

# ── brainio-cli (executable) ───────────────────────────────────────────
add_executable(brainio src/main.cpp)

target_compile_definitions(brainio PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(brainio PRIVATE brainio-adapters)

# Platform audio frameworks (Linux needs pthreads/dl).
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(brainio PRIVATE Threads::Threads dl m)
endif()

