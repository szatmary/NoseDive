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

option(VESC_BUILD_TESTS "Build tests" ON)
if(VESC_BUILD_TESTS)
    enable_testing()
    add_executable(vesc_tests tests/test_main.cpp)
    target_link_libraries(vesc_tests PRIVATE vesc)
    add_test(NAME vesc_tests COMMAND vesc_tests)
endif()
