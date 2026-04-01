cmake_minimum_required(VERSION 3.16)
project(nosedive VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Library sources
set(NOSEDIVE_SOURCES
    src/crc.cpp
    src/protocol.cpp
    src/commands.cpp
    src/profile.cpp
    src/refloat.cpp
    src/ble_transport.cpp
    src/storage.cpp
    src/engine.cpp
)

# Static library
add_library(nosedive STATIC ${NOSEDIVE_SOURCES})
target_include_directories(nosedive PUBLIC include)

# C API shared library (for Android / Kotlin-JNI)
add_library(nosedive_ffi SHARED src/ffi.cpp ${NOSEDIVE_SOURCES})
target_include_directories(nosedive_ffi PUBLIC include)

# C API static library (for iOS / Swift SPM linking)
add_library(nosedive_ffi_static STATIC src/ffi.cpp ${NOSEDIVE_SOURCES})
target_include_directories(nosedive_ffi_static PUBLIC include)
set_target_properties(nosedive_ffi_static PROPERTIES OUTPUT_NAME nosedive_ffi)

# iOS framework settings
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(nosedive PROPERTIES
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
    )
    set_target_properties(nosedive_ffi PROPERTIES
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER com.nosedive.lib
    )
endif()

# Android NDK settings
if(ANDROID)
    set_target_properties(nosedive_ffi PROPERTIES
        LIBRARY_OUTPUT_NAME "nosedive"
    )
endif()

# Tests
option(NOSEDIVE_BUILD_TESTS "Build tests" ON)
if(NOSEDIVE_BUILD_TESTS)
    enable_testing()
    add_executable(nosedive_tests tests/test_main.cpp src/ffi.cpp)
    target_link_libraries(nosedive_tests PRIVATE nosedive)
    add_test(NAME nosedive_tests COMMAND nosedive_tests)
endif()
