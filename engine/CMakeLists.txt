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
	${PROJECT_SOURCE_DIR}/Fury/Joint.cpp
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
	${PROJECT_SOURCE_DIR}/Fury/Scene.cpp
	${PROJECT_SOURCE_DIR}/Fury/SceneNode.cpp
	${PROJECT_SOURCE_DIR}/Fury/Serializable.cpp
	${PROJECT_SOURCE_DIR}/Fury/Shader.cpp
	${PROJECT_SOURCE_DIR}/Fury/SphereBounds.cpp
	${PROJECT_SOURCE_DIR}/Fury/Texture.cpp
	${PROJECT_SOURCE_DIR}/Fury/ThreadUtil.cpp
	${PROJECT_SOURCE_DIR}/Fury/Transform.cpp
	${PROJECT_SOURCE_DIR}/Fury/Uniform.cpp
	${PROJECT_SOURCE_DIR}/Fury/Vector4.cpp)

set(FURY_EDITOR_SRC
	${PROJECT_SOURCE_DIR}/Fury/Editor/Editor.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/Editor3DPreview.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorAnimationWindow.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorAssetPicker.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorAssetWindows.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorBodySetupWindow.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorConfirmDialog.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorDebug.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorGizmo.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorLog.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorNodeProperties.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorParticleWindow.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorPicking.cpp
	${PROJECT_SOURCE_DIR}/Fury/Editor/EditorSelectionViz.cpp
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
	set_target_properties(${NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/../examples")
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
		target_compile_definitions(furye PRIVATE WITH_EDITOR=1)
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
	target_compile_definitions(fury PRIVATE WITH_EDITOR=0)
	install(TARGETS fury DESTINATION bin)

	fury_add_target(furye ${FURY_COMMON_SRC} ${FURY_EDITOR_SRC})
	target_compile_definitions(furye PRIVATE WITH_EDITOR=1)
	target_link_libraries(furye PRIVATE nfd::nfd)
	install(TARGETS furye DESTINATION bin)
endif()