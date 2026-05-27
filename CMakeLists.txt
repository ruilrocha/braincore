cmake_minimum_required(VERSION 3.5...4.0)
project(brainio)

set(CMAKE_CXX_STANDARD 23)

# ── Find packages ──────────────────────────────────────────────────────
find_package(pocketfft REQUIRED)
find_package(GTest REQUIRED)

# ═══════════════════════════════════════════════════════════════════════
# brainio-core  (lean static library — domain + adapters, no app layer)
#
#   Domain:   Brain, PlayHead, Sound, WindowFunction, AudioPrint, ...
#   Aquila:   MelFilterBank (in-tree DSP)
#   Adapters: MfccAnalyser, PocketfftBackend, all search strategies,
#             SpectralMorph (IFft-injected, iOS-safe), EffectHelpers
# ═══════════════════════════════════════════════════════════════════════
file(GLOB_RECURSE AQUILA_SOURCES "src/aquila/*.cpp")

add_library(brainio-core STATIC
    src/BrainSession.cpp
    src/domain/Brain.cpp
    src/domain/PlayHead.cpp
    src/domain/Sound.cpp
    src/adapter/analysis/MfccAnalyser.cpp
    src/adapter/fft/PocketfftBackend.cpp
    src/adapter/search/ClosestSearch.cpp
    src/adapter/search/SynapticSearch.cpp
    src/adapter/search/VpTreeSearch.cpp
    src/adapter/search/MarkovChainSearch.cpp
    src/adapter/search/MomentumSearch.cpp
    src/adapter/effects/SpectralMorph.cpp
    src/adapter/effects/EffectHelpers.cpp
    ${AQUILA_SOURCES}
)

target_include_directories(brainio-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(brainio-core PUBLIC pocketfft::pocketfft)

# Platform-specific libs.
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(brainio-core PUBLIC Threads::Threads dl m)
endif()

if(APPLE)
    target_link_options(brainio-core PRIVATE "-Wl,-no_warn_duplicate_libraries")
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
    GTest::gtest_main
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
