#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────
# build-xcframework.sh
#
# Builds brainio-core + brainio-fft as a universal XCFramework for:
#   - iOS (arm64)
#   - iOS Simulator (arm64)
#   - macOS (arm64)
#
# Requirements:
#   - Xcode with iOS SDK
#   - Conan 2.x
#   - CMake 3.24+
#
# Output: build/BrainIO.xcframework/
# ──────────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/xcframework"
OUTPUT_DIR="$PROJECT_ROOT/build/BrainIO.xcframework"

# Common CMake flags for library-only build (no CLI, no playback, no I/O, no UI).
CMAKE_COMMON=(
    -DCMAKE_CXX_STANDARD=23
    -DBRAINIO_FFT_BACKEND=pocketfft
    -DBRAINIO_IO_BACKEND=none
    -DBRAINIO_BUILD_PLAYBACK=OFF
    -DBRAINIO_BUILD_UI=OFF
    -DBRAINIO_BUILD_CLI=OFF
)

# ── Helper: build for a platform ───────────────────────────────────────
build_platform() {
    local PLATFORM=$1   # e.g. "iphoneos", "iphonesimulator", "macosx"
    local ARCH=$2       # e.g. "arm64"
    local SYSROOT
    SYSROOT=$(xcrun --sdk "$PLATFORM" --show-sdk-path)

    local PLATFORM_BUILD="$BUILD_DIR/$PLATFORM-$ARCH"
    echo "╔═══ Building for $PLATFORM ($ARCH) ═══╗"

    # Conan install for cross-compilation.
    # For header-only packages (pocketfft), no cross-compile profile needed.
    mkdir -p "$PLATFORM_BUILD"

    # Run Conan install (reuses cached packages since pocketfft is header-only).
    (cd "$PROJECT_ROOT" && conan install . \
        --output-folder="$PLATFORM_BUILD" \
        --build=missing \
        -s os=iOS -s os.version=16.0 -s arch=armv8 \
        2>/dev/null || true)

    # CMake configure.
    cmake -S "$PROJECT_ROOT" -B "$PLATFORM_BUILD/build" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
        -DCMAKE_OSX_SYSROOT="$SYSROOT" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="16.0" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$PLATFORM_BUILD/build/Release/generators/conan_toolchain.cmake" \
        "${CMAKE_COMMON[@]}" \
        2>/dev/null || \
    cmake -S "$PROJECT_ROOT" -B "$PLATFORM_BUILD/build" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
        -DCMAKE_OSX_SYSROOT="$SYSROOT" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="16.0" \
        -DCMAKE_BUILD_TYPE=Release \
        "${CMAKE_COMMON[@]}"

    # Build.
    cmake --build "$PLATFORM_BUILD/build" --config Release

    # Merge static libs into one fat archive.
    local LIB_DIR="$PLATFORM_BUILD/build"
    libtool -static -o "$PLATFORM_BUILD/libbrainio.a" \
        "$LIB_DIR/libbrainio-core.a" \
        "$LIB_DIR/libbrainio-fft.a"

    echo "  → $PLATFORM_BUILD/libbrainio.a"
}

# ── Helper: build for macOS ────────────────────────────────────────────
build_macos() {
    local PLATFORM_BUILD="$BUILD_DIR/macosx-arm64"
    echo "╔═══ Building for macOS (arm64) ═══╗"

    mkdir -p "$PLATFORM_BUILD"

    (cd "$PROJECT_ROOT" && conan install . \
        --output-folder="$PLATFORM_BUILD" \
        --build=missing 2>/dev/null || true)

    cmake -S "$PROJECT_ROOT" -B "$PLATFORM_BUILD/build" \
        -DCMAKE_OSX_ARCHITECTURES="arm64" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$PLATFORM_BUILD/build/Release/generators/conan_toolchain.cmake" \
        "${CMAKE_COMMON[@]}" \
        2>/dev/null || \
    cmake -S "$PROJECT_ROOT" -B "$PLATFORM_BUILD/build" \
        -DCMAKE_OSX_ARCHITECTURES="arm64" \
        -DCMAKE_BUILD_TYPE=Release \
        "${CMAKE_COMMON[@]}"

    cmake --build "$PLATFORM_BUILD/build" --config Release

    local LIB_DIR="$PLATFORM_BUILD/build"
    libtool -static -o "$PLATFORM_BUILD/libbrainio.a" \
        "$LIB_DIR/libbrainio-core.a" \
        "$LIB_DIR/libbrainio-fft.a"

    echo "  → $PLATFORM_BUILD/libbrainio.a"
}

# ── Build all platforms ─────────────────────────────────────────────────
rm -rf "$BUILD_DIR" "$OUTPUT_DIR"
mkdir -p "$BUILD_DIR"

build_platform "iphoneos" "arm64"
build_platform "iphonesimulator" "arm64"
build_macos

# ── Create XCFramework ──────────────────────────────────────────────────
echo "╔═══ Creating XCFramework ═══╗"

xcodebuild -create-xcframework \
    -library "$BUILD_DIR/iphoneos-arm64/libbrainio.a" \
    -headers "$PROJECT_ROOT/include" \
    -library "$BUILD_DIR/iphonesimulator-arm64/libbrainio.a" \
    -headers "$PROJECT_ROOT/include" \
    -library "$BUILD_DIR/macosx-arm64/libbrainio.a" \
    -headers "$PROJECT_ROOT/include" \
    -output "$OUTPUT_DIR"

echo ""
echo "✅ XCFramework created at: $OUTPUT_DIR"
echo ""
echo "To use in an iOS/macOS project:"
echo "  1. Drag BrainIO.xcframework into your Xcode project, or"
echo "  2. Reference it via Package.swift as a binary target."
