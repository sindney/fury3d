#include <cmath>

#include "Angle.h"
#include "AnimationClip.h"
#include "AnimationUtil.h"
#include "Log.h"

namespace fury
{
	void AnimationUtil::OptimizeAnimClip(const std::shared_ptr<AnimationClip> &clip, float quality)
	{
		if (quality > 1.0f)
			quality = 1.0f;

		float maxAngle = quality * Angle::DegreeToRadian(45);
		std::vector<KeyFrame> tempFrames;

		auto ProcessKeyFrames = [&](std::vector<KeyFrame> &keyframes)
		{
			unsigned int count = keyframes.size();
			if (count < 3)
				return;

			KeyFrame prev = keyframes[0];
			tempFrames.erase(tempFrames.begin(), tempFrames.end());
			tempFrames.reserve(count);
			tempFrames.push_back(prev);

			for (unsigned int i = 1; i < count - 1; i++)
			{
				KeyFrame curr = keyframes[i], next = keyframes[i + 1];

				Vector4 vec1(prev.x, prev.y, prev.z);
				Vector4 vec2(curr.x, curr.y, curr.z);
				Vector4 vec3(next.x, next.y, next.z);

				Vector4 a = (vec2 - vec1).Normalized();
				if (a.SquareLength() == 0.0f)
					continue;

				Vector4 b = (vec3 - vec2).Normalized();
				float angle = std::abs(std::acos(a * b));

				if (angle < maxAngle)
					continue;

				prev = curr;
				tempFrames.push_back(curr);
			}

			tempFrames.push_back(keyframes[count - 1]);

			keyframes.resize(tempFrames.size());
			std::copy(tempFrames.begin(), tempFrames.end(), keyframes.begin());
		};

		unsigned int oldCount = 0, newCount = 0;
		for (auto channel : clip->m_Channels)
		{
			if (channel->rotations.size() > 0)
			{
				oldCount += channel->rotations.size();
				ProcessKeyFrames(channel->rotations);
				newCount += channel->rotations.size();
			}
			
			if (channel->positions.size() > 0)
			{
				oldCount += channel->positions.size();
				ProcessKeyFrames(channel->positions);
				newCount += channel->positions.size();
			}
			
			if (channel->scalings.size() > 0)
			{
				oldCount += channel->scalings.size();
				ProcessKeyFrames(channel->scalings);
				newCount += channel->scalings.size();
			}
		}

		FURYD << "Before: " << oldCount << " After: " << newCount;
	}
}