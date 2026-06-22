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

# Default to building a single static fury executable (engine + Lua launcher).
# Set BUILD_SHARED_LIBS=ON to instead get a libfury.{dylib,so,dll} for embedding.
option(BUILD_SHARED_LIBS "Build the engine as a shared library instead of an executable." OFF)

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
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/lua/lua.h")
	message(FATAL_ERROR "lua submodule missing — run: git submodule update --init --recursive")
endif()
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/sol2/include/sol/sol.hpp")
	message(FATAL_ERROR "sol2 submodule missing — run: git submodule update --init --recursive")
endif()

# SFML 3.1: build only the modules we actually need
set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_GRAPHICS ON CACHE BOOL "" FORCE)
set(SFML_BUILD_WINDOW ON CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/SFML)

# Lua 5.4: upstream is Makefile-only with sources flat at repo root.
# We compile them into a static library here. Exclude the standalone-interpreter
# main (lua.c) and the alternative single-file build (onelua.c) to avoid
# duplicate-symbol / unwanted-main errors.
file(GLOB LUA_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/lua/*.c)
list(REMOVE_ITEM LUA_SRC
	${PROJECT_SOURCE_DIR}/ThirdParty/lua/lua.c
	${PROJECT_SOURCE_DIR}/ThirdParty/lua/onelua.c)
add_library(lua STATIC ${LUA_SRC})
target_include_directories(lua PUBLIC ${PROJECT_SOURCE_DIR}/ThirdParty/lua)
target_compile_definitions(lua PRIVATE
	$<$<PLATFORM_ID:Darwin>:LUA_USE_MACOSX>
	$<$<PLATFORM_ID:Linux>:LUA_USE_LINUX>)
set_property(TARGET lua PROPERTY POSITION_INDEPENDENT_CODE ON)

# sol2 is header-only.
include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/sol2/include)

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

# Path to the Lua launcher's main(). Resolves whether engine is the top-level
# project or pulled in via add_subdirectory from examples/.
set(FURY_LAUNCHER_SRC "${PROJECT_SOURCE_DIR}/../examples/main.cpp")

if(BUILD_SHARED_LIBS)
	# Embedder path: produce a shared libfury (no main).
	add_library(fury SHARED ${FURY_SRC})
	if(OS_WINDOWS)
		target_link_libraries(fury PUBLIC SFML::Window SFML::System opengl32 lua)
	elseif(OS_MACOSX)
		target_link_libraries(fury PUBLIC SFML::Window SFML::System ${OPENGL_LIBRARIES} ${COREFOUNDATION_LIB} lua)
		set_target_properties(fury PROPERTIES BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
	endif()
	install(TARGETS fury DESTINATION lib)
else()
	# Common path: the engine IS the executable. The launcher source lives in
	# examples/main.cpp so demo authors can read it.
	if(NOT EXISTS "${FURY_LAUNCHER_SRC}")
		message(FATAL_ERROR "Launcher source not found at ${FURY_LAUNCHER_SRC} — examples/main.cpp must exist alongside engine/.")
	endif()
	add_executable(fury ${FURY_SRC} ${FURY_LAUNCHER_SRC})
	if(OS_WINDOWS)
		target_link_libraries(fury PRIVATE SFML::Window SFML::System opengl32 lua)
	elseif(OS_MACOSX)
		target_link_libraries(fury PRIVATE SFML::Window SFML::System ${OPENGL_LIBRARIES} ${COREFOUNDATION_LIB} lua)
	endif()
	install(TARGETS fury DESTINATION bin)
endif()

# Copy the host-platform FBX2glTF binary next to the built `fury` executable
# (or library, if BUILD_SHARED_LIBS). FbxConverter::LocateBinary() looks for
# this file in the same directory as the running executable; without the copy
# the runtime falls back to the engine source tree, which is fine for
# development but breaks any installed layout.
if(OS_MACOSX)
	set(FBX2GLTF_BINARY "FBX2glTF-darwin-x64")
elseif(OS_WINDOWS)
	set(FBX2GLTF_BINARY "FBX2glTF-windows-x64.exe")
else()
	set(FBX2GLTF_BINARY "FBX2glTF-linux-x64")
endif()
set(FBX2GLTF_SRC_BINARY "${PROJECT_SOURCE_DIR}/ThirdParty/FBX2glTF/${FBX2GLTF_BINARY}")
if(EXISTS "${FBX2GLTF_SRC_BINARY}")
	add_custom_command(TARGET fury POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${FBX2GLTF_SRC_BINARY}"
			"$<TARGET_FILE_DIR:fury>/${FBX2GLTF_BINARY}"
		COMMENT "Copying FBX2glTF binary next to fury"
		VERBATIM)
	if(NOT OS_WINDOWS)
		add_custom_command(TARGET fury POST_BUILD
			COMMAND chmod +x "$<TARGET_FILE_DIR:fury>/${FBX2GLTF_BINARY}"
			VERBATIM)
	endif()
endif()

# Copy the host-platform FBX2glTF binary next to the built `fury` executable
# (or library, if BUILD_SHARED_LIBS). FbxConverter::LocateBinary() looks for
# this file in the same directory as the running executable; without the copy
# the runtime falls back to the engine source tree, which is fine for
# development but breaks any installed layout.
if(OS_MACOSX)
	set(FBX2GLTF_BINARY "FBX2glTF-darwin-x64")
elseif(OS_WINDOWS)
	set(FBX2GLTF_BINARY "FBX2glTF-windows-x64.exe")
else()
	set(FBX2GLTF_BINARY "FBX2glTF-linux-x64")
endif()
set(FBX2GLTF_SRC_BINARY "${PROJECT_SOURCE_DIR}/ThirdParty/FBX2glTF/${FBX2GLTF_BINARY}")
if(EXISTS "${FBX2GLTF_SRC_BINARY}")
	add_custom_command(TARGET fury POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${FBX2GLTF_SRC_BINARY}"
			"$<TARGET_FILE_DIR:fury>/${FBX2GLTF_BINARY}"
		COMMENT "Copying FBX2glTF binary next to fury"
		VERBATIM)
	if(NOT OS_WINDOWS)
		add_custom_command(TARGET fury POST_BUILD
			COMMAND chmod +x "$<TARGET_FILE_DIR:fury>/${FBX2GLTF_BINARY}"
			VERBATIM)
	endif()
endif()
