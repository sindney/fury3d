#include "Angle.h"
#include "AnimationClip.h"
#include "AnimationPlayer.h"
#include "Debug.h"
#include "Joint.h"
#include "Mesh.h"
#include "Vector4.h"
#include "Quaternion.h"
#include "SceneNode.h"
#include "MeshRender.h"

namespace fury
{
	AnimationPlayer::Ptr AnimationPlayer::Create(const std::string &name, float speed)
	{
		return std::make_shared<AnimationPlayer>(name, speed);
	}

	AnimationPlayer::AnimationPlayer(const std::string &name, float speed)
		: Entity(name), m_Speed(speed)
	{
		m_TypeIndex = typeid(AnimationPlayer);
	}

	void AnimationPlayer::SetSpeed(float speed)
	{
		m_Speed = speed;
	}

	float AnimationPlayer::GetSpeed() const
	{
		return m_Speed;
	}

	void AnimationPlayer::SetTime(float time)
	{
		m_Time = time;
	}

	float AnimationPlayer::GetTime() const
	{
		return m_Time;
	}

	void AnimationPlayer::AdvanceTime(const std::shared_ptr<SceneNode> &node, const std::shared_ptr<AnimationClip> &clip, float dt)
	{
		m_SceneNode = node;
		m_AnimClip = clip;
		AdvanceTime(dt);
	}

	void AnimationPlayer::AdvanceTime(const std::shared_ptr<AnimationClip> &clip, float dt)
	{
		m_AnimClip = clip;
		AdvanceTime(dt);
	}

	void AnimationPlayer::AdvanceTime(float dt)
	{
		if (m_SceneNode.expired() || m_AnimClip.expired())
		{
			LOGW << "Node or AnimClip empty!";
			return;
		}

		auto clip = m_AnimClip.lock();
		auto node = m_SceneNode.lock();
		auto mesh = node->GetComponent<MeshRender>()->GetMesh();

		m_Time += dt;

		float current = 0.0f, ratio = 0.0f, duration = 0.0f;

		current = m_Time * clip->GetTicksPerSecond() * m_Speed;
		duration = clip->GetDuration() * clip->GetTicksPerSecond();

		if (!clip->GetLoop() && current > duration)
			return;

		while (current > duration)
			current -= duration;

		auto ApplyAnim = [&](std::vector<KeyFrame> &frames, Vector4 &output)
		{
			auto count = frames.size();
			if (count < 1)
				return;

			if (count == 1)
			{
				auto frame = frames[0];
				output.x = frame.x;
				output.y = frame.y;
				output.z = frame.z;
			}
			else
			{
				auto it = frames.begin();
				while (true)
				{
					if (it == frames.end())
						break;

					auto &first = *it++;
					auto &second = *it;

					if (first.tick <= current && second.tick >= current)
					{
						ratio = (current - first.tick) / (second.tick - first.tick);
						auto v0 = Vector4(first.x, first.y, first.z);
						auto v1 = Vector4(second.x, second.y, second.z);
						output = v0 + (v1 - v0) * ratio;
						break;
					}
				}
			}
		};

		// apply animation to joint's local transforms
		auto channelCount = clip->GetChannelCount();
		for (int i = 0; i < channelCount; i++)
		{
			auto channel = clip->GetChannelAt(i);
			auto joint = mesh->GetJoint(channel->name);
			if (joint == nullptr)
				continue;

			auto rotCount = channel->rotations.size();
			Vector4 position, scaling(1, 1), rotation;
			Quaternion quatRotation;
			
			if (rotCount > 0)
			{
				if (rotCount == 1)
				{
					auto frame = channel->rotations[0];
					rotation.x = frame.x;
					rotation.y = frame.y;
					rotation.z = frame.z;
					quatRotation = Angle::EulerRadToQuat(rotation);
				}
				else
				{
					auto it = channel->rotations.begin();
					while (true)
					{
						if (it == channel->rotations.end())
							break;

						auto &first = *it++;
						auto &second = *it;

						if (first.tick <= current && second.tick >= current)
						{
							ratio = (current - first.tick) / (second.tick - first.tick);
							auto q0 = Angle::EulerRadToQuat(Vector4(first.x, first.y, first.z));
							auto q1 = Angle::EulerRadToQuat(Vector4(second.x, second.y, second.z));
							quatRotation = q0.Slerp(q1, ratio);
							break;
						}
					}
				}
			}

			ApplyAnim(channel->positions, position);
			ApplyAnim(channel->scalings, scaling);

			if (dt == 0.0f)
			{
				// reset old and new TRS
				joint->SetPosition(position, true);
				joint->SetRotation(quatRotation, true);
				joint->SetScaling(scaling, true);
			}
			else
			{
				// copy new TRS to old TRS
				joint->SetPosition(joint->GetPosition(), true);
				joint->SetRotation(joint->GetRotation(), true);
				joint->SetScaling(joint->GetScaling(), true);
			}

			// assign new TRS values
			joint->SetPosition(position, false);
			joint->SetRotation(quatRotation, false);
			joint->SetScaling(scaling, false);
		}
	}

	void AnimationPlayer::Display(float dt)
	{
		if (m_SceneNode.expired() || m_AnimClip.expired())
		{
			LOGW << "Node or AnimClip empty.";
			return;
		}

		auto clip = m_AnimClip.lock();
		auto node = m_SceneNode.lock();
		auto mesh = node->GetComponent<MeshRender>()->GetMesh();

		auto channelCount = clip->GetChannelCount();
		for (int i = 0; i < channelCount; i++)
		{
			auto channel = clip->GetChannelAt(i);
			auto joint = mesh->GetJoint(channel->name);
			if (joint == nullptr)
				continue;

			joint->Update(dt);
		}

		// update joint tree
		mesh->GetRootJoint()->Update(Matrix4());
	}
}