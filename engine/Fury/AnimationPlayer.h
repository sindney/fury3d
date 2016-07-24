#ifndef _FURY_ANIMATION_PLAYER_H_
#define _FURY_ANIMATION_PLAYER_H_

#include "Fury/Entity.h"

namespace fury
{
	class AnimationClip;

	class SceneNode;

	class FURY_API AnimationPlayer : public Entity
	{
	public:

		typedef std::shared_ptr<AnimationPlayer> Ptr;

		static Ptr Create(const std::string &name, float speed = 1.0f);

	protected:

		std::weak_ptr<SceneNode> m_SceneNode;

		std::weak_ptr<AnimationClip> m_AnimClip;

		float m_Speed = 1.0f;

		float m_Time = 0.0f;

	public:

		AnimationPlayer(const std::string &name, float speed = 1.0f);

		void SetSpeed(float speed);

		float GetSpeed() const;

		void SetTime(float time);

		float GetTime() const;

		// make sure the node has meshRender compnent with a mesh.
		void AdvanceTime(const std::shared_ptr<SceneNode> &node, const std::shared_ptr<AnimationClip> &clip, float dt);

		void AdvanceTime(const std::shared_ptr<AnimationClip> &clip, float dt);

		void AdvanceTime(float dt);

		// 0 - 1, this interpolates the result from advanceTime call.
		void Display(float dt);
	};
}

#endif // _FURY_ANIMATION_PLAYER_H_