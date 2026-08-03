#include "Fury/ParticleModules.h"

#include <algorithm>

namespace fury
{
	// ---- Gradient --------------------------------------------------------
	Gradient::Gradient()
	{
		keys.push_back({ 0.0f, Color::White });
		keys.push_back({ 1.0f, Color::White });
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

	bool Gradient::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		keys.clear();
		return LoadArray(wrapper, "keys", [&](const void* node) -> bool
		{
			GradientKey k;
			if (!LoadMemberValue(node, "time", k.time)) return false;
			if (!LoadMemberValue(node, "color", k.color)) return false;
			keys.push_back(k);
			return true;
		});
	}

	void Gradient::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "keys");
		StartArray(wrapper);
		for (const auto& k : keys)
		{
			StartObject(wrapper);
			SaveKey(wrapper, "time"); SaveValue(wrapper, k.time);
			SaveKey(wrapper, "color"); SaveValue(wrapper, k.color);
			EndObject(wrapper);
		}
		EndArray(wrapper);
		if (object) EndObject(wrapper);
	}

	// ---- AnimationCurve --------------------------------------------------
	AnimationCurve::AnimationCurve()
	{
		keys.push_back({ 0.0f, 1.0f });
		keys.push_back({ 1.0f, 1.0f });
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

	bool AnimationCurve::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		keys.clear();
		return LoadArray(wrapper, "keys", [&](const void* node) -> bool
		{
			CurveKey k;
			if (!LoadMemberValue(node, "time", k.time)) return false;
			if (!LoadMemberValue(node, "value", k.value)) return false;
			keys.push_back(k);
			return true;
		});
	}

	void AnimationCurve::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "keys");
		StartArray(wrapper);
		for (const auto& k : keys)
		{
			StartObject(wrapper);
			SaveKey(wrapper, "time"); SaveValue(wrapper, k.time);
			SaveKey(wrapper, "value"); SaveValue(wrapper, k.value);
			EndObject(wrapper);
		}
		EndArray(wrapper);
		if (object) EndObject(wrapper);
	}

	// ---- Burst -----------------------------------------------------------
	bool Burst::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		LoadMemberValue(wrapper, "time", time);
		LoadMemberValue(wrapper, "count", count);
		LoadMemberValue(wrapper, "probability", probability);
		return true;
	}

	void Burst::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "time"); SaveValue(wrapper, time);
		SaveKey(wrapper, "count"); SaveValue(wrapper, count);
		SaveKey(wrapper, "probability"); SaveValue(wrapper, probability);
		if (object) EndObject(wrapper);
	}

	// ---- EmissionModule --------------------------------------------------
	bool EmissionModule::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		LoadMemberValue(wrapper, "rateOverTime", rateOverTime);
		bursts.clear();
		LoadArray(wrapper, "bursts", [&](const void* node) -> bool
		{
			Burst b;
			if (!b.Load(node)) return false;
			bursts.push_back(b);
			return true;
		});
		return true;
	}

	void EmissionModule::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "rateOverTime"); SaveValue(wrapper, rateOverTime);
		SaveKey(wrapper, "bursts");
		StartArray(wrapper);
		for (auto& b : bursts) b.Save(wrapper);
		EndArray(wrapper);
		if (object) EndObject(wrapper);
	}

	// ---- ShapeModule -----------------------------------------------------
	bool ShapeModule::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		unsigned int t = 0;
		LoadMemberValue(wrapper, "type", t);
		type = static_cast<ParticleShape>(t);
		LoadMemberValue(wrapper, "scale", scale);
		LoadMemberValue(wrapper, "radius", radius);
		LoadMemberValue(wrapper, "angle", angle);
		return true;
	}

	void ShapeModule::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "type"); SaveValue(wrapper, static_cast<unsigned int>(type));
		SaveKey(wrapper, "scale"); SaveValue(wrapper, scale);
		SaveKey(wrapper, "radius"); SaveValue(wrapper, radius);
		SaveKey(wrapper, "angle"); SaveValue(wrapper, angle);
		if (object) EndObject(wrapper);
	}

	// ---- VelocityModule --------------------------------------------------
	bool VelocityModule::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		LoadMemberValue(wrapper, "linear", linear);
		LoadMemberValue(wrapper, "speed", speed);
		LoadMemberValue(wrapper, "inheritFromParent", inheritFromParent);
		return true;
	}

	void VelocityModule::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "linear"); SaveValue(wrapper, linear);
		SaveKey(wrapper, "speed"); SaveValue(wrapper, speed);
		SaveKey(wrapper, "inheritFromParent"); SaveValue(wrapper, inheritFromParent);
		if (object) EndObject(wrapper);
	}

	// ---- ColorOverLifetimeModule ----------------------------------------
	bool ColorOverLifetimeModule::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		if (auto w = FindMember(wrapper, "color"))
			color.Load(w, false);
		return true;
	}

	void ColorOverLifetimeModule::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "color");
		color.Save(wrapper, true);
		if (object) EndObject(wrapper);
	}

	// ---- SizeOverLifetimeModule -----------------------------------------
	bool SizeOverLifetimeModule::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		if (auto w = FindMember(wrapper, "size"))
			size.Load(w, false);
		return true;
	}

	void SizeOverLifetimeModule::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "size");
		size.Save(wrapper, true);
		if (object) EndObject(wrapper);
	}

	// ---- RotationOverLifetimeModule -------------------------------------
	bool RotationOverLifetimeModule::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		LoadMemberValue(wrapper, "angularVelocity", angularVelocity);
		return true;
	}

	void RotationOverLifetimeModule::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "angularVelocity"); SaveValue(wrapper, angularVelocity);
		if (object) EndObject(wrapper);
	}

	// ---- RendererModule --------------------------------------------------
	bool RendererModule::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper)) return false;
		LoadMemberValue(wrapper, "materialName", materialName);
		unsigned int bm = 0;
		LoadMemberValue(wrapper, "blendMode", bm);
		blendMode = static_cast<ParticleBlend>(bm);
		LoadMemberValue(wrapper, "receiveShadows", receiveShadows);
		return true;
	}

	void RendererModule::Save(void* wrapper, bool object)
	{
		if (object) StartObject(wrapper);
		SaveKey(wrapper, "materialName"); SaveValue(wrapper, materialName);
		SaveKey(wrapper, "blendMode"); SaveValue(wrapper, static_cast<unsigned int>(blendMode));
		SaveKey(wrapper, "receiveShadows"); SaveValue(wrapper, receiveShadows);
		if (object) EndObject(wrapper);
	}
}