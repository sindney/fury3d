cmake_minimum_required(VERSION 3.22)

project(FuryDemo)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(OS_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(OS_MACOSX 1)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GUI_IMP "Fury Use ImGui." ON)
if(GUI_IMP)
	add_definitions(-D_FURY_GUI_IMP_)
endif()

# Build the engine (and its vendored SFML / rapidjson / tinygltf) in-tree
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../engine ${CMAKE_BINARY_DIR}/engine)

# Engine include path (engine target's INTERFACE_INCLUDE_DIRECTORIES is not
# set in the legacy engine CMake; expose it explicitly here so Demo.cpp can
# `#include <Fury/Fury.h>`).
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../engine)

if(OS_MACOSX)
	find_package(OpenGL REQUIRED)
	include_directories(${OPENGL_INCLUDE_DIR})
endif()

file(GLOB DEMO_SRC ${DEMO_SRC} "*.cpp")
add_executable(demo ${DEMO_SRC})

if(OS_WINDOWS)
	target_link_libraries(demo PRIVATE fury SFML::Graphics SFML::Window SFML::System opengl32)
elseif(OS_MACOSX)
	target_link_libraries(demo PRIVATE fury SFML::Graphics SFML::Window SFML::System ${OPENGL_LIBRARIES})
	set_target_properties(demo PROPERTIES BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
endif()

if(OS_WINDOWS)
	install(TARGETS demo DESTINATION bin)
elseif(OS_MACOSX)
	install(TARGETS demo DESTINATION bin/demo.app/Contents/MacOS)
endif()
