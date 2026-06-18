cmake_minimum_required(VERSION 3.22)

project(FURY3D)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(OS_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(OS_MACOSX 1)
else()
	message(SEND_ERROR "Only support windows && osx currentlly.")
endif()

option(GUI_IMP "Use ImGui." ON)
if(GUI_IMP)
	add_definitions(-D_FURY_GUI_IMP_)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-int-to-void-pointer-cast")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -O2 -DNDEBUG")

option(BUILD_SHARED_LIBS "Build shared librarie." ON)

if(OS_WINDOWS)
	option(EXPORT_DLL "Define FURY_API_EXPORT" ON)
	if(EXPORT_DLL)
		add_definitions(-DFURY_API_EXPORT)
	endif()
endif()

# Vendored third-party submodules
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/SFML/CMakeLists.txt")
	message(FATAL_ERROR "SFML submodule missing — run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/rapidjson/include/rapidjson/document.h")
	message(FATAL_ERROR "rapidjson submodule missing — run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/tinygltf/tiny_gltf.h")
	message(FATAL_ERROR "tinygltf submodule missing — run: git submodule update --init --recursive")
endif()

# SFML 3.1: build only the modules we actually need
set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_GRAPHICS ON CACHE BOOL "" FORCE)
set(SFML_BUILD_WINDOW ON CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/SFML)

include_directories(${PROJECT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/rapidjson/include)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/STB)

file(GLOB IMGUI_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/ImGui/*.cpp)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/LZ4)
file(GLOB LZ4_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/LZ4/*.c)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/tinygltf)
set(TINYGLTF_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/tinygltf/tiny_gltf.cc)
set_source_files_properties(${TINYGLTF_SRC} PROPERTIES COMPILE_DEFINITIONS "TINYGLTF_NO_STB_IMAGE;TINYGLTF_NO_STB_IMAGE_WRITE")

if(OS_MACOSX)
	find_library(COREFOUNDATION_LIB CoreFoundation)
	find_package(OpenGL REQUIRED)
	include_directories(${OPENGL_INCLUDE_DIR})
endif()

file(GLOB FURY_SRC ${PROJECT_SOURCE_DIR}/Fury/*.cpp)
list(APPEND FURY_SRC ${IMGUI_SRC} ${LZ4_SRC} ${TINYGLTF_SRC})

if(BUILD_SHARED_LIBS)
	add_library(fury SHARED ${FURY_SRC})
	if(OS_WINDOWS)
		target_link_libraries(fury PUBLIC SFML::Window SFML::System opengl32)
	elseif(OS_MACOSX)
		target_link_libraries(fury PUBLIC SFML::Window SFML::System ${OPENGL_LIBRARIES} ${COREFOUNDATION_LIB})
		set_target_properties(fury PROPERTIES BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
	endif()
else()
	add_library(fury STATIC ${FURY_SRC})
	if(OS_WINDOWS)
		target_link_libraries(fury PUBLIC SFML::Window SFML::System opengl32)
	elseif(OS_MACOSX)
		target_link_libraries(fury PUBLIC SFML::Window SFML::System ${OPENGL_LIBRARIES} ${COREFOUNDATION_LIB})
	endif()
endif()

install(TARGETS fury DESTINATION lib)
