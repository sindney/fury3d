cmake_minimum_required(VERSION 3.22)

# Enable OBJC for nfd's nfd_cocoa.m on macOS (gated behind add_subdirectory
# under WITH_EDITOR; the language is harmless on platforms that don't use it
# since CMake no-ops the toolchain when no .m sources are present).
if(APPLE)
	project(FURY3D LANGUAGES C CXX OBJC)
else()
	project(FURY3D LANGUAGES C CXX)
endif()

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

# Two binaries from one configure:
#   fury   — headless runtime, no editor shell (no ImReflect/ImGuizmo/nfd, no WITH_EDITOR)
#   furye  — editor shell + runtime (ImReflect/ImGuizmo/nfd linked, WITH_EDITOR defined)
# Both targets share the engine core; only the editor sources / defines differ.
# This replaces the old WITH_EDITOR=ON/OFF + two-separate-build-dirs flow.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-int-to-void-pointer-cast")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -O2 -DNDEBUG")

# ImReflect (vendored at ThirdParty/ImReflect) uses C++17 template-template
# parameters with defaulted template arguments — accepted by P0522 but
# Clang gates it behind -frelaxed-template-template-args until C++20.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -frelaxed-template-template-args")
endif()

# Default to building static binaries. Set BUILD_SHARED_LIBS=ON to instead get
# a libfury.{dylib,so,dll} for embedding (editor variant only — the embedder
# picks which feature set to consume; see WITH_EDITOR_API below).
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
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/meshoptimizer.h")
	message(FATAL_ERROR "meshoptimizer missing — vendor src/meshoptimizer.h and the split .cpp files under engine/ThirdParty/meshoptimizer/")
endif()
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/nfd/CMakeLists.txt")
	message(FATAL_ERROR "nfd submodule missing — run: git submodule update --init --recursive")
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

# meshoptimizer: vendored split-source layout (mirrors the upstream
# "src/" dir; .cpp files are split per algorithm). MIT licensed.
# Built as a static library so the C API gets a proper CMake target
# and the wrapper in engine/Fury/MeshSimplifier.cpp links via
# target_link_libraries.
file(GLOB MESHO_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/*.cpp)
add_library(meshoptimizer STATIC ${MESHO_SRC})
target_include_directories(meshoptimizer PUBLIC ${PROJECT_SOURCE_DIR}/ThirdParty/meshoptimizer/src)
set_property(TARGET meshoptimizer PROPERTY POSITION_INDEPENDENT_CODE ON)

include_directories(${PROJECT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/rapidjson/include)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/STB)

file(GLOB IMGUI_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/ImGui/*.cpp)
# Backends live in their own subdirectory so we glob them separately;
# both backends (OpenGL3 + the fury-3d-bespoke SFML3) are required whenever
# GUI_IMP is on. Upstream backend headers `#include "imgui.h"` directly,
# so the ImGui core directory must be in the search path.
include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/ImGui)
file(GLOB IMGUI_BACKEND_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/ImGui/backends/*.cpp)
list(APPEND IMGUI_SRC ${IMGUI_BACKEND_SRC})

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/LZ4)
file(GLOB LZ4_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/LZ4/*.c)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/tinygltf)
set(TINYGLTF_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/tinygltf/tiny_gltf.cc)
set_source_files_properties(${TINYGLTF_SRC} PROPERTIES COMPILE_DEFINITIONS "TINYGLTF_NO_STB_IMAGE;TINYGLTF_NO_STB_IMAGE_WRITE")

# Editor-only include paths. Applied globally so the editor sources
# in FURY_EDITOR_SRC resolve their headers, but only furye consumes
# that source list — fury never sees these symbols.
include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/ImReflect)
include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/ImGuizmo)

if(OS_MACOSX)
	find_library(COREFOUNDATION_LIB CoreFoundation)
	find_package(OpenGL REQUIRED)
	include_directories(${OPENGL_INCLUDE_DIR})
endif()

# Engine source split: FURY_CORE_SRC builds for both binaries; FURY_EDITOR_SRC
# only feeds furye. The Editor/*.cpp files are themselves wrapped in
# `#ifdef WITH_EDITOR`, so even if they were accidentally compiled into fury
# they would compile to empty TUs — but splitting them out keeps the build
# graph honest and the symbol surface of fury small.
file(GLOB FURY_CORE_SRC ${PROJECT_SOURCE_DIR}/Fury/*.cpp)
file(GLOB FURY_EDITOR_SRC ${PROJECT_SOURCE_DIR}/Fury/Editor/*.cpp)
list(APPEND FURY_EDITOR_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/ImGuizmo/ImGuizmo.cpp)

# nfd (nativefiledialog-extended) is integrated as a static library
# via add_subdirectory() — the SFML precedent. We force nfd's test /
# install options OFF so the editor build doesn't generate spurious
# targets or install rules. nfd's own CMake handles per-platform
# source selection (nfd_cocoa.m on macOS, nfd_win.cpp on Windows).
# Linux paths (GTK/portal/wayland) are inert here because fury3d
# doesn't build on Linux.
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_SDL2_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_GLFW3_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/nfd)

# Common path: the engine IS the executable. The launcher source lives in
# examples/main.cpp so demo authors can read it.
set(FURY_LAUNCHER_SRC "${PROJECT_SOURCE_DIR}/../examples/main.cpp")
if(NOT EXISTS "${FURY_LAUNCHER_SRC}")
	message(FATAL_ERROR "Launcher source not found at ${FURY_LAUNCHER_SRC} — examples/main.cpp must exist alongside engine/.")
endif()

set(FURY_COMMON_SRC ${FURY_CORE_SRC} ${IMGUI_SRC} ${LZ4_SRC} ${TINYGLTF_SRC})

if(OS_WINDOWS)
	set(FURY_PLATFORM_LIBS SFML::Window SFML::System opengl32 lua meshoptimizer)
elseif(OS_MACOSX)
	set(FURY_PLATFORM_LIBS SFML::Window SFML::System ${OPENGL_LIBRARIES} ${COREFOUNDATION_LIB} lua meshoptimizer)
endif()

# Path to the host-platform FBX2glTF binary copied next to each executable.
if(OS_MACOSX)
	set(FBX2GLTF_BINARY "FBX2glTF-darwin-x64")
elseif(OS_WINDOWS)
	set(FBX2GLTF_BINARY "FBX2glTF-windows-x64.exe")
else()
	set(FBX2GLTF_BINARY "FBX2glTF-linux-x64")
endif()
set(FBX2GLTF_SRC_BINARY "${PROJECT_SOURCE_DIR}/ThirdParty/FBX2glTF/${FBX2GLTF_BINARY}")

# Helper: configure a fury* target with the standard post-build steps
# (output into examples/, copy FBX2glTF, regenerate docs/LUA_API.md).
# Avoids repeating the output / FBX2glTF copy stanza for each variant.
function(fury_add_target NAME)
	add_executable(${NAME} ${ARGN} ${FURY_LAUNCHER_SRC})
	target_link_libraries(${NAME} PRIVATE ${FURY_PLATFORM_LIBS})
	set_target_properties(${NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/../examples")
	if(EXISTS "${FBX2GLTF_SRC_BINARY}")
		add_custom_command(TARGET ${NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${FBX2GLTF_SRC_BINARY}"
				"$<TARGET_FILE_DIR:${NAME}>/${FBX2GLTF_BINARY}"
			COMMENT "Copying FBX2glTF binary next to ${NAME}"
			VERBATIM)
		if(NOT OS_WINDOWS)
			add_custom_command(TARGET ${NAME} POST_BUILD
				COMMAND chmod +x "$<TARGET_FILE_DIR:${NAME}>/${FBX2GLTF_BINARY}"
				VERBATIM)
		endif()
	endif()
	# Regenerate docs/LUA_API.md whenever LuaBindings.cpp changes. The
	# docgen is a hand-rolled Python scanner (engine/Tools/lua_api_docgen.py)
	# — deterministic output, so re-running on the same input produces
	# byte-identical docs/LUA_API.md. The script intentionally never blocks
	# the build on parse errors; unparseable bindings emit a marker.
	add_custom_command(TARGET ${NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E env
			python3 "${PROJECT_SOURCE_DIR}/Tools/lua_api_docgen.py"
				--src "${PROJECT_SOURCE_DIR}/Fury/LuaBindings.cpp"
				--out "${PROJECT_SOURCE_DIR}/../docs/LUA_API.md"
		COMMENT "Regenerating docs/LUA_API.md from LuaBindings.cpp"
		VERBATIM)
endfunction()

# Output the fury / furye binaries directly into examples/ so launching is
# just `cd examples && ./fury` (or `./furye`). Resource/ lives at
# examples/Resource/, alongside Editor.lua and the host-platform FBX2glTF
# binary copied below; nothing has to be moved into a sub-bin/ directory at
# run time.

if(BUILD_SHARED_LIBS)
	# Embedder path: produce a shared libfury (no main). The embedder picks
	# the feature set via WITH_EDITOR_API; the static-binary path below
	# always builds both fury and furye.
	if(WITH_EDITOR_API)
		add_library(furye SHARED ${FURY_CORE_SRC} ${FURY_EDITOR_SRC} ${IMGUI_SRC} ${LZ4_SRC} ${TINYGLTF_SRC})
		target_compile_definitions(furye PRIVATE WITH_EDITOR)
		target_link_libraries(furye PUBLIC ${FURY_PLATFORM_LIBS} nfd::nfd)
		set_target_properties(furye PROPERTIES
			BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
		install(TARGETS furye DESTINATION lib)
	else()
		add_library(fury SHARED ${FURY_CORE_SRC} ${IMGUI_SRC} ${LZ4_SRC} ${TINYGLTF_SRC})
		target_link_libraries(fury PUBLIC ${FURY_PLATFORM_LIBS})
		set_target_properties(fury PROPERTIES
			BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
		install(TARGETS fury DESTINATION lib)
	endif()
else()
	fury_add_target(fury ${FURY_COMMON_SRC})
	install(TARGETS fury DESTINATION bin)

	fury_add_target(furye ${FURY_COMMON_SRC} ${FURY_EDITOR_SRC})
	target_compile_definitions(furye PRIVATE WITH_EDITOR)
	target_link_libraries(furye PRIVATE nfd::nfd)
	install(TARGETS furye DESTINATION bin)
endif()