#include "Log.h"
#include "Light.h"
#include "Mesh.h"
#include "MeshUtil.h"
#include "SceneNode.h"

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