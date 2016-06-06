#ifndef _FURY_ANIMATION_CLIP_H_
#define _FURY_ANIMATION_CLIP_H_

#include <vector>

#include "Entity.h"

namespace fury
{
	struct KeyFrame
	{
	public:

		unsigned int tick;

		float x, y, z;

		KeyFrame(unsigned int tick = 0, float x = 0.0f, float y = 0.0f, float z = 0.0f) :
			tick(tick), x(x), y(y), z(z) {}
	};

	struct AnimationChannel
	{
	public:

		std::vector<KeyFrame> rotations;

		std::vector<KeyFrame> positions;

		std::vector<KeyFrame> scalings;

		std::string name;

		AnimationChannel(const std::string &name) : 
			name(name) {}
	};

	class FURY_API AnimationClip final : public Entity
	{
	public:

		friend class AnimationUtil;

		typedef std::shared_ptr<AnimationClip> Ptr;

		typedef std::shared_ptr<AnimationChannel> ChannelPtr;

		static Ptr Create(const std::string &name, int ticksPerSecond = 24);

	private:

		std::vector<std::shared_ptr<AnimationChannel>> m_Channels;

		float m_Duration = 0.0f;

		float m_Speed = 1.0f;

		int m_TicksPerSecond = 24;

		bool m_Loop = true;

	public:

		AnimationClip(const std::string &name, int ticksPerSecond = 24);

		virtual ~AnimationClip();

		void CalculateDuration();

		float GetDuration() const;

		void SetDuration(float duration);

		float GetSpeed() const;

		void SetSpeed(float speed);

		int GetTicksPerSecond() const;

		void SetTicksPerSecond(int ticksPerSecond);

		bool GetLoop() const;

		void SetLoop(bool loop);

		int GetChannelCount() const;

		ChannelPtr AddChannel(const std::string &name);

		void AddChannel(const ChannelPtr &channel);

		ChannelPtr RemoveChannel(const std::string &name);

		ChannelPtr GetChannel(const std::string &name) const;

		ChannelPtr GetChannelAt(unsigned int index) const;
	};

}

#endif // _FURY_ANIMATION_CLIP_H_