#ifndef _FURY_ANIMATION_STATE_H_
#define _FURY_ANIMATION_STATE_H_

#include <memory>
#include <string>

#include "Fury/EnumUtil.h"
#include "Macros.h"

namespace fury
{
	class AnimationClip;

	// Per-clip runtime state owned by an Animator. Mirrors Unity's legacy
	// AnimationState: name/clip/enabled/weight/speed/layer/time/wrapMode.
	// `time` and `length` are in seconds (Unity API surface); the clip's
	// tick-based time base is converted at the sampling boundary.
	class FURY_API AnimationState
	{
	public:
		typedef std::shared_ptr<AnimationState> Ptr;

		static Ptr Create(const std::string &name, const std::shared_ptr<AnimationClip> &clip);

		AnimationState(const std::string &name, const std::shared_ptr<AnimationClip> &clip);

		const std::string &GetName() const;

		std::shared_ptr<AnimationClip> GetClip() const;

		bool IsEnabled() const;
		void SetEnabled(bool enabled);

		float GetWeight() const;
		void SetWeight(float weight);

		float GetSpeed() const;
		void SetSpeed(float speed);

		int GetLayer() const;
		void SetLayer(int layer);

		// seconds
		float GetTime() const;
		void SetTime(float time);

		float GetNormalizedTime() const;
		void SetNormalizedTime(float normalizedTime);

		// seconds, read-only (derived from clip duration)
		float GetLength() const;

		AnimWrapMode GetWrapMode() const;
		void SetWrapMode(AnimWrapMode mode);

		// Advance `time` by `dt` seconds using speed + wrap mode. Returns
		// false when a non-looping clip has reached its end (so the caller
		// can stop it). Default follows the clip's m_Loop flag.
		bool Advance(float dt);

		// Current sampling time in the clip's tick base.
		float GetTickTime() const;

		// PingPong direction state (+1 forward, -1 reverse). Exposed so the
		// Animator can reset it on Rewind/Play.
		float GetPingPongDir() const;
		void SetPingPongDir(float dir);

	private:
		std::string m_Name;

		std::shared_ptr<AnimationClip> m_Clip;

		bool m_Enabled = false;

		float m_Weight = 1.0f;

		float m_Speed = 1.0f;

		int m_Layer = 0;

		float m_Time = 0.0f;

		AnimWrapMode m_WrapMode = AnimWrapMode::Default;

		float m_PingPongDir = 1.0f;
	};
}

#endif // _FURY_ANIMATION_STATE_H_
