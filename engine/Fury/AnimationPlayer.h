#ifndef _FURY_ANIMATION_PLAYER_H_
#define _FURY_ANIMATION_PLAYER_H_

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Fury/Component.h"
#include "Fury/EnumUtil.h"

namespace fury
{
	class AnimationClip;

	class AnimationState;

	class SceneNode;

	class Transform;

	// Unity-legacy-style animation component. Attaches to a SceneNode and
	// drives playback of registered AnimationClips. Channels resolve to
	// the owning Mesh's Joint tree (skinned -- each Joint mirrors a
	// SceneNode, driven via its Transform) first, else to descendant
	// SceneNodes (node-level).
	//
	// Two-phase tick: AdvanceTime writes old/new TRS pairs; Display
	// interpolates by render alpha into each target's Transform. When
	// animatePhysics is false (default) both phases run on OnUpdate;
	// when true, AdvanceTime runs on OnFixedUpdate and Display on
	// OnUpdate with the engine's fixed-tick alpha.
	class FURY_API Animator : public Component
	{
	public:
		typedef std::shared_ptr<Animator> Ptr;

		static Ptr Create(const std::string &name = "Animator");

	protected:
		std::string m_Name;

		std::vector<std::shared_ptr<AnimationState>> m_States;

		bool m_AnimatePhysics = false;

		AnimWrapMode m_DefaultWrapMode = AnimWrapMode::Default;

		size_t m_FixedUpdateKey = 0;

		size_t m_UpdateKey = 0;

		bool m_Subscribed = false;

		// Crossfade bookkeeping. Empty target name = no active fade.
		std::string m_CrossFadeTarget;

		float m_CrossFadeElapsed = 0.0f;

		float m_CrossFadeLength = 0.0f;

		int m_CrossFadeLayer = 0;

		struct AnimTarget
		{
			enum Kind { None, TransformT } kind = None;
			std::shared_ptr<SceneNode> node;
			std::shared_ptr<Transform> transform;
		};

		mutable std::unordered_map<AnimationState*, std::vector<AnimTarget>> m_TargetCache;

		std::unordered_set<std::string> m_WarnedChannels;

	public:
		Animator(const std::string &name = "Animator");

		const std::string &GetName() const { return m_Name; }
		void SetName(const std::string &name) { m_Name = name; }

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		Component::Ptr Clone() const override;

		// Unity-legacy playback API.
		bool Play(const std::string &name, PlayMode mode = PlayMode::StopSameLayer);

		void Stop();

		void Stop(const std::string &name);

		void Rewind();

		void Rewind(const std::string &name);

		void CrossFade(const std::string &name, float fadeLength, PlayMode mode = PlayMode::StopSameLayer);

		bool IsPlaying(const std::string &name) const;

		// Clip registration. SetClip registers a script-authored clip
		// without going through EntityManager; RemoveClip drops it.
		void SetClip(const std::string &name, const std::shared_ptr<AnimationClip> &clip);

		void RemoveClip(const std::string &name);

		std::shared_ptr<AnimationState> GetState(const std::string &name) const;

		unsigned int GetStateCount() const;

		std::shared_ptr<AnimationState> GetStateAt(unsigned int index) const;

		bool GetAnimatePhysics() const;
		void SetAnimatePhysics(bool value);

		AnimWrapMode GetDefaultWrapMode() const;
		void SetDefaultWrapMode(AnimWrapMode mode);

		// Currently playing clip (highest-weight enabled state), or nullptr.
		std::shared_ptr<AnimationClip> GetClip() const;

		// Two-phase tick. AdvanceTime writes old/new TRS pairs for the
		// current pose; Display interpolates by alpha and rebuilds joints.
		void AdvanceTime(float dt);

		void Display(float alpha);

	protected:
		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnOwnerDestructing(SceneNode &node) override;

	private:
		void Subscribe();

		void Unsubscribe();

		void InvalidateCache();

		// Signal callbacks. TickFixed runs on OnFixedUpdate (advance only);
		// TickUpdate runs on OnUpdate (display, plus advance when not
		// animating physics).
		void TickFixed();

		void TickUpdate(float dt);

		const std::vector<AnimTarget>& ResolveTargets(const std::shared_ptr<AnimationState>& state);

		void ApplyChannel(const std::shared_ptr<AnimationState>& state,
			const std::vector<AnimTarget>& targets, bool scrub);

		// Refresh skinned-mesh AABBs from the pose applied this frame
		// (walks the owner subtree; no-op when nothing posed).
		void UpdateSkinnedBounds();

		std::shared_ptr<AnimationClip> ResolveClipFromManager(const std::string &name) const;
	};
}

#endif // _FURY_ANIMATION_PLAYER_H_
