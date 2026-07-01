cmake_minimum_required(VERSION 3.5...4.0)
project(braincore)

set(CMAKE_CXX_STANDARD 23)

# ── Find packages ──────────────────────────────────────────────────────
find_package(pocketfft REQUIRED)
find_package(GTest REQUIRED)

# ═══════════════════════════════════════════════════════════════════════
# braincore-lib  (lean static library — domain + adapters, no app layer)
#
#   Domain:   Brain, PlayHead, Sound, WindowFunction, AudioPrint, ...
#   Aquila:   MelFilterBank (in-tree DSP)
#   Adapters: MfccAnalyser, PocketfftBackend, all search strategies,
#             SpectralMorph (IFft-injected, iOS-safe), EffectHelpers
# ═══════════════════════════════════════════════════════════════════════
file(GLOB_RECURSE AQUILA_SOURCES "src/aquila/*.cpp")

add_library(braincore-lib STATIC
    src/BrainSession.cpp
    src/engine/BrainEngine.cpp
    src/engine/InfiniteDriftState.cpp
    src/engine/BlockEffectChain.cpp
    src/engine/OutputPipeline.cpp
    src/domain/Brain.cpp
    src/domain/NearestNeighbourIndex.cpp
    src/domain/PlayHead.cpp
    src/domain/Sound.cpp
    src/adapter/analysis/MfccAnalyser.cpp
    src/adapter/fft/PocketfftBackend.cpp
    src/adapter/search/ClosestSearch.cpp
    src/adapter/search/SynapticSearch.cpp
    src/adapter/search/VpTreeSearch.cpp
    src/adapter/effects/SpectralMorph.cpp
    src/adapter/effects/EffectHelpers.cpp
    src/adapter/effects/OlaBuffer.cpp
    ${AQUILA_SOURCES}
)

target_include_directories(braincore-lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(braincore-lib PUBLIC pocketfft::pocketfft)

# ═══════════════════════════════════════════════════════════════════════
# DSP release-build flags
# ═══════════════════════════════════════════════════════════════════════
# -ffp-contract=fast enables FMA (fused multiply-add) without reordering
# NaN/Inf handling or breaking IEEE semantics the way -ffast-math does.
# Applied only in Release to keep debug builds bit-reproducible.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(braincore-lib PRIVATE
        $<$<CONFIG:Release>:-ffp-contract=fast>
    )
endif()

# Optional LTO — off by default to keep iOS/cross-build link times short.
option(BRAINCORE_ENABLE_LTO "Enable interprocedural (link-time) optimisation for braincore-lib" OFF)
if(BRAINCORE_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
        message(STATUS "LTO enabled for braincore-lib")
        set_property(TARGET braincore-lib PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO requested but not supported: ${_ipo_err}")
    endif()
endif()

# Platform-specific libs.
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(braincore-lib PUBLIC Threads::Threads dl m)
endif()

if(APPLE)
    target_link_options(braincore-lib PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

# ═══════════════════════════════════════════════════════════════════════
# braincore-tests  (Google Test unit tests)
# ═══════════════════════════════════════════════════════════════════════
enable_testing()

file(GLOB_RECURSE TEST_SOURCES "tests/**/*.cpp")
add_executable(braincore-tests ${TEST_SOURCES})
target_include_directories(braincore-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(braincore-tests PRIVATE
    braincore-lib
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(braincore-tests)

# ═══════════════════════════════════════════════════════════════════════
# braincore-bench  (Google Benchmark micro-benchmarks)
# Opt-in: cmake … -DENABLE_BENCHMARKS=ON  [default: OFF]
# Usage:  cmake --build --preset conan-debug --target braincore-bench
#         ./cmake-build-debug/conan/build/Debug/braincore-bench
# ═══════════════════════════════════════════════════════════════════════
option(ENABLE_BENCHMARKS "Build Google Benchmark micro-benchmarks" OFF)

if(ENABLE_BENCHMARKS)
    find_package(benchmark REQUIRED)
    message(STATUS "Benchmarks enabled — building braincore-bench")

    file(GLOB_RECURSE BENCH_SOURCES "bench/*.cpp")
    add_executable(braincore-bench ${BENCH_SOURCES})
    target_include_directories(braincore-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(braincore-bench PRIVATE
        braincore-lib
        benchmark::benchmark_main
    )
endif()

# ═══════════════════════════════════════════════════════════════════════
# AddressSanitizer  (opt-in: cmake … -DENABLE_ASAN=ON)
# ═══════════════════════════════════════════════════════════════════════
option(ENABLE_ASAN "Build with AddressSanitizer (-fsanitize=address)" OFF)

if(ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled")
    set(_asan_flags -fsanitize=address -fno-omit-frame-pointer)
    target_compile_options(braincore-lib  PUBLIC  ${_asan_flags})
    target_link_options   (braincore-lib  PUBLIC  -fsanitize=address)
    target_compile_options(braincore-tests PRIVATE ${_asan_flags})
    target_link_options   (braincore-tests PRIVATE -fsanitize=address)
endif()

option(ENABLE_MSAN "Build with MemorySanitizer (-fsanitize=memory)" OFF)

if(ENABLE_MSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "MemorySanitizer requires Clang (current compiler: ${CMAKE_CXX_COMPILER_ID}). "
            "Set CC=clang CXX=clang++ before running conan profile detect and cmake.")
    endif()
    message(STATUS "MemorySanitizer enabled")
    set(_msan_flags -fsanitize=memory -fno-omit-frame-pointer -fsanitize-memory-track-origins)
    target_compile_options(braincore-lib  PUBLIC  ${_msan_flags})
    target_link_options   (braincore-lib  PUBLIC  -fsanitize=memory)
    target_compile_options(braincore-tests PRIVATE ${_msan_flags})
    target_link_options   (braincore-tests PRIVATE -fsanitize=memory)
endif()



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
