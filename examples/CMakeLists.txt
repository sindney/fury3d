cmake_minimum_required(VERSION 3.22)

project(FuryDemo)

# The engine produces the `fury` executable directly (its CMake pulls in
# examples/main.cpp as the launcher's main()). This file exists so users can
# still run `cmake -S examples -B build-demo`; it just delegates upward to
# engine/CMakeLists.txt.

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../engine ${CMAKE_BINARY_DIR}/engine)
