cmake_minimum_required(VERSION 3.18)
project(nosedive-jni)

set(CMAKE_CXX_STANDARD 17)

# Path to the shared C++ library
set(NOSEDIVE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../lib/nosedive)

# Build libnosedive sources directly into the JNI .so
file(GLOB NOSEDIVE_SOURCES ${NOSEDIVE_DIR}/src/*.cpp)

add_library(nosedive_jni SHARED
    nosedive_jni.cpp
    ${NOSEDIVE_SOURCES}
)

target_include_directories(nosedive_jni PRIVATE
    ${NOSEDIVE_DIR}/include
)

find_library(log-lib log)
target_link_libraries(nosedive_jni ${log-lib})
