#include "Fury/ParticleModules.h"

#include <algorithm>

#include "Fury/Archive.h"

namespace fury
{
	// ---- Gradient --------------------------------------------------------
	Gradient::Gradient()
	{
		keys.push_back({ 0.0f, Color::White });
		keys.push_back({ 1.0f, Color::White });
	}

	void Gradient::Serialize(Archive& ar)
	{
		if (ar.IsLoading())
			keys.clear();
		if (!ar.EnterArray("keys", ArchiveFieldPolicy::Optional))
			return;
		std::size_t count = ar.IsLoading() ? ar.GetArraySize() : keys.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			if (ar.IsLoading())
			{
				ar.SetArrayIndex(i);
				keys.emplace_back();
			}
			GradientKey &k = keys[i];
			if (ar.EnterObject())
			{
				ar << Required("time", k.time);
				ar << Required("color", k.color);
				ar.LeaveObject();
			}
		}
		ar.LeaveArray();
	}

	Color Gradient::Sample(float time01) const
	{
		if (keys.empty()) return Color::White;
		if (keys.size() == 1) return keys.front().color;

		const float t = std::min(1.0f, std::max(0.0f, time01));
		for (size_t i = 1; i < keys.size(); ++i)
		{
			if (keys[i].time >= t)
			{
				const auto& a = keys[i - 1];
				const auto& b = keys[i];
				const float span = std::max(1e-6f, b.time - a.time);
				const float local = (t - a.time) / span;
				return Color(
					a.color.r + (b.color.r - a.color.r) * local,
					a.color.g + (b.color.g - a.color.g) * local,
					a.color.b + (b.color.b - a.color.b) * local,
					a.color.a + (b.color.a - a.color.a) * local);
			}
		}
		return keys.back().color;
	}

	// ---- AnimationCurve --------------------------------------------------
	AnimationCurve::AnimationCurve()
	{
		keys.push_back({ 0.0f, 1.0f });
		keys.push_back({ 1.0f, 1.0f });
	}

	void AnimationCurve::Serialize(Archive& ar)
	{
		if (ar.IsLoading())
			keys.clear();
		if (!ar.EnterArray("keys", ArchiveFieldPolicy::Optional))
			return;
		std::size_t count = ar.IsLoading() ? ar.GetArraySize() : keys.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			if (ar.IsLoading())
			{
				ar.SetArrayIndex(i);
				keys.emplace_back();
			}
			CurveKey &k = keys[i];
			if (ar.EnterObject())
			{
				ar << Required("time", k.time);
				ar << Required("value", k.value);
				ar.LeaveObject();
			}
		}
		ar.LeaveArray();
	}

	float AnimationCurve::Sample(float time01) const
	{
		if (keys.empty()) return 1.0f;
		if (keys.size() == 1) return keys.front().value;

		const float t = std::min(1.0f, std::max(0.0f, time01));
		for (size_t i = 1; i < keys.size(); ++i)
		{
			if (keys[i].time >= t)
			{
				const auto& a = keys[i - 1];
				const auto& b = keys[i];
				const float span = std::max(1e-6f, b.time - a.time);
				const float local = (t - a.time) / span;
				return a.value + (b.value - a.value) * local;
			}
		}
		return keys.back().value;
	}

	// ---- Burst -----------------------------------------------------------
	void Burst::Serialize(Archive& ar)
	{
		ar << Optional("time", time, 0.0f);
		ar << Optional("count", count, 0);
		ar << Optional("probability", probability, 1.0f);
	}

	// ---- EmissionModule --------------------------------------------------
	void EmissionModule::Serialize(Archive& ar)
	{
		ar << Optional("rateOverTime", rateOverTime, 0.0f);
		if (ar.IsLoading())
			bursts.clear();
		if (ar.EnterArray("bursts", ArchiveFieldPolicy::Optional))
		{
			std::size_t count = ar.IsLoading() ? ar.GetArraySize() : bursts.size();
			for (std::size_t i = 0; i < count; ++i)
			{
				if (ar.IsLoading())
				{
					ar.SetArrayIndex(i);
					bursts.emplace_back();
					if (ar.EnterObject())
					{
						bursts.back().Serialize(ar);
						ar.LeaveObject();
					}
				}
				else
				{
					ar.EnterObject();
					bursts[i].Serialize(ar);
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}
	}

	// ---- ShapeModule -----------------------------------------------------
	void ShapeModule::Serialize(Archive& ar)
	{
		unsigned int shapeType = static_cast<unsigned int>(type);
		ar << Optional("type", shapeType, 0u);
		ar << Optional("scale", scale);
		ar << Optional("radius", radius, 0.5f);
		ar << Optional("angle", angle, 25.0f);
		if (ar.IsLoading())
			type = static_cast<ParticleShape>(shapeType);
	}

	// ---- VelocityModule --------------------------------------------------
	void VelocityModule::Serialize(Archive& ar)
	{
		ar << Optional("linear", linear);
		ar << Optional("speed", speed, 1.0f);
		ar << Optional("inheritFromParent", inheritFromParent, false);
	}

	// ---- ColorOverLifetimeModule ----------------------------------------
	void ColorOverLifetimeModule::Serialize(Archive& ar)
	{
		if (ar.EnterObject("color", ArchiveFieldPolicy::Optional))
		{
			color.Serialize(ar);
			ar.LeaveObject();
		}
	}

	// ---- SizeOverLifetimeModule -----------------------------------------
	void SizeOverLifetimeModule::Serialize(Archive& ar)
	{
		if (ar.EnterObject("size", ArchiveFieldPolicy::Optional))
		{
			size.Serialize(ar);
			ar.LeaveObject();
		}
	}

	// ---- RotationOverLifetimeModule -------------------------------------
	void RotationOverLifetimeModule::Serialize(Archive& ar)
	{
		ar << Optional("angularVelocity", angularVelocity, 0.0f);
	}

	// ---- RendererModule --------------------------------------------------
	void RendererModule::Serialize(Archive& ar)
	{
		ar << Optional("materialName", materialName);
		unsigned int blend = static_cast<unsigned int>(blendMode);
		ar << Optional("blendMode", blend, 0u);
		ar << Optional("receiveShadows", receiveShadows, true);
		if (ar.IsLoading())
			blendMode = static_cast<ParticleBlend>(blend);
	}
}