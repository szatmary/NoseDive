# libvesc Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract a standalone `libvesc` C++ library that implements the full VESC protocol — parsing, encoding, framing, CRC, and type definitions. Zero app logic. Reusable by any VESC project (including the Go simulator).

**Architecture:** `lib/vesc/` is a self-contained CMake library with no dependencies beyond C++17 stdlib. `lib/nosedive/` depends on `lib/vesc/` and adds app-specific logic (engine, storage, profiles, FFI).

**Tech Stack:** C++17, CMake

---

## File Map

### Create: `lib/vesc/`
```
lib/vesc/
  CMakeLists.txt
  include/vesc/
    vesc.hpp          — umbrella header
    crc.hpp           — CRC16 (moved from nosedive)
    protocol.hpp      — packet framing, decoder, buffer (moved from nosedive)
    commands.hpp      — CommPacketID, Values, FWVersion, Telemetry, parsers, builders (moved from nosedive)
    refloat.hpp       — Refloat types, parsers, builders (moved from nosedive)
  src/
    crc.cpp
    protocol.cpp
    commands.cpp
    refloat.cpp
  tests/
    test_main.cpp     — protocol-level tests (extracted from nosedive tests)
```

### Modify: `lib/nosedive/`
- `CMakeLists.txt` — depends on `lib/vesc/`, remove moved sources
- `include/nosedive/nosedive.hpp` — include `vesc/vesc.hpp` instead of individual files
- `include/nosedive/engine.hpp` — `#include <vesc/commands.hpp>` etc.
- `include/nosedive/storage.hpp` — may reference vesc types
- `src/*.cpp` — update includes
- `tests/test_main.cpp` — remove protocol tests (now in libvesc), keep app tests

### No changes
- `lib/nosedive/include/nosedive/ffi.h` — already clean
- `lib/nosedive/src/ffi.cpp` — includes stay the same (paths change)
- `lib/nosedive/include/nosedive/profile.hpp` — no vesc dependency
- `lib/nosedive/src/profile.cpp` — no vesc dependency
- `lib/nosedive/src/storage.cpp` — no vesc dependency

---

### Task 1: Create lib/vesc/ directory structure

**Files:**
- Create: `lib/vesc/CMakeLists.txt`
- Create: `lib/vesc/include/vesc/vesc.hpp`

- [ ] **Step 1: Create directory structure**

```bash
mkdir -p lib/vesc/include/vesc lib/vesc/src lib/vesc/tests
```

- [ ] **Step 2: Create CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(vesc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(VESC_SOURCES
    src/crc.cpp
    src/protocol.cpp
    src/commands.cpp
    src/refloat.cpp
)

add_library(vesc STATIC ${VESC_SOURCES})
target_include_directories(vesc PUBLIC include)

# Tests
option(VESC_BUILD_TESTS "Build tests" ON)
if(VESC_BUILD_TESTS)
    enable_testing()
    add_executable(vesc_tests tests/test_main.cpp)
    target_link_libraries(vesc_tests PRIVATE vesc)
    add_test(NAME vesc_tests COMMAND vesc_tests)
endif()
```

- [ ] **Step 3: Create umbrella header**

```cpp
// lib/vesc/include/vesc/vesc.hpp
#pragma once

#include "vesc/crc.hpp"
#include "vesc/protocol.hpp"
#include "vesc/commands.hpp"
#include "vesc/refloat.hpp"
```

- [ ] **Step 4: Commit**

```bash
git add lib/vesc/
git commit -m "Create lib/vesc/ directory structure"
```

---

### Task 2: Move protocol files to libvesc

**Files:**
- Move: `lib/nosedive/include/nosedive/crc.hpp` → `lib/vesc/include/vesc/crc.hpp`
- Move: `lib/nosedive/src/crc.cpp` → `lib/vesc/src/crc.cpp`
- Move: `lib/nosedive/include/nosedive/protocol.hpp` → `lib/vesc/include/vesc/protocol.hpp`
- Move: `lib/nosedive/src/protocol.cpp` → `lib/vesc/src/protocol.cpp`
- Move: `lib/nosedive/include/nosedive/commands.hpp` → `lib/vesc/include/vesc/commands.hpp`
- Move: `lib/nosedive/src/commands.cpp` → `lib/vesc/src/commands.cpp`
- Move: `lib/nosedive/include/nosedive/refloat.hpp` → `lib/vesc/include/vesc/refloat.hpp`
- Move: `lib/nosedive/src/refloat.cpp` → `lib/vesc/src/refloat.cpp`

- [ ] **Step 1: Copy files**

```bash
cp lib/nosedive/include/nosedive/crc.hpp lib/vesc/include/vesc/
cp lib/nosedive/src/crc.cpp lib/vesc/src/
cp lib/nosedive/include/nosedive/protocol.hpp lib/vesc/include/vesc/
cp lib/nosedive/src/protocol.cpp lib/vesc/src/
cp lib/nosedive/include/nosedive/commands.hpp lib/vesc/include/vesc/
cp lib/nosedive/src/commands.cpp lib/vesc/src/
cp lib/nosedive/include/nosedive/refloat.hpp lib/vesc/include/vesc/
cp lib/nosedive/src/refloat.cpp lib/vesc/src/
```

- [ ] **Step 2: Update include paths in copied files**

In all `lib/vesc/` source files, replace `#include "nosedive/..."` with `#include "vesc/..."`:

```bash
cd lib/vesc
find . -name "*.cpp" -o -name "*.hpp" | xargs sed -i '' 's|nosedive/crc|vesc/crc|g; s|nosedive/protocol|vesc/protocol|g; s|nosedive/commands|vesc/commands|g; s|nosedive/refloat|vesc/refloat|g'
```

Change `namespace nosedive` to `namespace vesc` in all libvesc files:

```bash
find . -name "*.cpp" -o -name "*.hpp" | xargs sed -i '' 's/namespace nosedive/namespace vesc/g; s/nosedive::/vesc::/g'
```

- [ ] **Step 3: Build libvesc standalone**

```bash
cd lib/vesc && cmake -B build && cmake --build build --parallel
```

- [ ] **Step 4: Commit**

```bash
git add lib/vesc/
git commit -m "Move CRC, protocol, commands, refloat to lib/vesc/"
```

---

### Task 3: Extract protocol tests into libvesc

**Files:**
- Create: `lib/vesc/tests/test_main.cpp`
- Modify: `lib/nosedive/tests/test_main.cpp` (remove protocol tests)

- [ ] **Step 1: Create libvesc test file**

Extract these tests from `lib/nosedive/tests/test_main.cpp` into `lib/vesc/tests/test_main.cpp`:
- `test_crc16`
- `test_packet_roundtrip_short`
- `test_packet_roundtrip_long`
- `test_buffer_int16`, `test_buffer_int32`, `test_buffer_float16`, `test_buffer_float32_auto`, `test_buffer_string`
- `test_packet_decoder_single`, `test_packet_decoder_chunked`, `test_packet_decoder_multiple`
- `test_refloat_command_builders`, `test_refloat_compat_decoders`
- `test_parse_fw_version`, `test_parse_ping_can`, `test_parse_refloat_info`
- `test_command_builders`, `test_computed_values`

Update includes to use `vesc/` headers and `vesc::` namespace.

- [ ] **Step 2: Remove extracted tests from nosedive**

Remove the moved test functions and their calls from `lib/nosedive/tests/test_main.cpp`. Keep only:
- `test_profile_load`
- `test_storage_roundtrip`
- `test_engine_ffi`
- `test_engine_payload`

- [ ] **Step 3: Build and run both test suites**

```bash
cd lib/vesc && cmake --build build --parallel && cd build && ./vesc_tests
cd lib/nosedive && cmake --build build --parallel && cd build && ./nosedive_tests
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Split tests: protocol tests in libvesc, app tests in libnosedive"
```

---

### Task 4: Update libnosedive to depend on libvesc

**Files:**
- Modify: `lib/nosedive/CMakeLists.txt`
- Modify: `lib/nosedive/include/nosedive/nosedive.hpp`
- Modify: `lib/nosedive/include/nosedive/engine.hpp`
- Modify: `lib/nosedive/src/engine.cpp`
- Modify: `lib/nosedive/src/ffi.cpp`
- Delete: `lib/nosedive/include/nosedive/crc.hpp`
- Delete: `lib/nosedive/include/nosedive/protocol.hpp`
- Delete: `lib/nosedive/include/nosedive/commands.hpp`
- Delete: `lib/nosedive/include/nosedive/refloat.hpp`
- Delete: `lib/nosedive/src/crc.cpp`
- Delete: `lib/nosedive/src/protocol.cpp`
- Delete: `lib/nosedive/src/commands.cpp`
- Delete: `lib/nosedive/src/refloat.cpp`

- [ ] **Step 1: Update CMakeLists.txt**

Add libvesc as a subdirectory dependency:

```cmake
# At the top, add libvesc
add_subdirectory(../vesc ${CMAKE_BINARY_DIR}/vesc)

# Remove moved sources from NOSEDIVE_SOURCES
set(NOSEDIVE_SOURCES
    src/storage.cpp
    src/engine.cpp
    src/profile.cpp
)

# Link against vesc
target_link_libraries(nosedive PUBLIC vesc)
target_link_libraries(nosedive_ffi PUBLIC vesc)
target_link_libraries(nosedive_ffi_static PUBLIC vesc)
target_link_libraries(nosedive_tests PRIVATE nosedive vesc)
```

- [ ] **Step 2: Update includes in nosedive files**

Replace all `#include "nosedive/crc.hpp"` with `#include "vesc/crc.hpp"`, etc.
Replace all `nosedive::` references to vesc types with `vesc::` (or add `using namespace vesc;` / `using vesc::CommPacketID;` etc.).

Update `nosedive.hpp`:
```cpp
#pragma once
#include "vesc/vesc.hpp"
#include "nosedive/profile.hpp"
#include "nosedive/storage.hpp"
#include "nosedive/engine.hpp"
```

- [ ] **Step 3: Delete moved files from nosedive**

```bash
rm lib/nosedive/include/nosedive/crc.hpp
rm lib/nosedive/include/nosedive/protocol.hpp
rm lib/nosedive/include/nosedive/commands.hpp
rm lib/nosedive/include/nosedive/refloat.hpp
rm lib/nosedive/src/crc.cpp
rm lib/nosedive/src/protocol.cpp
rm lib/nosedive/src/commands.cpp
rm lib/nosedive/src/refloat.cpp
```

- [ ] **Step 4: Build and test everything**

```bash
cd lib/nosedive && rm -rf build && cmake -B build && cmake --build build --parallel
cd build && ./nosedive_tests
```

- [ ] **Step 5: Build iOS app**

```bash
cd ios/NoseDive && rm -rf .build && swift build
```

(May need to update the module map or linker flags to also link libvesc)

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "libnosedive depends on libvesc, protocol files removed from nosedive"
```

---

### Task 5: Namespace cleanup

**Files:**
- All libvesc files: use `namespace vesc`
- All libnosedive files: use `namespace nosedive`, reference vesc types with `vesc::` prefix

- [ ] **Step 1: Ensure clean namespace separation**

libvesc types in `vesc::` namespace: `vesc::CommPacketID`, `vesc::Values`, `vesc::FWVersion`, `vesc::Telemetry`, `vesc::Buffer`, `vesc::PacketDecoder`, `vesc::encode_packet`, `vesc::crc16`, etc.

libnosedive types in `nosedive::` namespace: `nosedive::Engine`, `nosedive::Storage`, `nosedive::Board`, `nosedive::RiderProfile`, etc.

Engine references vesc types explicitly: `vesc::Telemetry`, `vesc::FWVersion`, etc.

- [ ] **Step 2: Build and test**

```bash
cd lib/vesc && cmake --build build && cd build && ./vesc_tests
cd lib/nosedive && cmake --build build && cd build && ./nosedive_tests
```

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "Clean namespace separation: vesc:: for protocol, nosedive:: for app"
```
