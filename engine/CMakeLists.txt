cmake_minimum_required(VERSION 3.0)

project(FURY3D)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(OS_WINDOWS 1)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(OS_MACOSX 1)
else()
	message(SEND_ERROR "Only support windows && osx currentlly.")
endif()

set(STB_INCLUDE "/usr/local/include" CACHE PATH "Path to stb_library headers.")

set(RAPIDJSON_INCLUDE "/usr/local/include" CACHE PATH "Path to rapidjson headers.")

set(SFML_INCLUDE "/usr/local/include" CACHE PATH "Location of SFML headers.")
set(SFML_LIB "/usr/local/lib" CACHE PATH "Location of SFML lib.")

option(FBXPARSER_IMP "Build FbxParser.")
if(FBXPARSER_IMP)
	add_definitions(-D_FURY_FBXPARSER_IMP)
	set(FBXSDK_INCLUDE "/usr/local/include" CACHE PATH "Path to fbxsdk headers.")
	set(FBXSDK_LIB_RELEASE "/usr/local/lib" CACHE FILEPATH "Path to fbxsdk release lib.")
	set(FBXSDK_LIB_DEBUG "/usr/local/lib" CACHE FILEPATH "Path to fbxsdk debug lib.")
endif()

set(FBXSDK_LIB optimized ${FBXSDK_LIB_RELEASE} debug ${FBXSDK_LIB_DEBUG})

set(CMAKE_CXX_FLAGS "-std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -O2 -NDEBUG")

option(BUILD_SHARED_LIBS "Build shared librarie." ON)

if(OS_WINDOWS)
	option(EXPORT_DLL "Define FURY_API_EXPORT" ON)
	if(EXPORT_DLL)
		add_definitions(-DFURY_API_EXPORT)
	endif()
endif()

add_subdirectory(src)