cmake_minimum_required(VERSION 3.22)

# Enable OBJC for nfd's nfd_cocoa.m on macOS (gated behind add_subdirectory
# under WITH_EDITOR; the language is harmless on platforms that don't use it
# since CMake no-ops the toolchain when no .m sources are present).
if(APPLE)
	project(FURY3D LANGUAGES C CXX OBJC OBJCXX)
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

# Regenerate docs/LUA_API.md from LuaBindings.cpp at build time. Off by
# default — most builds don't need to refresh the docs; only enable when
# shipping a release or preparing a docs PR. Pass -DFURY_DOCGEN=ON to
# enable.
option(FURY_DOCGEN "Regenerate docs/LUA_API.md from LuaBindings.cpp at build time." OFF)

# Tracy profiler: ON by default for dev/editor configs (Debug,
# RelWithDebInfo, and multi-config generators where the editor lives),
# OFF by default for shipping configs (Release/MinSizeRel). Explicit
# -DFURY_WITH_TRACY=ON/OFF always wins.
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
	set(_FURY_TRACY_DEFAULT OFF)
else()
	set(_FURY_TRACY_DEFAULT ON)
endif()
option(FURY_WITH_TRACY "Enable Tracy profiler instrumentation (ThirdParty/tracy submodule)." ${_FURY_TRACY_DEFAULT})
option(FURY_WITH_TRACY_GPU "Enable Tracy OpenGL GPU zones (requires FURY_WITH_TRACY; unreliable on Apple/TBDR drivers)." ON)

# Two binaries from one configure:
#   fury   — headless runtime, no editor shell (no ImReflect/ImGuizmo/nfd, no WITH_EDITOR)
#   furye  — editor shell + runtime (ImReflect/ImGuizmo/nfd linked, WITH_EDITOR defined)
# Both targets share the engine core; only the editor sources / defines differ.
# This replaces the old WITH_EDITOR=ON/OFF + two-separate-build-dirs flow.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -Wno-int-to-void-pointer-cast is a GCC/Clang warning flag — MSVC rejects
# unknown /W options outright (D8021). Only forward it when the active
# toolchain actually understands it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-int-to-void-pointer-cast")
endif()

# MSVC's /W options are spelled differently from GCC's -W; -Wall is silently
# accepted as "enable all warnings" only on GCC/Clang. On MSVC, the project's
# /W4 default is already strong enough for our purposes.
#
# Also pass /bigobj: sol2's heavy template instantiations (used in
# LuaBindings.cpp) blow past MSVC's 2^16 section limit (~64K symbols
# in a single .obj) and trigger C1128. /bigobj lifts the cap to 2^32.
# /utf-8 sets the execution character set so literal UTF-8 paths don't
# need a wchar conversion layer on Windows.
#
# _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS hides rapidjson's std::iterator
# deprecation noise (vendored third-party; not our code).
if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /utf-8 /bigobj /D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS /D_CRT_SECURE_NO_WARNINGS /D_SCL_SECURE_NO_WARNINGS /D_USE_MATH_DEFINES /DWIN32_LEAN_AND_MEAN /DNOMINMAX")
	# /external:W0 silences vendored-header warnings (MSVC 16.10+); /wd4127
	# + /wd4245 demote two warnings that leak from vendored .cpp files
	# compiled directly (ImGuizmo, imgui_impl_opengl3 backends) — neither
	# fires in engine code.
	if(MSVC_VERSION GREATER_EQUAL 1929)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /external:W0 /wd4127 /wd4245")
	endif()
else()
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -O2 -DNDEBUG")
endif()

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

# Shipping strip: -DFURY_SHIPPING=ON defines FURY_BUILD_SHIPPING (alongside
# the per-config FURY_BUILD_DEBUG/PROFILE/RELEASE markers) and forces
# development-only features off. Visual debug overlays (LOD tier tint now;
# bounds/buoyancy overlays move here later) live behind WITH_DBG_OVERLAY so
# they exist in every flavor except Shipping.
option(FURY_SHIPPING "Strip development-only features (defines FURY_BUILD_SHIPPING)" OFF)
option(FURY_WITH_DBG_OVERLAY "Compile visual debug overlays (LOD tier tint, ...)" ON)
if(FURY_SHIPPING AND FURY_WITH_DBG_OVERLAY)
	message(STATUS "FURY_SHIPPING=ON: forcing FURY_WITH_DBG_OVERLAY=OFF")
	set(FURY_WITH_DBG_OVERLAY OFF)
endif()

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
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/JoltPhysics/Jolt/Jolt.h")
	message(FATAL_ERROR "JoltPhysics submodule missing - run: git submodule update --init --recursive")
endif()
if(FURY_WITH_TRACY AND NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/tracy/public/TracyClient.cpp")
	message(FATAL_ERROR "tracy submodule missing - run: git submodule update --init --recursive (or configure with -DFURY_WITH_TRACY=OFF)")
endif()

# Kraut-CLI tree toolchain (add-kraut-vegetation). Vendored submodule built
# via ExternalProject: Kraut's CMake is a standalone superbuild (own Output/
# layout, FetchContent for SDL2/ImGui/glew) that would drag its targets and
# options into our graph via add_subdirectory. Only KrautCLI (+ KrautPreview
# when enabled) are built -- the Qt editor stays off. Binaries are copied
# next to fury/furye POST_BUILD (FBX2glTF pattern) by CopyKrautTools.cmake.
option(FURY_WITH_KRAUT "Build KrautCLI + KrautPreview tools (ThirdParty/Kraut submodule)." ON)
option(FURY_WITH_KRAUT_PREVIEW "Build KrautPreview (fetches SDL2 at configure time; off = KrautCLI only)." ON)

if(FURY_WITH_KRAUT)
	if(NOT EXISTS "${PROJECT_SOURCE_DIR}/ThirdParty/Kraut/CMakeLists.txt")
		message(FATAL_ERROR "Kraut submodule missing - run: git submodule update --init --recursive (or configure with -DFURY_WITH_KRAUT=OFF)")
	endif()

	include(ExternalProject)
	if(FURY_WITH_KRAUT_PREVIEW)
		set(FURY_KRAUT_PREVIEW_OPT ON)
	else()
		set(FURY_KRAUT_PREVIEW_OPT OFF)
	endif()
	# Forward the macOS arch/target settings only when set (empty values
	# would break the subbuild).
	set(FURY_KRAUT_OSX_ARGS "")
	if(CMAKE_OSX_ARCHITECTURES)
		list(APPEND FURY_KRAUT_OSX_ARGS -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES})
	endif()
	if(CMAKE_OSX_DEPLOYMENT_TARGET)
		list(APPEND FURY_KRAUT_OSX_ARGS -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
	endif()
	# POLICY_VERSION_MINIMUM: Kraut's vendored FetchContent deps (SDL2 2.30.5)
	# predate CMake 3.5+ policy requirements.
	# BUILD_ALWAYS: the submodule's content changes when the pin moves (git
	# checkout), which stamp-based staleness can't see; the subbuild is
	# incremental, so the re-invocation is cheap when nothing changed.
	ExternalProject_Add(kraut_tools
		SOURCE_DIR "${PROJECT_SOURCE_DIR}/ThirdParty/Kraut"
		BINARY_DIR "${CMAKE_BINARY_DIR}/kraut"
		CMAKE_ARGS
			-DCMAKE_BUILD_TYPE=Release
			${FURY_KRAUT_OSX_ARGS}
			-DKRAUT_BUILD_EDITOR=OFF
			-DKRAUT_BUILD_PREVIEW=${FURY_KRAUT_PREVIEW_OPT}
			-DCMAKE_POLICY_VERSION_MINIMUM=3.5
		BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
		UPDATE_COMMAND ""
		BUILD_ALWAYS ON
		INSTALL_COMMAND ""
	)
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
target_include_directories(lua SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/ThirdParty/lua)
target_compile_definitions(lua PRIVATE
	$<$<PLATFORM_ID:Darwin>:LUA_USE_MACOSX>
	$<$<PLATFORM_ID:Linux>:LUA_USE_LINUX>)
set_property(TARGET lua PROPERTY POSITION_INDEPENDENT_CODE ON)

# sol2 is header-only.
include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty/sol2/include)

# meshoptimizer: vendored split-source layout (mirrors the upstream
# "src/" dir; .cpp files are split per algorithm). MIT licensed.
# Built as a static library so the C API gets a proper CMake target
# and the wrapper in engine/Fury/MeshSimplifier.cpp links via
# target_link_libraries.
file(GLOB MESHO_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/*.cpp)
add_library(meshoptimizer STATIC ${MESHO_SRC})
target_include_directories(meshoptimizer SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/ThirdParty/meshoptimizer/src)
set_property(TARGET meshoptimizer PROPERTY POSITION_INDEPENDENT_CODE ON)

# Tracy profiler client: single-TU build; TRACY_ENABLE/TRACY_ON_DEMAND are
# PUBLIC so linking `tracy` arms the Profiler.h macros in engine sources.
if(FURY_WITH_TRACY)
	add_library(tracy STATIC ${PROJECT_SOURCE_DIR}/ThirdParty/tracy/public/TracyClient.cpp)
	target_include_directories(tracy SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/ThirdParty/tracy/public)
	target_compile_definitions(tracy PUBLIC TRACY_ENABLE TRACY_ON_DEMAND)
	set_property(TARGET tracy PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

# JoltPhysics: vendored submodule (v5.6.0), built via its own Build/CMakeLists.txt
# (the SFML/nfd add_subdirectory precedent). When embedded like this, Jolt's
# cmake auto-disables its unit tests / samples / viewer targets (guarded by
# CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR) - only the Jolt static
# lib is produced. ENABLE_ALL_WARNINGS OFF keeps Jolt's warnings-as-errors
# away from our toolchain; FLOATING_POINT_EXCEPTIONS OFF because we embed Jolt
# in a process that also runs GL/ImGui code; ENABLE_INSTALL OFF keeps our
# install() surface unchanged. The Jolt target exports its include dir +
# config defines (JPH_*) PUBLIC, so consumers just link `Jolt`.
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
# Jolt defaults its MSVC runtime library to static (/MT) via the
# USE_STATIC_MSVC_RUNTIME_LIBRARY option. The rest of fury3d links against
# the dynamic CRT (/MD); mixing static-CRT Jolt into our MDd objects
# produces LNK2038 /RuntimeLibrary mismatches and LNK2005 duplicate
# symbol errors at link time (MSVCP140D is already linked from the rest
# of the project). Disable Jolt's static CRT so it inherits /MD.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/ThirdParty/JoltPhysics/Build)

# The engine's own headers stay a normal -I path: we WANT -Wall to apply to
# them. Every vendored ThirdParty path below is marked SYSTEM (-isystem on
# GCC/Clang) so warnings raised inside vendored headers are suppressed without
# touching the vendored sources or weakening -Wall for engine code. This is
# what silences rapidjson's std::iterator deprecations, stb_image.h's unused
# static, and ImReflect's unused type alias.
include_directories(${PROJECT_SOURCE_DIR})

include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty/rapidjson/include)

include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty)

include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty/STB)

file(GLOB IMGUI_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/ImGui/*.cpp)
# Backends live in their own subdirectory so we glob them separately;
# both backends (OpenGL3 + the fury-3d-bespoke SFML3) are required whenever
# GUI_IMP is on. Upstream backend headers `#include "imgui.h"` directly,
# so the ImGui core directory must be in the search path.
include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty/ImGui)
file(GLOB IMGUI_BACKEND_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/ImGui/backends/*.cpp)
list(APPEND IMGUI_SRC ${IMGUI_BACKEND_SRC})

include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty/LZ4)
file(GLOB LZ4_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/LZ4/*.c)

include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty/tinygltf)
set(TINYGLTF_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/tinygltf/tiny_gltf.cc)
set_source_files_properties(${TINYGLTF_SRC} PROPERTIES COMPILE_DEFINITIONS "TINYGLTF_NO_STB_IMAGE;TINYGLTF_NO_STB_IMAGE_WRITE")

# Editor-only include paths. Applied globally so the editor sources
# in FURY_EDITOR_SRC resolve their headers, but only furye consumes
# that source list — fury never sees these symbols.
include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty/ImReflect)
include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/ThirdParty/ImGuizmo)

if(OS_MACOSX)
	find_library(COREFOUNDATION_LIB CoreFoundation)
	find_package(OpenGL REQUIRED)
	include_directories(SYSTEM ${OPENGL_INCLUDE_DIR})
endif()

# Engine source split: FURY_CORE_SRC builds for both binaries; FURY_EDITOR_SRC
# only feeds furye. The Editor/*.cpp files are themselves wrapped in
# `#if WITH_EDITOR`, so even if they were accidentally compiled into fury
# they would compile to empty TUs — but splitting them out keeps the build
# graph honest and the symbol surface of fury small. Sources are listed
# explicitly (not globbed) so adding/removing a file is a deliberate change.
set(FURY_CORE_SRC
	${PROJECT_SOURCE_DIR}/Fury/AnimationClip.cpp
	${PROJECT_SOURCE_DIR}/Fury/AnimationPlayer.cpp
	${PROJECT_SOURCE_DIR}/Fury/AnimationState.cpp
	${PROJECT_SOURCE_DIR}/Fury/AnimationUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/ArrayBuffers.cpp
	${PROJECT_SOURCE_DIR}/Fury/BodySetup.cpp
	${PROJECT_SOURCE_DIR}/Fury/BoxBounds.cpp
	${PROJECT_SOURCE_DIR}/Fury/Buffer.cpp
	${PROJECT_SOURCE_DIR}/Fury/BufferManager.cpp
	${PROJECT_SOURCE_DIR}/Fury/Camera.cpp
	${PROJECT_SOURCE_DIR}/Fury/CharacterController.cpp
	${PROJECT_SOURCE_DIR}/Fury/Cli.cpp
	${PROJECT_SOURCE_DIR}/Fury/Color.cpp
	${PROJECT_SOURCE_DIR}/Fury/Component.cpp
	${PROJECT_SOURCE_DIR}/Fury/Engine.cpp
	${PROJECT_SOURCE_DIR}/Fury/Entity.cpp
	${PROJECT_SOURCE_DIR}/Fury/EnumUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/FbxConverter.cpp
	${PROJECT_SOURCE_DIR}/Fury/FileUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/Frustum.cpp
	${PROJECT_SOURCE_DIR}/Fury/GLLoader.cpp
	${PROJECT_SOURCE_DIR}/Fury/GltfImporter.cpp
	${PROJECT_SOURCE_DIR}/Fury/Gui.cpp
	${PROJECT_SOURCE_DIR}/Fury/InputUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/InstancedMeshRender.cpp
	${PROJECT_SOURCE_DIR}/Fury/InstancedMeshStreamer.cpp
	${PROJECT_SOURCE_DIR}/Fury/Joint.cpp
	${PROJECT_SOURCE_DIR}/Fury/KrautConverter.cpp
	${PROJECT_SOURCE_DIR}/Fury/Light.cpp
	${PROJECT_SOURCE_DIR}/Fury/LuaBindings.cpp
	${PROJECT_SOURCE_DIR}/Fury/Material.cpp
	${PROJECT_SOURCE_DIR}/Fury/MathUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/Matrix4.cpp
	${PROJECT_SOURCE_DIR}/Fury/Mesh.cpp
	${PROJECT_SOURCE_DIR}/Fury/MeshRender.cpp
	${PROJECT_SOURCE_DIR}/Fury/MeshSimplifier.cpp
	${PROJECT_SOURCE_DIR}/Fury/MeshUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/OcTree.cpp
	${PROJECT_SOURCE_DIR}/Fury/OcTreeNode.cpp
	${PROJECT_SOURCE_DIR}/Fury/Pass.cpp
	${PROJECT_SOURCE_DIR}/Fury/ParticleModules.cpp
	${PROJECT_SOURCE_DIR}/Fury/ParticleRenderer.cpp
	${PROJECT_SOURCE_DIR}/Fury/ParticleSystem.cpp
	${PROJECT_SOURCE_DIR}/Fury/PhysicsWorld.cpp
	${PROJECT_SOURCE_DIR}/Fury/Pipeline.cpp
	${PROJECT_SOURCE_DIR}/Fury/Plane.cpp
	${PROJECT_SOURCE_DIR}/Fury/PlayerController.cpp
	${PROJECT_SOURCE_DIR}/Fury/PostProcessEffect.cpp
	${PROJECT_SOURCE_DIR}/Fury/PostProcessRegistry.cpp
	${PROJECT_SOURCE_DIR}/Fury/PrelightPipeline.cpp
	${PROJECT_SOURCE_DIR}/Fury/Quaternion.cpp
	${PROJECT_SOURCE_DIR}/Fury/RenderQuery.cpp
	${PROJECT_SOURCE_DIR}/Fury/RenderSettings.cpp
	${PROJECT_SOURCE_DIR}/Fury/RenderTarget.cpp
	${PROJECT_SOURCE_DIR}/Fury/RenderUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/SkyAtmosphere.cpp
	${PROJECT_SOURCE_DIR}/Fury/Heightmap.cpp
	${PROJECT_SOURCE_DIR}/Fury/OceanComponent.cpp
	${PROJECT_SOURCE_DIR}/Fury/OceanWaves.cpp
	${PROJECT_SOURCE_DIR}/Fury/BuoyancyComponent.cpp
	${PROJECT_SOURCE_DIR}/Fury/Terrain.cpp
	${PROJECT_SOURCE_DIR}/Fury/Scene.cpp
	${PROJECT_SOURCE_DIR}/Fury/SceneNode.cpp
	${PROJECT_SOURCE_DIR}/Fury/Serializable.cpp
	${PROJECT_SOURCE_DIR}/Fury/Shader.cpp
	${PROJECT_SOURCE_DIR}/Fury/SphereBounds.cpp
	${PROJECT_SOURCE_DIR}/Fury/Texture.cpp
	${PROJECT_SOURCE_DIR}/Fury/ThreadUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/Transform.cpp
	${PROJECT_SOURCE_DIR}/Fury/Uniform.cpp
	${PROJECT_SOURCE_DIR}/Fury/Vector4.cpp
	${PROJECT_SOURCE_DIR}/Fury/WaveSampler.cpp)

set(FURY_EDITOR_SRC
	${PROJECT_SOURCE_DIR}/Fury/Editor/Editor.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/Editor3DPreview.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorAnimationWindow.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorAssetPicker.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorAssetWindows.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorBodySetupWindow.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorConfirmDialog.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorGizmo.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorLog.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorNodeProperties.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorParticleWindow.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorPicking.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorSelectionViz.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorSkyWindow.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorTerrainWindow.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorThemes.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorWindows.cpp
	${PROJECT_SOURCE_DIR}/ThirdParty/ImGuizmo/ImGuizmo.cpp
	${PROJECT_SOURCE_DIR}/ThirdParty/ImGuizmo/ImSequencer.cpp
	${PROJECT_SOURCE_DIR}/ThirdParty/ImGuizmo/ImCurveEdit.cpp)

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

# macOS-only DPI shim. The file is Objective-C++ and the project's
# project() call already enables the OBJC language on APPLE (see top of
# this file). The shim is the only consumer of <AppKit/AppKit.h> and is
# gated in C++ by #if PLATFORM_MACOS so non-Apple builds that happen to
# pick up the file would be no-ops. See platform-window-dpi spec.
if(OS_MACOSX)
	list(APPEND FURY_COMMON_SRC "${PROJECT_SOURCE_DIR}/Fury/Engine_dpi_mac.mm")
endif()

# Windows-only V2 DPI awareness manifest. Linker flag form (`LINK_FLAGS`)
# is portable across MSVC, MinGW, and Clang. The manifest file is in
# engine/Resources/ and is shipped with the source tree. See
# platform-window-dpi spec.
if(OS_WINDOWS)
	set(_FURY_MANIFEST "${PROJECT_SOURCE_DIR}/Resources/fury.exe.manifest")
	set(_FURY_MANIFEST_FLAG "/MANIFESTINPUT:${_FURY_MANIFEST}")
endif()

if(OS_WINDOWS)
	# ws2_32 (FileUtil ntohl/htonl), comctl32 (SetWindowSubclass).
	set(FURY_PLATFORM_LIBS SFML::Window SFML::System opengl32 ws2_32 comctl32 lua meshoptimizer Jolt)
elseif(OS_MACOSX)
	set(FURY_PLATFORM_LIBS SFML::Window SFML::System ${OPENGL_LIBRARIES} ${COREFOUNDATION_LIB} lua meshoptimizer Jolt)
endif()
if(FURY_WITH_TRACY)
	# Tracy needs winsock2 (ws2_32) and dial-up box (dbghelp) on Windows.
	if(OS_WINDOWS)
		list(APPEND FURY_PLATFORM_LIBS tracy dbghelp)
	else()
		list(APPEND FURY_PLATFORM_LIBS tracy pthread dl)
	endif()
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
	# Build-config marker for gating debug-only code (pipeline traces,
	# verification probes): FURY_BUILD_DEBUG / FURY_BUILD_PROFILE /
	# FURY_BUILD_RELEASE — exactly one per config.
	target_compile_definitions(${NAME} PRIVATE
		$<$<CONFIG:Debug>:FURY_BUILD_DEBUG>
		$<$<CONFIG:RelWithDebInfo>:FURY_BUILD_PROFILE>
		$<$<CONFIG:Release>:FURY_BUILD_RELEASE>)
	# Shipping strip + debug-overlay gate (see the options above).
	target_compile_definitions(${NAME} PRIVATE
		$<$<BOOL:${FURY_SHIPPING}>:FURY_BUILD_SHIPPING>
		WITH_DBG_OVERLAY=$<BOOL:${FURY_WITH_DBG_OVERLAY}>)
	if(FURY_WITH_TRACY AND FURY_WITH_TRACY_GPU)
		target_compile_definitions(${NAME} PRIVATE FURY_TRACY_GPU)
	endif()
	set_target_properties(${NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/../examples"
		# Multi-Config generators (Visual Studio / Xcode / Ninja Multi-Config)
		# append $(Configuration) to RUNTIME_OUTPUT_DIRECTORY by default,
		# dropping binaries into examples/Debug/, examples/Release/, etc.
		# Single-config generators (Ninja, Unix Makefiles) ignore these
		# per-config overrides -- harmless there. Forcing the same path
		# across every config keeps `cd examples && ./furye Editor.lua`
		# working regardless of which generator built the engine.
		RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${PROJECT_SOURCE_DIR}/../examples"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${PROJECT_SOURCE_DIR}/../examples"
		RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${PROJECT_SOURCE_DIR}/../examples"
		RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${PROJECT_SOURCE_DIR}/../examples")
	# Windows-only V2 DPI awareness manifest (see platform-window-dpi
	# spec). The manifest linker flag is defined at the top of the
	# static-binary section (gated by if(OS_WINDOWS)). We also disable
	# the default GenerateManifest via VS_GLOBAL_GenerateManifest
	# because the MSVC linker would otherwise generate a stock manifest
	# with no <dpiAwareness> and win over our /MANIFESTINPUT.
	# <dpiAwareness> element and win over our /MANIFESTINPUT. See
	# platform-window-dpi spec.
	if(OS_WINDOWS)
		set_property(TARGET ${NAME} APPEND_STRING PROPERTY
			LINK_FLAGS " ${_FURY_MANIFEST_FLAG}")
		set_target_properties(${NAME} PROPERTIES
			VS_GLOBAL_GenerateManifest "false")
	endif()
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
	# Kraut tree tools: the ExternalProject builds them on its own
	# schedule; the POST_BUILD copy picks whatever is newest under the
	# submodule's Output/Bin at link time (warns, not errors, when the
	# tools haven't been built yet -- `make kraut_tools` builds them).
	if(FURY_WITH_KRAUT)
		add_dependencies(${NAME} kraut_tools)
		add_custom_command(TARGET ${NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND}
				-DKRAUT_OUTPUT_DIR=${PROJECT_SOURCE_DIR}/ThirdParty/Kraut/Output/Bin
				-DDEST_DIR=$<TARGET_FILE_DIR:${NAME}>
				-DWITH_PREVIEW=${FURY_WITH_KRAUT_PREVIEW}
				-P "${PROJECT_SOURCE_DIR}/../tools/cmake/CopyKrautTools.cmake"
			COMMENT "Copying KrautCLI/KrautPreview next to ${NAME}"
			VERBATIM)
	endif()
	# Regenerate docs/LUA_API.md from LuaBindings.cpp. Off by default;
	# pass -DFURY_DOCGEN=ON to enable. When on, the post-build step
	# invokes engine/Tools/lua_api_docgen.py (deterministic, byte-identical
	# output for unchanged input) and rewrites docs/LUA_API.md. The step
	# is skipped with a status message when no Python interpreter is on
	# PATH, so the build still succeeds.
	if(FURY_DOCGEN)
		if(NOT COMMAND find_package OR 1)
			find_package(Python3 COMPONENTS Interpreter QUIET)
		endif()
		if(Python3_Interpreter_FOUND)
			set(_FURY_DOCGEN_PYTHON "${Python3_EXECUTABLE}")
		else()
			find_program(_FURY_DOCGEN_PYTHON
				NAMES python3 python py
				PATHS ENV PATH
				DOC "Python interpreter used by the docs/LUA_API.md docgen"
				NO_DEFAULT_PATH)
		endif()
		if(_FURY_DOCGEN_PYTHON)
			add_custom_command(TARGET ${NAME} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E env
					"${_FURY_DOCGEN_PYTHON}" "${PROJECT_SOURCE_DIR}/Tools/lua_api_docgen.py"
						--src "${PROJECT_SOURCE_DIR}/Fury/LuaBindings.cpp"
						--out "${PROJECT_SOURCE_DIR}/../docs/LUA_API.md"
				COMMENT "Regenerating docs/LUA_API.md from LuaBindings.cpp"
				VERBATIM)
		else()
			add_custom_command(TARGET ${NAME} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E echo
					"docs/LUA_API.md not regenerated: no python3 interpreter on PATH"
				COMMENT "Skipping docs/LUA_API.md regeneration (no python3 on PATH)"
				VERBATIM)
		endif()
	endif()
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
		target_compile_definitions(furye PRIVATE WITH_EDITOR=1
			$<$<BOOL:${FURY_SHIPPING}>:FURY_BUILD_SHIPPING>
			WITH_DBG_OVERLAY=$<BOOL:${FURY_WITH_DBG_OVERLAY}>)
		target_link_libraries(furye PUBLIC ${FURY_PLATFORM_LIBS} nfd::nfd)
		if(FURY_WITH_TRACY AND FURY_WITH_TRACY_GPU)
			target_compile_definitions(furye PRIVATE FURY_TRACY_GPU)
		endif()
		set_target_properties(furye PROPERTIES
			BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
		install(TARGETS furye DESTINATION lib)
	else()
		add_library(fury SHARED ${FURY_CORE_SRC} ${IMGUI_SRC} ${LZ4_SRC} ${TINYGLTF_SRC})
		target_compile_definitions(fury PRIVATE WITH_EDITOR=0
			$<$<BOOL:${FURY_SHIPPING}>:FURY_BUILD_SHIPPING>
			WITH_DBG_OVERLAY=$<BOOL:${FURY_WITH_DBG_OVERLAY}>)
		target_link_libraries(fury PUBLIC ${FURY_PLATFORM_LIBS})
		if(FURY_WITH_TRACY AND FURY_WITH_TRACY_GPU)
			target_compile_definitions(fury PRIVATE FURY_TRACY_GPU)
		endif()
		set_target_properties(fury PROPERTIES
			BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
		install(TARGETS fury DESTINATION lib)
	endif()
else()
	fury_add_target(fury ${FURY_COMMON_SRC})
	target_compile_definitions(fury PRIVATE WITH_EDITOR=0)
	install(TARGETS fury DESTINATION bin)

	fury_add_target(furye ${FURY_COMMON_SRC} ${FURY_EDITOR_SRC})
	target_compile_definitions(furye PRIVATE WITH_EDITOR=1)
	target_link_libraries(furye PRIVATE nfd::nfd)
	install(TARGETS furye DESTINATION bin)
endif()

# Tracy tools (profiler UI + headless capture CLI): built alongside the engine
# when Tracy is on; binaries land next to fury/furye in examples/. ExternalProject
# keeps tracy's own C++20 + CPM-fetched deps isolated from our C++17 engine build.
# Tools are a standalone ALL target: a full build includes them, but building just
# the fury/furye targets skips them.
if(FURY_WITH_TRACY AND NOT BUILD_SHARED_LIBS)
	include(ExternalProject)
	set(_TRACY_TOOLS_DIR ${CMAKE_BINARY_DIR}/tracy-tools)
	if(OS_WINDOWS)
		set(_TRACY_TOOL_SUFFIX ".exe")
	endif()
	# Multi-config generators put Release outputs in a Release/ subdir.
	if(CMAKE_CONFIGURATION_TYPES)
		set(_TRACY_TOOL_SUBDIR "Release/")
	endif()
	ExternalProject_Add(tracy_capture_ext
		PREFIX ${_TRACY_TOOLS_DIR}/capture
		SOURCE_DIR ${PROJECT_SOURCE_DIR}/ThirdParty/tracy/capture
		CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
		BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
		INSTALL_COMMAND "")
	ExternalProject_Add(tracy_profiler_ext
		PREFIX ${_TRACY_TOOLS_DIR}/profiler
		SOURCE_DIR ${PROJECT_SOURCE_DIR}/ThirdParty/tracy/profiler
		CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
		BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
		INSTALL_COMMAND "")
	add_custom_target(tracy_tools ALL
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${_TRACY_TOOLS_DIR}/capture/src/tracy_capture_ext-build/${_TRACY_TOOL_SUBDIR}tracy-capture${_TRACY_TOOL_SUFFIX}
			${PROJECT_SOURCE_DIR}/../examples/tracy-capture${_TRACY_TOOL_SUFFIX}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${_TRACY_TOOLS_DIR}/profiler/src/tracy_profiler_ext-build/${_TRACY_TOOL_SUBDIR}tracy-profiler${_TRACY_TOOL_SUFFIX}
			${PROJECT_SOURCE_DIR}/../examples/tracy-profiler${_TRACY_TOOL_SUFFIX}
		DEPENDS tracy_capture_ext tracy_profiler_ext
		COMMENT "Copying tracy-profiler / tracy-capture next to fury/furye"
		VERBATIM)
endif()