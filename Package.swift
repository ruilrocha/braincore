// swift-tools-version: 6.0
//
// BrainCore — Swift Package for the brain-io C++ audio matching library.
//
// Exposes the domain core + analysis + search + effects adapters as a single
// C++ static library callable from Swift via C++ interop (Swift 5.9+/Xcode 16+).
//
// Usage in a Swift target:
//   swiftSettings: [.interoperabilityMode(.Cxx)]
//   import BrainCore
//
// iOS 16+ / macOS 13+ — both support C++23 with Xcode 15+.

import PackageDescription

let package = Package( 
    name: "BrainCore",
    platforms: [
        .iOS(.v16),
        .macOS(.v13),
    ],
    products: [
        .library(name: "BrainCore", targets: ["BrainCore"]),
    ],
    targets: [
        .target(
            name: "BrainCore",
            path: "src",
            // Only src/include/ is exposed to Swift — BrainCore.h includes
            // BrainSession.h which uses Pimpl so Swift's module generator
            // never sees heavy C++20/23 headers.
            publicHeadersPath: "include",
            // vendor/pocketfft/ is outside src/ — add as a search path so
            // `#include <pocketfft_hdronly.h>` resolves for SPM builds.
            // (CMake builds use the Conan-installed copy instead.)
            cxxSettings: [
                .headerSearchPath("../vendor/pocketfft"),
            ]
        ),
    ],
    cxxLanguageStandard: .cxx2b
)

