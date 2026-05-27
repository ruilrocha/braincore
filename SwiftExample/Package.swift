// swift-tools-version: 5.9
//
// SwiftExample — minimal macOS executable that tests the BrainCore C++ library
// via Swift C++ interop.
//
// Build: swift build (from this directory)
// Run:   swift run

import PackageDescription

let package = Package(
    name: "SwiftExample",
    platforms: [.macOS(.v13)],
    dependencies: [
        // Local path: parent directory is brain-io.
        .package(path: ".."),
    ],
    targets: [
        .executableTarget(
            name: "SwiftExample",
            dependencies: [
                .product(name: "BrainCore", package: "brain-io"),
            ],
            swiftSettings: [
                // Enable Swift ↔ C++ interop (required for BrainCore).
                .interoperabilityMode(.Cxx),
                // Pass C++23 standard to clang when Swift parses BrainCore headers.
                // BrainCore uses std::span, std::ranges, std::numbers (C++20/23).
                .unsafeFlags(["-Xcc", "-std=c++2b"]),
            ]
        ),
    ]
)
