#ifndef _FURY_ANIMATION_UTIL_H_
#define _FURY_ANIMATION_UTIL_H_

#include <memory>

#include "Macros.h"

namespace fury
{
	class AnimationClip;

	class FURY_API AnimationUtil final
	{
	public:

		static void OptimizeAnimClip(const std::shared_ptr<AnimationClip> &clip, float quality = 0.5f);
	};
}

#endif // _FURY_ANIMATION_UTIL_H_