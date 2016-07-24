#ifndef _FURY_LIGHT_H_
#define _FURY_LIGHT_H_

#include "Fury/Component.h"
#include "Fury/BoxBounds.h"
#include "Fury/Color.h"
#include "Fury/EnumUtil.h"

namespace fury
{
	class Mesh;

	// light's default direction is (0,-1,0,0)
	class FURY_API Light : public Component
	{
	public:

		typedef std::shared_ptr<Light> Ptr;

		static Ptr Create();

	private:

		LightType m_Type = LightType::POINT;

		Color m_Color = Color::White;

		float m_Intensity = 1.0f;

		float m_InnerAngle = 45.0f;

		float m_OutterAngle = 45.0f;

		float m_Falloff = 0.0f;

		float m_Radius = 0.0f;

		bool m_CastShadows = false;

		BoxBounds m_AABB;

		std::shared_ptr<Mesh> m_Mesh;

	public:

		Light();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		Component::Ptr Clone() const override;

		LightType GetType() const;

		void SetType(LightType type);

		void SetColor(Color color);

		Color GetColor() const;

		void SetIntensity(float value);

		float GetIntensity() const;

		// in radian
		void SetInnerAngle(float value);

		float GetInnerAngle() const;

		// in radian
		void SetOutterAngle(float value);

		float GetOutterAngle() const;

		void SetFalloff(float value);

		float GetFalloff() const;

		void SetRadius(float value);

		float GetRadius() const;

		void SetCastShadows(bool cast);

		bool GetCastShadows() const;

		BoxBounds GetAABB() const;

		void CalculateAABB();

		// Convex Volume that defines light's shape.
		std::shared_ptr<Mesh> GetMesh();

		// Create a mesh shaped by light's params.
		std::shared_ptr<Mesh> EvaluateVolume();

	protected:

		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;

		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;
	};
}

#endif // _FURY_LIGHT_H_