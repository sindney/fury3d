#include "Fury/AnimationPlayer.h"

#include "Fury/AnimationClip.h"
#include "Fury/AnimationState.h"
#include "Fury/Engine.h"
#include "Fury/EntityManager.h"
#include "Fury/Joint.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/Quaternion.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Transform.h"
#include "Fury/Vector4.h"

#include <functional>

namespace fury
{
	Animator::Ptr Animator::Create(const std::string &name)
	{
		return std::make_shared<Animator>(name);
	}

	Animator::Animator(const std::string &name)
	{
		m_Name = name;
		m_TypeIndex = typeid(Animator);
	}

	bool Animator::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Animator: json node is not an object!";
			return false;
		}

		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "Animator")
		{
			FURYE << "Animator: invalid type " << str;
			return false;
		}

		LoadMemberValue(wrapper, "animate_physics", m_AnimatePhysics);

		std::string wrapStr;
		if (LoadMemberValue(wrapper, "default_wrap", wrapStr))
			m_DefaultWrapMode = EnumUtil::AnimWrapModeFromString(wrapStr);

		// Bound clip names are resolved back through EntityManager on load.
		m_States.clear();
		LoadArray(wrapper, "clips", [&](const void* node) -> bool
		{
			std::string clipName;
			if (!LoadValue(node, clipName) || clipName.empty())
				return true;
			if (auto clip = ResolveClipFromManager(clipName))
			{
				auto state = AnimationState::Create(clipName, clip);
				state->SetWrapMode(m_DefaultWrapMode);
				m_States.push_back(state);
			}
			else
			{
				FURYW << "Animator: clip " << clipName << " not found in EntityManager on load";
			}
			return true;
		});

		return true;
	}

	void Animator::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		SaveKey(wrapper, "type");
		SaveValue(wrapper, "Animator");

		SaveKey(wrapper, "animate_physics");
		SaveValue(wrapper, m_AnimatePhysics);

		SaveKey(wrapper, "default_wrap");
		SaveValue(wrapper, EnumUtil::AnimWrapModeToString(m_DefaultWrapMode));

		SaveKey(wrapper, "clips");
		StartArray(wrapper);
		for (const auto &state : m_States)
		{
			if (state && !state->GetName().empty())
				SaveValue(wrapper, state->GetName());
		}
		EndArray(wrapper);

		if (object)
			EndObject(wrapper);
	}

	Component::Ptr Animator::Clone() const
	{
		auto clone = Animator::Create(m_Name);
		clone->m_AnimatePhysics = m_AnimatePhysics;
		clone->m_DefaultWrapMode = m_DefaultWrapMode;
		for (const auto &state : m_States)
		{
			if (state)
				clone->m_States.push_back(AnimationState::Create(state->GetName(), state->GetClip()));
		}
		return clone;
	}

	void Animator::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		Subscribe();
	}

	void Animator::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnDetaching(node);
		Unsubscribe();
	}

	void Animator::OnOwnerDestructing(SceneNode &node)
	{
		(void)node;
		Unsubscribe();
	}

	void Animator::Subscribe()
	{
		if (m_Subscribed || m_Owner.expired())
			return;

		auto self = std::static_pointer_cast<Animator>(m_Owner.lock()->GetComponent(typeid(Animator)));
		if (!self) return;

		m_FixedUpdateKey = Engine::OnFixedUpdate->Connect(self, &Animator::TickFixed);
		m_UpdateKey = Engine::OnUpdate->Connect(self, &Animator::TickUpdate);
		m_Subscribed = true;
	}

	void Animator::Unsubscribe()
	{
		if (!m_Subscribed)
			return;
		Engine::OnFixedUpdate->Disconnect(m_FixedUpdateKey);
		Engine::OnUpdate->Disconnect(m_UpdateKey);
		m_Subscribed = false;
	}

	void Animator::InvalidateCache()
	{
		m_TargetCache.clear();
	}

	std::shared_ptr<AnimationClip> Animator::ResolveClipFromManager(const std::string &name) const
	{
		if (!Scene::Active)
			return nullptr;
		auto mgr = Scene::Active->GetEntityManager();
		if (!mgr)
			return nullptr;
		return mgr->Get<AnimationClip>(name);
	}

	std::shared_ptr<AnimationState> Animator::GetState(const std::string &name) const
	{
		for (const auto &s : m_States)
			if (s && s->GetName() == name)
				return s;
		return nullptr;
	}

	unsigned int Animator::GetStateCount() const { return static_cast<unsigned int>(m_States.size()); }

	std::shared_ptr<AnimationState> Animator::GetStateAt(unsigned int index) const
	{
		if (index < m_States.size())
			return m_States[index];
		return nullptr;
	}

	bool Animator::GetAnimatePhysics() const { return m_AnimatePhysics; }
	void Animator::SetAnimatePhysics(bool value) { m_AnimatePhysics = value; }

	AnimWrapMode Animator::GetDefaultWrapMode() const { return m_DefaultWrapMode; }
	void Animator::SetDefaultWrapMode(AnimWrapMode mode) { m_DefaultWrapMode = mode; }

	std::shared_ptr<AnimationClip> Animator::GetClip() const
	{
		auto dom = GetState("");
		// Pick highest-weight enabled state.
		std::shared_ptr<AnimationState> best;
		for (const auto &s : m_States)
		{
			if (!s || !s->IsEnabled()) continue;
			if (!best || s->GetWeight() > best->GetWeight())
				best = s;
		}
		return best ? best->GetClip() : nullptr;
	}

	bool Animator::Play(const std::string &name, PlayMode mode)
	{
		auto state = GetState(name);
		if (!state)
		{
			auto clip = ResolveClipFromManager(name);
			if (!clip)
			{
				FURYW << "Animator: clip " << name << " not found";
				return false;
			}
			state = AnimationState::Create(name, clip);
			state->SetWrapMode(m_DefaultWrapMode);
			m_States.push_back(state);
			InvalidateCache();
		}

		int targetLayer = state->GetLayer();
		if (mode == PlayMode::StopAll)
		{
			for (const auto &s : m_States)
			{
				if (s && s != state)
				{
					s->SetEnabled(false);
					s->SetTime(0.0f);
				}
			}
		}
		else // StopSameLayer
		{
			for (const auto &s : m_States)
			{
				if (s && s != state && s->GetLayer() == targetLayer)
				{
					s->SetEnabled(false);
					s->SetTime(0.0f);
				}
			}
		}

		state->SetEnabled(true);
		state->SetWeight(1.0f);
		state->SetTime(0.0f);
		state->SetPingPongDir(1.0f);
		m_CrossFadeTarget.clear();
		return true;
	}

	void Animator::Stop()
	{
		for (const auto &s : m_States)
		{
			if (s)
			{
				s->SetEnabled(false);
				s->SetTime(0.0f);
			}
		}
		m_CrossFadeTarget.clear();
	}

	void Animator::Stop(const std::string &name)
	{
		auto state = GetState(name);
		if (state)
		{
			state->SetEnabled(false);
			state->SetTime(0.0f);
		}
	}

	void Animator::Rewind()
	{
		for (const auto &s : m_States)
		{
			if (s)
			{
				s->SetTime(0.0f);
				s->SetPingPongDir(1.0f);
			}
		}
	}

	void Animator::Rewind(const std::string &name)
	{
		auto state = GetState(name);
		if (state)
		{
			state->SetTime(0.0f);
			state->SetPingPongDir(1.0f);
		}
	}

	void Animator::CrossFade(const std::string &name, float fadeLength, PlayMode mode)
	{
		auto state = GetState(name);
		if (!state)
		{
			auto clip = ResolveClipFromManager(name);
			if (!clip)
			{
				FURYW << "Animator: clip " << name << " not found for CrossFade";
				return;
			}
			state = AnimationState::Create(name, clip);
			state->SetWrapMode(m_DefaultWrapMode);
			m_States.push_back(state);
			InvalidateCache();
		}

		int targetLayer = state->GetLayer();
		if (mode == PlayMode::StopAll)
		{
			for (const auto &s : m_States)
			{
				if (s && s != state && s->IsEnabled())
					s->SetWeight(1.0f);
			}
		}
		else
		{
			for (const auto &s : m_States)
			{
				if (s && s != state && s->GetLayer() == targetLayer && s->IsEnabled())
					s->SetWeight(1.0f);
			}
		}

		state->SetEnabled(true);
		state->SetWeight(0.0f);
		state->SetTime(0.0f);
		state->SetPingPongDir(1.0f);
		m_CrossFadeTarget = name;
		m_CrossFadeElapsed = 0.0f;
		m_CrossFadeLength = fadeLength > 0.0f ? fadeLength : 0.0001f;
		m_CrossFadeLayer = targetLayer;
	}

	bool Animator::IsPlaying(const std::string &name) const
	{
		auto state = GetState(name);
		if (!state || !state->IsEnabled())
			return false;
		AnimWrapMode mode = state->GetWrapMode();
		if (mode == AnimWrapMode::Default)
			mode = state->GetClip() && state->GetClip()->GetLoop() ? AnimWrapMode::Loop : AnimWrapMode::Once;
		if (mode == AnimWrapMode::Once)
		{
			float length = state->GetLength();
			if (length > 0.0f && state->GetTime() >= length)
				return false;
		}
		return true;
	}

	void Animator::SetClip(const std::string &name, const std::shared_ptr<AnimationClip> &clip)
	{
		auto existing = GetState(name);
		if (existing)
		{
			existing->SetWrapMode(m_DefaultWrapMode);
			// Replace the clip on the existing state by recreating.
			existing = AnimationState::Create(name, clip);
			existing->SetWrapMode(m_DefaultWrapMode);
			for (auto &s : m_States)
			{
				if (s && s->GetName() == name)
				{
					s = existing;
					break;
				}
			}
		}
		else
		{
			auto state = AnimationState::Create(name, clip);
			state->SetWrapMode(m_DefaultWrapMode);
			m_States.push_back(state);
		}
		InvalidateCache();
	}

	void Animator::RemoveClip(const std::string &name)
	{
		for (auto it = m_States.begin(); it != m_States.end(); ++it)
		{
			if (*it && (*it)->GetName() == name)
			{
				m_States.erase(it);
				InvalidateCache();
				return;
			}
		}
	}

	void Animator::TickFixed()
	{
		AdvanceTime(Engine::GetFixedDt());
	}

	void Animator::TickUpdate(float dt)
	{
		if (m_AnimatePhysics)
			Display(Engine::GetFixedTickAlpha());
		else
		{
			AdvanceTime(dt);
			// Non-physics path: snap to the sampled pose each frame
			// (alpha = 1). Pre = previous Post, Post = new sampled
			// pose, so old + (new-old)*1 = new.
			Display(1.0f);
		}
	}

	// Sample a keyframe vector track (positions or scalings) at `current`
	// tick, linearly interpolating between surrounding frames.
	static void SampleVectorTrack(const std::vector<KeyFrame> &frames, float current, Vector4 &out)
	{
		auto count = frames.size();
		if (count == 0) return;
		if (count == 1)
		{
			out.x = frames[0].x; out.y = frames[0].y; out.z = frames[0].z;
			return;
		}
		for (size_t i = 0; i + 1 < count; ++i)
		{
			const auto &f = frames[i];
			const auto &s = frames[i + 1];
			if (f.tick <= current && s.tick >= current)
			{
				float span = static_cast<float>(s.tick - f.tick);
				float r = span > 0.0f ? (current - static_cast<float>(f.tick)) / span : 0.0f;
				Vector4 v0(f.x, f.y, f.z);
				Vector4 v1(s.x, s.y, s.z);
				out = v0 + (v1 - v0) * r;
				return;
			}
		}
		// Past the last frame: hold last.
		out.x = frames.back().x; out.y = frames.back().y; out.z = frames.back().z;
	}

	// Sample a rotation track (Euler radians in KeyFrame) as a slerped
	// quaternion at `current` tick.
	static void SampleRotationTrack(const std::vector<KeyFrame> &frames, float current, Quaternion &out)
	{
		auto count = frames.size();
		if (count == 0) return;
		if (count == 1)
		{
			out = MathUtil::EulerRadToQuat(Vector4(frames[0].x, frames[0].y, frames[0].z));
			return;
		}
		for (size_t i = 0; i + 1 < count; ++i)
		{
			const auto &f = frames[i];
			const auto &s = frames[i + 1];
			if (f.tick <= current && s.tick >= current)
			{
				float span = static_cast<float>(s.tick - f.tick);
				float r = span > 0.0f ? (current - static_cast<float>(f.tick)) / span : 0.0f;
				Quaternion q0 = MathUtil::EulerRadToQuat(Vector4(f.x, f.y, f.z));
				Quaternion q1 = MathUtil::EulerRadToQuat(Vector4(s.x, s.y, s.z));
				out = q0.Slerp(q1, r);
				return;
			}
		}
		out = MathUtil::EulerRadToQuat(Vector4(frames.back().x, frames.back().y, frames.back().z));
	}

	const std::vector<Animator::AnimTarget>& Animator::ResolveTargets(const std::shared_ptr<AnimationState>& state)
	{
		auto it = m_TargetCache.find(state.get());
		if (it != m_TargetCache.end())
			return it->second;

		std::vector<AnimTarget> targets;
		if (!state || !state->GetClip())
		{
			m_TargetCache[state.get()] = targets;
			return m_TargetCache[state.get()];
		}

		auto clip = state->GetClip();
		auto owner = m_Owner.lock();
		std::shared_ptr<Mesh> mesh;
		if (owner)
		{
			if (auto mr = owner->GetComponent<MeshRender>())
				mesh = mr->GetMesh();
		}

		int channelCount = clip->GetChannelCount();
		targets.resize(channelCount);
		for (int i = 0; i < channelCount; ++i)
		{
			auto channel = clip->GetChannelAt(i);
			if (!channel) { targets[i].kind = AnimTarget::None; continue; }

			const std::string &cname = channel->name;
			std::shared_ptr<Joint> joint = mesh ? mesh->GetJoint(cname) : nullptr;
			if (joint)
			{
				// glTF skin: the joint mirrors a scene-graph node. Drive
				// that node's Transform so Recompose produces JᵢW, which
				// Joint::GetFinalMatrix pairs with the ibm. SceneNode refs
				// are wired at import (GltfImporter) and re-linked on scene
				// load (Scene::Load); a missing ref means a stale/legacy
				// joint -- warn and skip rather than fall back to a tree-walk.
				auto jointNode = joint->GetSceneNode();
				if (jointNode)
				{
					auto tf = jointNode->GetComponent<Transform>();
					if (!tf)
					{
						tf = Transform::Create();
						jointNode->AddComponent(tf);
					}
					targets[i].kind = AnimTarget::TransformT;
					targets[i].node = jointNode;
					targets[i].transform = tf;
					continue;
				}
				if (m_WarnedChannels.insert(cname).second)
					FURYW << "Animator: joint '" << cname << "' has no linked SceneNode; skipping";
				targets[i].kind = AnimTarget::None;
				continue;
			}

			if (owner)
			{
				auto node = owner->FindChildRecursively(cname);
				if (node)
				{
					auto tf = node->GetComponent<Transform>();
					if (!tf)
					{
						tf = Transform::Create();
						node->AddComponent(tf);
					}
					targets[i].kind = AnimTarget::TransformT;
					targets[i].node = node;
					targets[i].transform = tf;
					continue;
				}
			}

			if (m_WarnedChannels.insert(cname).second)
				FURYW << "Animator: channel '" << cname << "' matches no joint or child node; skipping";
			targets[i].kind = AnimTarget::None;
		}

		m_TargetCache[state.get()] = std::move(targets);
		return m_TargetCache[state.get()];
	}

	void Animator::ApplyChannel(const std::shared_ptr<AnimationState>& state,
		const std::vector<AnimTarget>& targets, bool scrub)
	{
		auto clip = state->GetClip();
		if (!clip) return;
		float current = state->GetTickTime();
		int channelCount = clip->GetChannelCount();

		for (int i = 0; i < channelCount && i < static_cast<int>(targets.size()); ++i)
		{
			const auto &t = targets[i];
			if (t.kind == AnimTarget::None) continue;

			auto channel = clip->GetChannelAt(i);
			if (!channel) continue;

		// Initialize to the target's current (bind) TRS so that
		// channels with an empty track for a component (common in
		// FBX2glTF-baked clips -- e.g. joints that only rotate have
		// no position/scale track) preserve the bind value instead
		// of collapsing to Vector4()/identity. SampleVectorTrack /
		// SampleRotationTrack leave `out` untouched on empty input.
		Vector4 position, scaling(1, 1, 1, 1);
		Quaternion rotation;
		if (t.kind == AnimTarget::TransformT && t.node)
		{
			position = t.node->GetLocalPosition();
			rotation = t.node->GetLocalRoattion();
			scaling = t.node->GetLocalScale();
		}
		SampleVectorTrack(channel->positions, current, position);
		SampleVectorTrack(channel->scalings, current, scaling);
		SampleRotationTrack(channel->rotations, current, rotation);

		if (t.kind == AnimTarget::TransformT && t.transform)
		{
			if (scrub)
			{
				t.transform->SetPreTransforms(position, rotation, scaling);
				t.transform->SetPostTransforms(position, rotation, scaling);
			}
			else
			{
				// Pre = previous Post; Post = new sampled pose.
				t.transform->SetPreTransforms(t.transform->GetPostPosition(),
					t.transform->GetPostRotation(), t.transform->GetPostScale());
				t.transform->SetPostTransforms(position, rotation, scaling);
			}
		}
		}
	}

	void Animator::AdvanceTime(float dt)
	{
		// Pick the highest-weight enabled state as the dominant pose source.
		std::shared_ptr<AnimationState> dominant;
		for (const auto &s : m_States)
		{
			if (!s || !s->IsEnabled()) continue;
			if (!dominant || s->GetWeight() > dominant->GetWeight())
				dominant = s;
		}
		if (!dominant)
			return;

		// Advance time on all enabled states (so non-dominant ones stay
		// in sync for crossfade handoffs). dt==0 means scrub -- no advance.
		if (dt != 0.0f)
		{
			for (const auto &s : m_States)
			{
				if (!s || !s->IsEnabled()) continue;
				if (!s->Advance(dt))
				{
					// Once-mode clip finished.
					s->SetEnabled(false);
					if (s == dominant)
						dominant.reset();
				}
			}
		}

		// Ramp crossfade weights.
		if (!m_CrossFadeTarget.empty() && m_CrossFadeLength > 0.0f)
		{
			m_CrossFadeElapsed += dt;
			float t = m_CrossFadeElapsed / m_CrossFadeLength;
			if (t >= 1.0f)
			{
				// Fade complete: target wins, others on the layer stop.
				auto target = GetState(m_CrossFadeTarget);
				if (target)
				{
					target->SetWeight(1.0f);
					for (const auto &s : m_States)
					{
						if (s && s != target && s->GetLayer() == m_CrossFadeLayer)
						{
							s->SetEnabled(false);
							s->SetWeight(0.0f);
						}
					}
				}
				m_CrossFadeTarget.clear();
			}
			else
			{
				auto target = GetState(m_CrossFadeTarget);
				if (target)
				{
					target->SetWeight(t);
					for (const auto &s : m_States)
					{
						if (s && s != target && s->GetLayer() == m_CrossFadeLayer && s->IsEnabled())
							s->SetWeight(1.0f - t);
					}
				}
			}
		}

		// Re-pick dominant after weight changes / state stops.
		if (!dominant)
		{
			for (const auto &s : m_States)
			{
				if (!s || !s->IsEnabled()) continue;
				if (!dominant || s->GetWeight() > dominant->GetWeight())
					dominant = s;
			}
		}
		if (!dominant)
			return;

		bool scrub = (dt == 0.0f);
		const auto &targets = ResolveTargets(dominant);
		ApplyChannel(dominant, targets, scrub);
	}

	void Animator::Display(float alpha)
	{
		std::shared_ptr<AnimationState> dominant;
		for (const auto &s : m_States)
		{
			if (!s || !s->IsEnabled()) continue;
			if (!dominant || s->GetWeight() > dominant->GetWeight())
				dominant = s;
		}
		if (!dominant)
			return;

		const auto &targets = ResolveTargets(dominant);
		auto clip = dominant->GetClip();
		if (!clip) return;

		// Interpolate the per-target TRS pairs by render alpha. Skinned
		// joints are driven through their linked SceneNode's Transform
		// (the scene graph's Recompose produces JᵢW, which
		// Joint::GetFinalMatrix pairs with the ibm) -- no parallel joint
		// tree-walk is needed.
		int channelCount = clip->GetChannelCount();
		for (int i = 0; i < channelCount && i < static_cast<int>(targets.size()); ++i)
		{
			const auto &t = targets[i];
			if (t.kind == AnimTarget::TransformT && t.transform)
				t.transform->SetDeltaTime(alpha);
		}

		UpdateSkinnedBounds();
	}

	void Animator::UpdateSkinnedBounds()
	{
		auto owner = m_Owner.lock();
		if (!owner) return;

		// The pose applied this frame can carry a skinned mesh outside
		// its bind-pose AABB -- refresh bounds from the deformed pose so
		// culling / LOD / shadows see where the mesh actually is. The
		// skinned branch of Mesh::CalculateAABB blends vertices by
		// Joint::GetFinalMatrix() (world space), so convert back to the
		// mesh node's model space before SetModelAABB, then re-insert
		// into the scene manager (same propagation path Recompose uses).
		std::function<void(const SceneNode::Ptr &)> walk =
			[&](const SceneNode::Ptr &n)
		{
			if (!n) return;
			if (auto render = n->GetComponent<MeshRender>())
			{
				auto mesh = render->GetMesh();
				if (mesh && mesh->IsSkinnedMesh())
				{
					mesh->CalculateAABB();
					n->SetModelAABB(n->GetWorldMatrix().Inverse().Multiply(mesh->GetAABB()));
					if (Scene::Active && Scene::Active->GetSceneManager())
						Scene::Active->GetSceneManager()->UpdateSceneNode(n);
				}
			}
			for (unsigned int i = 0; i < n->GetChildCount(); ++i)
				walk(n->GetChildAt(i));
		};
		walk(owner);
	}
}
