cmake_minimum_required(VERSION 3.0)

project(FURY3D)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(OS_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(OS_MACOSX 1)
else()
	message(SEND_ERROR "Only support windows && osx currentlly.")
endif()

set(RAPIDJSON_INCLUDE "/usr/local/include" CACHE PATH "Path to rapidjson headers.")

set(SFML_INCLUDE "/usr/local/include" CACHE PATH "Location of SFML headers.")
set(SFML_LIB "/usr/local/lib" CACHE PATH "Location of SFML lib.")

option(FBXPARSER_IMP "Use FbxParser." OFF)
if(FBXPARSER_IMP)
	add_definitions(-D_FURY_FBXPARSER_IMP_)
	set(FBXSDK_INCLUDE "/usr/local/include" CACHE PATH "Path to fbxsdk headers.")
	set(FBXSDK_LIB_RELEASE "/usr/local/lib" CACHE FILEPATH "Path to fbxsdk release lib.")
	set(FBXSDK_LIB_DEBUG "/usr/local/lib" CACHE FILEPATH "Path to fbxsdk debug lib.")
	set(FBXSDK_LIB optimized ${FBXSDK_LIB_RELEASE} debug ${FBXSDK_LIB_DEBUG})
endif()

option(GUI_IMP "Use ImGui." ON)
if(GUI_IMP)
	add_definitions(-D_FURY_GUI_IMP_)
endif()

set(CMAKE_CXX_FLAGS "-std=c++11 -Wno-int-to-void-pointer-cast")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -O2 -NDEBUG")

option(BUILD_SHARED_LIBS "Build shared librarie." ON)

if(OS_WINDOWS)
	option(EXPORT_DLL "Define FURY_API_EXPORT" ON)
	if(EXPORT_DLL)
		add_definitions(-DFURY_API_EXPORT)
	endif()
endif()

# add_subdirectory(src)

include_directories(${PROJECT_SOURCE_DIR})

include_directories(${RAPIDJSON_INCLUDE})

include_directories(${SFML_INCLUDE})
link_directories(${SFML_LIB})

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/STB)

file(GLOB IMGUI_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/ImGui/*.cpp)

include_directories(${PROJECT_SOURCE_DIR}/ThirdParty/LZ4)
file(GLOB LZ4_SRC ${PROJECT_SOURCE_DIR}/ThirdParty/LZ4/*.c)

if(FBXPARSER_IMP)
	include_directories(${FBXSDK_INCLUDE})
endif()

if(OS_MACOSX)
	find_library(COREFOUNDATION_LIB CoreFoundation)
	find_package(OpenGL REQUIRED)
	include_directories(${OPENGL_INCLUDE_DIR})
endif()

file(GLOB FURY_SRC ${PROJECT_SOURCE_DIR}/Fury/*.cpp)
list(APPEND FURY_SRC ${IMGUI_SRC} ${LZ4_SRC})

if(BUILD_SHARED_LIBS)
	add_library(fury SHARED ${FURY_SRC})
	if(OS_WINDOWS)
		target_link_libraries(fury sfml-window sfml-system opengl32 ${FBXSDK_LIB})
	elseif(OS_MACOSX)
		target_link_libraries(fury sfml-window sfml-system ${OPENGL_LIBRARIES} ${COREFOUNDATION_LIB} ${FBXSDK_LIB})
		set_target_properties(fury PROPERTIES BUILD_WITH_INSTALL_RPATH 1 INSTALL_NAME_DIR "@executable_path")
	endif()
else()
	add_library(fury STATIC ${FURY_SRC})
endif()

install(TARGETS fury DESTINATION lib)