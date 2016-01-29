#ifndef _FURY_DEBUG_H_
#define _FURY_DEBUG_H_

#include <iostream>
#include <functional>

#include <plog/Log.h>

#ifndef NDEBUG
#define ASSERT_MSG(condition, message) \
do { \
	if (! (condition)) { \
		LOGE << "Assertion failed: (" #condition ") : " << message; \
		std::cerr << "Assertion failed: (" #condition "), function " << __FUNCTION__ \
			<< ", file " << __FILE__ << ", line " << __LINE__ << ": " << message << std::endl; \
		std::abort(); \
	} \
} while (false)
#else
#define ASSERT_MSG(condition, message) \
do {\
		LOGE << "Assertion failed: (" #condition ") : " << message; \
} while (false)
#endif

#endif // _FURY_DEBUG_H_