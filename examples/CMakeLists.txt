cmake_minimum_required(VERSION 3.0)

project(FuryDemo)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(OS_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(OS_MACOSX 1)
endif()

set(CMAKE_CXX_FLAGS "-std=c++11")

set(FURY3D_INCLUDE "" CACHE PATH "Location of fury3d headers.")
set(FURY3D_LIB "" CACHE PATH "Location of fury3d lib.")

set(SFML_INCLUDE "/usr/local/include" CACHE PATH "Location of SFML headers.")
set(SFML_LIB "/usr/local/lib" CACHE PATH "Location of SFML lib.")

option(FBXPARSER_IMP "Fury Use FbxParser." OFF)
if(FBXPARSER_IMP)
	add_definitions(-D_FURY_FBXPARSER_IMP_)
	set(FBXSDK_INCLUDE "" CACHE PATH "Location of fbxsdk headers.")
	set(FBXSDK_LIB_RELEASE "/usr/local/lib" CACHE FILEPATH "Path to fbxsdk release lib.")
	set(FBXSDK_LIB_DEBUG "/usr/local/lib" CACHE FILEPATH "Path to fbxsdk debug lib.")
	set(FBXSDK_LIB optimized ${FBXSDK_LIB_RELEASE} debug ${FBXSDK_LIB_DEBUG})
endif()

option(GUI_IMP "Fury Use ImGui." ON)
if(GUI_IMP)
	add_definitions(-D_FURY_GUI_IMP_)
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)

if(FBXPARSER_IMP)
	include_directories(${FBXSDK_INCLUDE})
endif()

include_directories(${FURY3D_INCLUDE})
include_directories(${FURY3D_INCLUDE}/ThirdParty)
link_directories(${FURY3D_LIB})

include_directories(${SFML_INCLUDE})
link_directories(${SFML_LIB})

if(OS_MACOSX)
	find_package(OpenGL REQUIRED)
	include_directories(${OPENGL_INCLUDE_DIR})
endif()

file(GLOB DEMO_SRC ${DEMO_SRC} "*.cpp")
add_executable(demo ${DEMO_SRC})
if(OS_WINDOWS)
	target_link_libraries(demo libfury sfml-graphics sfml-window sfml-system opengl32 ${FBXSDK_LIB})
elseif(OS_MACOSX)
	target_link_libraries(demo fury sfml-graphics sfml-window sfml-system ${OPENGL_LIBRARIES} ${FBXSDK_LIB})
	set_target_properties(demo PROPERTIES BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
endif()

if(OS_WINDOWS)
	install(TARGETS demo DESTINATION bin)
elseif(OS_MACOSX)
	install(TARGETS demo DESTINATION bin/demo.app/Contents/MacOS)
endif()