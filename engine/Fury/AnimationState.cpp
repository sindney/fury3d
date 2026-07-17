#include <cmath>

#include "Fury/AnimationClip.h"
#include "Fury/AnimationState.h"

namespace fury
{
	AnimationState::Ptr AnimationState::Create(const std::string &name, const std::shared_ptr<AnimationClip> &clip)
	{
		return std::make_shared<AnimationState>(name, clip);
	}

	AnimationState::AnimationState(const std::string &name, const std::shared_ptr<AnimationClip> &clip)
		: m_Name(name), m_Clip(clip)
	{
	}

	const std::string &AnimationState::GetName() const { return m_Name; }

	std::shared_ptr<AnimationClip> AnimationState::GetClip() const { return m_Clip; }

	bool AnimationState::IsEnabled() const { return m_Enabled; }
	void AnimationState::SetEnabled(bool enabled) { m_Enabled = enabled; }

	float AnimationState::GetWeight() const { return m_Weight; }
	void AnimationState::SetWeight(float weight) { m_Weight = weight; }

	float AnimationState::GetSpeed() const { return m_Speed; }
	void AnimationState::SetSpeed(float speed) { m_Speed = speed; }

	int AnimationState::GetLayer() const { return m_Layer; }
	void AnimationState::SetLayer(int layer) { m_Layer = layer; }

	float AnimationState::GetTime() const { return m_Time; }

	void AnimationState::SetTime(float time)
	{
		float length = GetLength();
		if (length > 0.0f && time > length)
			time = length;
		if (time < 0.0f)
			time = 0.0f;
		m_Time = time;
	}

	float AnimationState::GetNormalizedTime() const
	{
		float length = GetLength();
		if (length <= 0.0f) return 0.0f;
		return m_Time / length;
	}

	void AnimationState::SetNormalizedTime(float normalizedTime)
	{
		m_Time = normalizedTime * GetLength();
	}

	float AnimationState::GetLength() const
	{
		return m_Clip ? m_Clip->GetDuration() : 0.0f;
	}

	AnimWrapMode AnimationState::GetWrapMode() const { return m_WrapMode; }
	void AnimationState::SetWrapMode(AnimWrapMode mode) { m_WrapMode = mode; }

	float AnimationState::GetTickTime() const
	{
		if (!m_Clip) return 0.0f;
		return m_Time * static_cast<float>(m_Clip->GetTicksPerSecond());
	}

	float AnimationState::GetPingPongDir() const { return m_PingPongDir; }
	void AnimationState::SetPingPongDir(float dir) { m_PingPongDir = dir; }

	bool AnimationState::Advance(float dt)
	{
		if (!m_Clip) return false;
		float length = GetLength();
		if (length <= 0.0f) return false;

		AnimWrapMode mode = m_WrapMode;
		if (mode == AnimWrapMode::Default)
			mode = m_Clip->GetLoop() ? AnimWrapMode::Loop : AnimWrapMode::Once;

		float delta = dt * m_Speed;

		if (mode == AnimWrapMode::PingPong)
		{
			m_Time += delta * m_PingPongDir;
			if (m_Time > length)
			{
				m_Time = length - (m_Time - length);
				m_PingPongDir = -1.0f;
				if (m_Time < 0.0f) m_Time = 0.0f;
			}
			else if (m_Time < 0.0f)
			{
				m_Time = -m_Time;
				m_PingPongDir = 1.0f;
				if (m_Time > length) m_Time = length;
			}
			return true;
		}

		if (mode == AnimWrapMode::Loop)
		{
			m_Time += delta;
			m_Time = std::fmod(m_Time, length);
			if (m_Time < 0.0f) m_Time += length;
			return true;
		}

		// Once + ClampForever: clamp to [0, length].
		m_Time += delta;
		if (m_Time >= length)
		{
			m_Time = length;
			return mode != AnimWrapMode::Once;
		}
		if (m_Time < 0.0f)
		{
			m_Time = 0.0f;
			return mode != AnimWrapMode::Once;
		}
		return true;
	}
}
