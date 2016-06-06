#ifndef _FURY_MACROS_H_
#define _FURY_MACROS_H_

#include <iostream>

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