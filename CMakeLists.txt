cmake_minimum_required(VERSION 3.5...4.0)
project(brainio)

set(CMAKE_CXX_STANDARD 23)

find_package(SndFile REQUIRED)
find_package(FFTW3 REQUIRED)
find_package(miniaudio REQUIRED)

file(GLOB_RECURSE AQUILA "src/aquila/*.cpp" "src/aquila/*.h")

add_executable(brainio
        src/main.cpp

        # Domain (innermost layer)
        src/domain/Brain.cpp
        src/domain/Brain.h
        src/domain/Block.h
        src/domain/Sound.cpp
        src/domain/Sound.h
        src/domain/SearchParams.h
        src/domain/BlockConfig.h
        src/domain/WindowFunction.h
        src/domain/Fingerprints.h
        src/domain/Random.h
        src/domain/constants.h
        src/domain/port/IAnalyser.h
        src/domain/port/ISoundFileGateway.h
        src/domain/port/ISearchStrategy.h
        src/domain/port/IBlockEffect.h
        src/domain/port/IAudioOutput.h

        # Use-case layer
        src/usecase/SoundProcessor.cpp
        src/usecase/SoundProcessor.h
        src/usecase/StreamProcessor.cpp
        src/usecase/StreamProcessor.h
        src/usecase/EffectHelpers.cpp
        src/usecase/EffectHelpers.h

        # Adapters (outermost layer)
        src/adapter/analysis/MfccAnalyser.cpp
        src/adapter/analysis/MfccAnalyser.h
        src/adapter/gateway/LibSndFileGateway.cpp
        src/adapter/gateway/LibSndFileGateway.h
        src/adapter/search/ClosestSearch.cpp
        src/adapter/search/ClosestSearch.h
        src/adapter/search/ReverseSearch.cpp
        src/adapter/search/ReverseSearch.h
        src/adapter/search/SynapticSearch.cpp
        src/adapter/search/SynapticSearch.h
        src/adapter/search/RandomSearch.cpp
        src/adapter/search/RandomSearch.h
        src/adapter/search/WeightedRandomSearch.cpp
        src/adapter/search/WeightedRandomSearch.h
        src/adapter/search/MarkovChainSearch.cpp
        src/adapter/search/MarkovChainSearch.h
        src/adapter/search/MomentumSearch.cpp
        src/adapter/search/MomentumSearch.h

        # Effects adapters
        src/adapter/effects/FftwSpectralMorph.cpp
        src/adapter/effects/FftwSpectralMorph.h

        # Playback adapters
        src/adapter/playback/MiniaudioOutput.cpp
        src/adapter/playback/MiniaudioOutput.h

        # Shared adapter utilities
        src/adapter/search/SearchUtils.h

        ${AQUILA}
)

# Inject the project root directory so main.cpp can resolve relative paths.
target_compile_definitions(brainio PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(brainio SndFile::sndfile fftw::fftw miniaudio::miniaudio)

# Platform audio frameworks are linked transitively by the miniaudio Conan package.
# On Linux, pthreads and dl may still be needed:
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(brainio Threads::Threads dl m)
endif()

