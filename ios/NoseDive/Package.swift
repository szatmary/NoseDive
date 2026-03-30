// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "NoseDive",
    platforms: [
        .iOS(.v17),
        .macOS(.v14)
    ],
    products: [
        .library(name: "NoseDive", targets: ["NoseDive"])
    ],
    targets: [
        .target(
            name: "NoseDive",
            path: "Sources"
        )
    ]
)
