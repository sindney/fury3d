#include "Fury/AnimationClip.h"
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

	bool AnimationClip::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "AnimationClip: json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		LoadMemberValue(wrapper, "ticksPerSecond", m_TicksPerSecond);
		LoadMemberValue(wrapper, "speed", m_Speed);
		LoadMemberValue(wrapper, "loop", m_Loop);
		LoadMemberValue(wrapper, "duration", m_Duration);

		// Read one TRS bucket (positions/rotations/scalings) into `out`
		// as tick + vec3. Rotation buckets are stored as Euler radians.
		auto loadKfArray = [](const void* parent, const std::string &key, std::vector<KeyFrame> &out) -> bool
		{
			out.clear();
			return LoadArray(parent, key, [&](const void* node) -> bool
			{
				unsigned int tick = 0;
				float x = 0.0f, y = 0.0f, z = 0.0f;
				LoadMemberValue(node, "tick", tick);
				LoadMemberValue(node, "x", x);
				LoadMemberValue(node, "y", y);
				LoadMemberValue(node, "z", z);
				out.emplace_back(tick, x, y, z);
				return true;
			});
		};

		m_Channels.clear();
		LoadArray(wrapper, "channels", [&](const void* node) -> bool
		{
			std::string name;
			if (!LoadMemberValue(node, "name", name))
				return false;
			auto ch = std::make_shared<AnimationChannel>(name);
			loadKfArray(node, "positions", ch->positions);
			loadKfArray(node, "rotations", ch->rotations);
			loadKfArray(node, "scalings", ch->scalings);
			m_Channels.push_back(ch);
			return true;
		});

		return true;
	}

	void AnimationClip::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "ticksPerSecond");
		SaveValue(wrapper, m_TicksPerSecond);

		SaveKey(wrapper, "speed");
		SaveValue(wrapper, m_Speed);

		SaveKey(wrapper, "loop");
		SaveValue(wrapper, m_Loop);

		SaveKey(wrapper, "duration");
		SaveValue(wrapper, m_Duration);

		// Emit one keyframe sub-array per TRS bucket (tick + vec3).
		auto saveKfArray = [wrapper](const std::string &key, const std::vector<KeyFrame> &frames)
		{
			SaveKey(wrapper, key);
			StartArray(wrapper);
			for (const auto &kf : frames)
			{
				StartObject(wrapper);
				SaveKey(wrapper, "tick"); SaveValue(wrapper, static_cast<unsigned int>(kf.tick));
				SaveKey(wrapper, "x"); SaveValue(wrapper, kf.x);
				SaveKey(wrapper, "y"); SaveValue(wrapper, kf.y);
				SaveKey(wrapper, "z"); SaveValue(wrapper, kf.z);
				EndObject(wrapper);
			}
			EndArray(wrapper);
		};

		SaveKey(wrapper, "channels");
		StartArray(wrapper);
		for (const auto &ch : m_Channels)
		{
			StartObject(wrapper);
			SaveKey(wrapper, "name");
			SaveValue(wrapper, ch->name);
			saveKfArray("positions", ch->positions);
			saveKfArray("rotations", ch->rotations);
			saveKfArray("scalings", ch->scalings);
			EndObject(wrapper);
		}
		EndArray(wrapper);

		if (object)
			EndObject(wrapper);
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