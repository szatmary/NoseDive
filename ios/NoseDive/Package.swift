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
        .systemLibrary(
            name: "CNoseDive",
            path: "Sources/CNoseDive"
        ),
        .target(
            name: "NoseDive",
            dependencies: ["CNoseDive"],
            path: "Sources",
            exclude: ["CNoseDive"],
            linkerSettings: [
                .unsafeFlags([
                    "-L../../lib/nosedive/build",
                ]),
            ]
        )
    ]
)
