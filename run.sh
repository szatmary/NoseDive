#!/bin/bash
set -e

cd "$(dirname "$0")"

# Build C++ library
echo "Building C++ engine..."
cd lib/nosedive
cmake -B build -DCMAKE_BUILD_TYPE=Debug 2>/dev/null
cmake --build build --parallel
cd ../..

# Copy dylib to Swift build dir
SWIFT_DEBUG="ios/NoseDive/.build/arm64-apple-macosx/debug"
mkdir -p "$SWIFT_DEBUG"
cp lib/nosedive/build/libnosedive_ffi.dylib "$SWIFT_DEBUG/"

# Build Swift app
echo "Building NoseDive app..."
cd ios/NoseDive
swift build

# Run
echo "Launching NoseDive..."
exec swift run
