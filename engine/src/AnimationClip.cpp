#include "AnimationClip.h"
#include "Log.h"

namespace fury
{
	AnimationClip::Ptr AnimationClip::Create(const std::string &name, int ticksPerSecond)
	{
		return std::make_shared<AnimationClip>(name, ticksPerSecond);
	}

	AnimationClip::AnimationClip(const std::string &name, int ticksPerSecond)
		: Entity(name), m_TicksPerSecond(ticksPerSecond)
	{
		m_TypeIndex = typeid(AnimationClip);
	}

	AnimationClip::~AnimationClip()
	{
		FURYD << "AnimationClip " << m_Name << " destoried!";
	}

	void AnimationClip::CalculateDuration()
	{
		m_Duration = 0.0f;

		auto Try = [&](unsigned int value)
		{
			float time = (float)value / m_TicksPerSecond;
			if (time > m_Duration)
				m_Duration = time;
		};

		for (auto channel : m_Channels)
		{
			int posCount = channel->positions.size();
			if (posCount > 0)
				Try(channel->positions[posCount - 1].tick);

			int rotCount = channel->rotations.size();
			if (rotCount > 0)
				Try(channel->rotations[rotCount - 1].tick);

			int sclCount = channel->scalings.size();
			if (sclCount > 0)
				Try(channel->scalings[sclCount - 1].tick);
		}
	}

	float AnimationClip::GetDuration() const
	{
		return m_Duration;
	}

	void AnimationClip::SetDuration(float duration)
	{
		m_Duration = duration;
	}

	float AnimationClip::GetSpeed() const
	{
		return m_Speed;
	}

	void AnimationClip::SetSpeed(float speed)
	{
		m_Speed = speed;
	}

	int AnimationClip::GetTicksPerSecond() const
	{
		return m_TicksPerSecond;
	}

	void AnimationClip::SetTicksPerSecond(int ticksPerSecond)
	{
		if (m_TicksPerSecond != ticksPerSecond)
		{
			m_TicksPerSecond = ticksPerSecond;
			CalculateDuration();
		}
	}

	bool AnimationClip::GetLoop() const
	{
		return m_Loop;
	}

	void AnimationClip::SetLoop(bool loop)
	{
		m_Loop = loop;
	}

	int AnimationClip::GetChannelCount() const
	{
		return m_Channels.size();
	}

	AnimationClip::ChannelPtr AnimationClip::AddChannel(const std::string &name)
	{
		auto channel = std::make_shared<AnimationChannel>(name);
		m_Channels.push_back(channel);
		return channel;
	}

	void AnimationClip::AddChannel(const AnimationClip::ChannelPtr &channel)
	{
		m_Channels.push_back(channel);
	}

	AnimationClip::ChannelPtr AnimationClip::RemoveChannel(const std::string &name)
	{
		for (unsigned int i = 0; i < m_Channels.size(); i++)
		{
			auto channel = m_Channels[i];
			if (channel->name == name)
			{
				m_Channels.erase(m_Channels.begin() + i);
				return channel;
			}
		}
		return nullptr;
	}

	AnimationClip::ChannelPtr AnimationClip::GetChannel(const std::string &name) const
	{
		for (unsigned int i = 0; i < m_Channels.size(); i++)
		{
			auto channel = m_Channels[i];
			if (channel->name == name)
				return channel;
		}
		return nullptr;
	}

	AnimationClip::ChannelPtr AnimationClip::GetChannelAt(unsigned int index) const
	{
		if (index < m_Channels.size())
			return m_Channels[index];
		else
			return nullptr;
	}
}