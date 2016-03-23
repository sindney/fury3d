#ifndef _FURY_ANIMATION_UTIL_H_
#define _FURY_ANIMATION_UTIL_H_

#include "Singleton.h"
#include "Entity.h"

namespace fury
{
	class AnimationClip;

	class FURY_API AnimationUtil : public Singleton<AnimationUtil>
	{
	public:

		typedef std::shared_ptr<AnimationUtil> Ptr;

		void OptimizeAnimClip(const std::shared_ptr<AnimationClip> &clip, float quality = 0.5f);
	};
}

#endif // _FURY_ANIMATION_UTIL_H_