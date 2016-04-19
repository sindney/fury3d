cmake_minimum_required(VERSION 3.0)

project(FuryDemo)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(OS_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(OS_MACOSX 1)
endif()

set(CMAKE_CXX_FLAGS "-std=c++11")

set(FBXSDK_INCLUDE "" CACHE PATH "Location of fbxsdk headers.")
set(FBXSDK_LIB "" CACHE PATH "Location of fbxsdk lib.")

set(FURY3D_INCLUDE "" CACHE PATH "Location of fury3d headers.")
set(FURY3D_LIB "" CACHE PATH "Location of fury3d lib.")

set(SFML_INCLUDE "/usr/local/include" CACHE PATH "Location of SFML headers.")
set(SFML_LIB "/usr/local/lib" CACHE PATH "Location of SFML lib.")

include_directories(${PROJECT_SOURCE_DIR}/include)

include_directories(${FBXSDK_INCLUDE})
link_directories(${FBXSDK_LIB})

include_directories(${FURY3D_INCLUDE})
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
	target_link_libraries(demo libfury libfbxsdk-md sfml-graphics sfml-window sfml-system opengl32)
elseif(OS_MACOSX)
	target_link_libraries(demo fury fbxsdk sfml-graphics sfml-window sfml-system ${OPENGL_LIBRARIES})
	set_target_properties(demo PROPERTIES BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
endif()

if(OS_WINDOWS)
	install(TARGETS demo DESTINATION bin)
elseif(OS_MACOSX)
	install(TARGETS demo DESTINATION bin/demo.app/Contents/MacOS)
endif()