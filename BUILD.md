# Building NoseDive

## Prerequisites

| Component | Requires |
|-----------|----------|
| CLI + Simulator | Go 1.24+ |
| C++ Library | CMake 3.16+, C++17 compiler |
| iOS/macOS App | Xcode 15+, macOS 14+ |
| Android (future) | Android NDK, CMake |

## Go CLI + Simulator

```bash
# Build
go build -o nosedive ./cmd/nosedive

# Run simulator
./nosedive -sim

# Run simulator with web GUI
./nosedive -sim -web

# Run simulator with BLE advertising
./nosedive -sim -sim-ble -ble-name "My Board"

# Connect to real board over TCP
./nosedive -addr 192.168.4.1:65102

# Scan for BLE boards
./nosedive -ble-scan

# Connect via BLE
./nosedive -ble <address>

# Load a board profile
./nosedive -sim -profile profiles/funwheel_x7_lr_hs.json
```

## C++ Core Library (libnosedive)

```bash
cd lib/nosedive

# Configure + build
cmake -B build
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

### Build outputs

| Target | Type | Description |
|--------|------|-------------|
| `libnosedive.a` | Static lib | Link directly into C++/Swift/ObjC targets |
| `libnosedive_ffi.so/.dylib` | Shared lib | FFI for Swift bridging and Kotlin JNI |
| `nosedive_tests` | Executable | Test suite |

### Cross-compile for iOS

```bash
cmake -B build-ios \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DNOSEDIVE_BUILD_TESTS=OFF
cmake --build build-ios
```

### Cross-compile for Android

```bash
cmake -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DNOSEDIVE_BUILD_TESTS=OFF
cmake --build build-android
```

## iOS / macOS App

The app is a Swift Package at `ios/NoseDive/`.

### Open in Xcode

```bash
open ios/NoseDive/Package.swift
```

Xcode will resolve the package and you can build/run from there. Select an iOS 17+ simulator or device, or a macOS 14+ target.

### Build from command line

```bash
cd ios/NoseDive

# iOS simulator
xcodebuild \
  -scheme NoseDive \
  -destination 'platform=iOS Simulator,name=iPhone 16 Pro' \
  build

# macOS
xcodebuild \
  -scheme NoseDive \
  -destination 'platform=macOS' \
  build
```

### Platform targets

- iOS 17.0+
- iPadOS 17.0+
- macOS 14.0+ (Sonoma)

## Board Profiles

JSON board profiles live in `profiles/`. Load them with the CLI:

```bash
./nosedive -sim -profile profiles/funwheel_x7_lr_hs.json
./nosedive -sim -profile profiles/diy_ubox_superflux.json
./nosedive -sim -profile profiles/onewheel_xr_vesc.json
```

## Project Layout

```
cmd/nosedive/       Go CLI entry point
pkg/                Go packages (vesc, refloat, simulator, board)
lib/nosedive/       C++ shared backend library
  include/          Public headers
  src/              Implementation
  tests/            Test suite
ios/NoseDive/       SwiftUI app (iOS, iPad, macOS)
profiles/           Board profile JSON files
docs/               Design docs (vision, tuning, setup wizard, protocol)
```
