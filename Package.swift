// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "BrainIO",
    platforms: [
        .iOS(.v16),
        .macOS(.v13)
    ],
    products: [
        .library(name: "BrainIO", targets: ["BrainIO"])
    ],
    targets: [
        .binaryTarget(
            name: "BrainIO",
            path: "build/BrainIO.xcframework"
        )
    ]
)
