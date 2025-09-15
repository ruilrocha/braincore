cmake_minimum_required (VERSION 3.5...4.0)
project(brainio)

set(CMAKE_CXX_STANDARD 23)

find_package(SndFile REQUIRED)
find_package(FFTW3 REQUIRED)

file(GLOB_RECURSE AQUILA "src/aquila/*.cpp" "src/aquila/*.h")

add_executable(brainio main.cpp
        src/Brain.cpp
        src/Brain.h
        src/Sound.cpp
        src/Sound.h
        src/gateway/libsndfile.cpp
        src/gateway/libsndfile.h
        src/gateway/ISoundFileGateway.h
        ${AQUILA}
)

target_link_libraries(brainio SndFile::sndfile fftw::fftw)