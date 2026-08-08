#ifndef _FURY_MACROS_H_
#define _FURY_MACROS_H_

#include <iostream>

// ----------------------------------------------------------------------------
// Platform macros (UnrealEngine-style). Prefer these over raw `defined(_WIN32)`
// / `__APPLE__` / `__linux__` checks; the platform list is the single source
// of truth and is easy to extend for new targets (iOS, Android, Emscripten).
// ----------------------------------------------------------------------------
#if defined(_WIN32)
	#define PLATFORM_WINDOWS 1
	#define PLATFORM_DESKTOP 1
#elif defined(__APPLE__)
	#include <TargetConditionals.h>
	#if TARGET_OS_IPHONE
		#define PLATFORM_IOS 1
		#define PLATFORM_MOBILE 1
	#else
		#define PLATFORM_MACOS 1
		#define PLATFORM_DESKTOP 1
	#endif
#elif defined(__ANDROID__)
	#define PLATFORM_ANDROID 1
	#define PLATFORM_MOBILE 1
#elif defined(__linux__)
	#define PLATFORM_LINUX 1
	#define PLATFORM_DESKTOP 1
#else
	#error "Unsupported platform -- add a PLATFORM_* macro for this target in Macros.h"
#endif

// Provide a 0 default for any platform macro that was not defined above. This
// lets `#if PLATFORM_WINDOWS` work without a separate `#if defined(...)`.
#ifndef PLATFORM_WINDOWS
	#define PLATFORM_WINDOWS 0
#endif
#ifndef PLATFORM_MACOS
	#define PLATFORM_MACOS 0
#endif
#ifndef PLATFORM_IOS
	#define PLATFORM_IOS 0
#endif
#ifndef PLATFORM_ANDROID
	#define PLATFORM_ANDROID 0
#endif
#ifndef PLATFORM_LINUX
	#define PLATFORM_LINUX 0
#endif
#ifndef PLATFORM_DESKTOP
	#define PLATFORM_DESKTOP 0
#endif
#ifndef PLATFORM_MOBILE
	#define PLATFORM_MOBILE 0
#endif

#if defined(_WIN32)

	#ifdef FURY_API_EXPORT
		#define FURY_API __declspec(dllexport)
	#else
		#define FURY_API __declspec(dllimport)
	#endif

	// For Visual C++ compilers, we also need to turn off this annoying C4251 warning
	#ifdef _MSC_VER
		#pragma warning(disable: 4251)
	#endif

#else

	#define FURY_API

#endif

#ifndef NDEBUG
#define ASSERT_MSG(condition, message) \
do { \
	if (! (condition)) { \
		std::cerr << "Assertion failed: (" #condition "), function " << __FUNCTION__ \
			<< ", file " << __FILE__ << ", line " << __LINE__ << ": " << message << std::endl; \
		std::abort(); \
			} \
} while (false)
#else
#define ASSERT_MSG(condition, message) \
do {} while (false)
#endif

#define FURY_MIPMAP_LEVEL 5

#endif // _FURY_MACROS_H_