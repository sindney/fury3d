#include "Fury/AnimationClip.h"
#include "Fury/Archive.h"
#include "Fury/Log.h"

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

	void AnimationClip::Serialize(Archive& ar)
	{
		Entity::SerializeFields(ar);

		ar << Optional("ticksPerSecond", m_TicksPerSecond, 24);
		ar << Optional("speed", m_Speed, 1.0f);
		ar << Optional("loop", m_Loop, true);
		ar << Optional("duration", m_Duration, 0.0f);

		// One TRS bucket (tick + vec3; rotations are Euler radians).
		auto transferKfArray = [&ar](const char *key, std::vector<KeyFrame> &frames)
		{
			if (ar.IsLoading())
				frames.clear();
			if (!ar.EnterArray(key, ArchiveFieldPolicy::Optional))
				return;
			std::size_t count = ar.IsLoading() ? ar.GetArraySize() : frames.size();
			for (std::size_t i = 0; i < count; ++i)
			{
				if (ar.IsLoading())
				{
					ar.SetArrayIndex(i);
					frames.emplace_back();
				}
				KeyFrame &kf = frames[i];
				if (ar.EnterObject())
				{
					unsigned int tick = static_cast<unsigned int>(kf.tick);
					float x = kf.x, y = kf.y, z = kf.z;
					ar << Optional("tick", tick, 0u);
					ar << Optional("x", x, 0.0f);
					ar << Optional("y", y, 0.0f);
					ar << Optional("z", z, 0.0f);
					if (ar.IsLoading())
					{
						kf.tick = tick;
						kf.x = x;
						kf.y = y;
						kf.z = z;
					}
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		};

		if (ar.IsLoading())
			m_Channels.clear();
		if (ar.EnterArray("channels", ArchiveFieldPolicy::Optional))
		{
			std::size_t count = ar.IsLoading() ? ar.GetArraySize() : m_Channels.size();
			for (std::size_t i = 0; i < count; ++i)
			{
				if (ar.IsLoading())
				{
					ar.SetArrayIndex(i);
					auto channel = std::make_shared<AnimationChannel>("");
					if (ar.EnterObject())
					{
						std::string name;
						ar << Required("name", name);
						channel->name = name;
						transferKfArray("positions", channel->positions);
						transferKfArray("rotations", channel->rotations);
						transferKfArray("scalings", channel->scalings);
						ar.LeaveObject();
					}
					m_Channels.push_back(channel);
				}
				else
				{
					ar.EnterObject();
					std::string name = m_Channels[i]->name;
					ar << Required("name", name);
					transferKfArray("positions", m_Channels[i]->positions);
					transferKfArray("rotations", m_Channels[i]->rotations);
					transferKfArray("scalings", m_Channels[i]->scalings);
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}
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
			size_t posCount = channel->positions.size();
			if (posCount > 0)
				Try(channel->positions[posCount - 1].tick);

			size_t rotCount = channel->rotations.size();
			if (rotCount > 0)
				Try(channel->rotations[rotCount - 1].tick);

			size_t sclCount = channel->scalings.size();
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
		return static_cast<int>(m_Channels.size());
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