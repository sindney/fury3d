#include "Fury/Log.h"
#include "Fury/Archive.h"
#include "Fury/Light.h"
#include "Fury/MathUtil.h"
#include "Fury/Mesh.h"
#include "Fury/MeshUtil.h"
#include "Fury/SceneNode.h"
#include "Fury/Scene.h"
#include "Fury/EntityManager.h"

#include <algorithm>

namespace fury
{
	Light::Ptr Light::Create()
	{
		return std::make_shared<Light>();
	}

	Light::Light()
	{
		m_TypeIndex = typeid(Light);
	};

	void Light::Serialize(Archive& ar)
	{
		std::string typeStr;
		if (ar.IsSaving()) typeStr = EnumUtil::LightTypeToString(m_Type);
		ar << Required("light_type", typeStr);
		if (ar.IsLoading()) SetType(EnumUtil::LightTypeFromString(typeStr));

		ar << Optional("color", m_Color);
		ar << Optional("intensity", m_Intensity);
		ar << Optional("inner_angle", m_InnerAngle);
		ar << Optional("outter_angle", m_OutterAngle);
		ar << Optional("falloff", m_Falloff);
		ar << Optional("radius", m_Radius);
		ar << Optional("cast_shadows", m_CastShadows);

		if (!ar.IsLoading()) return;

		// Legacy scenes serialized degree values into these radian
		// fields (old default was 45.0); a spot fov never exceeds pi,
		// so anything larger must be degrees -- migrate.
		if (m_InnerAngle > MathUtil::PI || m_OutterAngle > MathUtil::PI)
		{
			FURYW << "Light: inner/outter angle > 180deg stored as degrees ("
				<< m_InnerAngle << ", " << m_OutterAngle
				<< ") -- converting to radians; re-save the scene to persist.";
			m_InnerAngle = std::min(m_InnerAngle, 180.0f) * MathUtil::DegToRad;
			m_OutterAngle = std::min(m_OutterAngle, 180.0f) * MathUtil::DegToRad;
		}

		CalculateAABB();
	}

	Component::Ptr Light::Clone() const
	{
		auto ptr = Light::Create();
		ptr->m_Type = m_Type;
		ptr->m_Color = m_Color;
		ptr->m_Intensity = m_Intensity;
		ptr->m_InnerAngle = m_InnerAngle;
		ptr->m_OutterAngle = m_OutterAngle;
		ptr->m_Falloff = m_Falloff;
		ptr->m_Radius = m_Radius;
		ptr->m_AABB = m_AABB;
		return ptr;
	}

	LightType Light::GetType() const
	{
		return m_Type;
	}

	void Light::SetType(LightType type)
	{
		m_Type = type;
	}

	void Light::SetColor(Color color)
	{
		m_Color = color;
	}

	Color Light::GetColor() const
	{
		return m_Color;
	}

	void Light::SetIntensity(float value)
	{
		m_Intensity = value;
	}

	float Light::GetIntensity() const
	{
		return m_Intensity;
	}

	void Light::SetInnerAngle(float value)
	{
		m_InnerAngle = value;
	}

	float Light::GetInnerAngle() const
	{
		return m_InnerAngle;
	}

	void Light::SetOutterAngle(float value)
	{
		m_OutterAngle = value;
	}

	float Light::GetOutterAngle() const
	{
		return m_OutterAngle;
	}

	void Light::SetFalloff(float value)
	{
		m_Falloff = value;
	}

	float Light::GetFalloff() const
	{
		return m_Falloff;
	}

	void Light::SetRadius(float value)
	{
		m_Radius = value;
	}

	float Light::GetRadius() const
	{
		return m_Radius;
	}

	float Light::GetEffectiveRadius() const
	{
		auto owner = m_Owner.lock();
		if (!owner) return m_Radius;
		float scale = owner->GetWorldScale().x;
		if (scale < 1e-6f) scale = 1.0f;
		return m_Radius * scale;
	}

	void Light::SetCastShadows(bool cast)
	{
		m_CastShadows = cast;
	}

	bool Light::GetCastShadows() const
	{
		return m_CastShadows;
	}

	BoxBounds Light::GetAABB() const
	{
		return m_AABB;
	}

	void Light::CalculateAABB()
	{
		if (m_Type == LightType::POINT)
		{
			m_AABB.SetMinMax(Vector4(-m_Radius), Vector4(m_Radius));
		}
		else if (m_Type == LightType::DIRECTIONAL)
		{
			m_AABB.SetInfinite(true);
		}
		else
		{
			float height = m_Radius;
			float topR = std::tan(m_OutterAngle * 0.5f) * height;
			m_AABB.SetMinMax(Vector4(-topR, -height, -topR), Vector4(topR, 0.0f, topR));
		}

		// Sync the owner node's culling bounds + octree placement, so radius/type edits after attach aren't culled with a stale AABB.
		if (auto owner = m_Owner.lock())
		{
			owner->SetModelAABB(m_AABB);
			owner->Recompose(true);
		}
	}

	std::shared_ptr<Mesh> Light::GetMesh()
	{
		if (m_Mesh == nullptr)
			m_Mesh = EvaluateVolume();

		return m_Mesh;
	}

	std::shared_ptr<Mesh> Light::EvaluateVolume()
	{
		if (m_Mesh) m_Mesh.reset();

		if (m_Type == LightType::DIRECTIONAL)
		{
			m_Mesh = MeshUtil::GetUnitQuad();
		}
		else if (m_Type == LightType::POINT)
		{
			m_Mesh = MeshUtil::GetUnitIcoSphere();
		}
		else
		{
			float height = m_Radius;
			float bottomR = std::tan(m_OutterAngle * 0.5f) * height;
			m_Mesh = MeshUtil::CreateCylinder("spotlight_convex", 0.0f, bottomR, height, 5, 20);

			Matrix4 matrix;
			matrix.Translate(Vector4(0.0f, -height * 0.5f, 0.0f));

			MeshUtil::TransformMesh(m_Mesh, matrix);
		}

		return m_Mesh;
	}

	void Light::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		node->SetModelAABB(m_AABB);
	}

	void Light::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnDetaching(node);
		node->SetModelAABB(BoxBounds());
	}
}