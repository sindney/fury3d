#ifndef _FURY_EDITOR_REFLECT_H_
#define _FURY_EDITOR_REFLECT_H_

// Engine public headers must NOT include this file. New adapters belong here,
// not in engine/Fury/<Type>.h. This file is editor-only and only compiles
// when WITH_EDITOR is defined.
//
// Provides ImReflect tag_invoke overloads for fury value types so the Node
// Properties panel can call ImReflect::Input(label, value) without a parallel
// reflected facade struct.

#ifdef WITH_EDITOR

#include "Fury/Color.h"
#include "Fury/EnumUtil.h"
#include "Fury/MathUtil.h"
#include "Fury/Quaternion.h"
#include "Fury/Vector4.h"

#include "ImReflect.hpp"

namespace fury
{
	// Vector4 — edit X/Y/Z as a draggable 3-tuple. The w component is left
	// untouched so callers can pass position/scale vectors unchanged.
	inline void tag_invoke(ImReflect::ImInput_t,
		const char* label,
		Vector4& value,
		ImSettings& /*settings*/,
		ImResponse& response)
	{
		auto& r = response.get<Vector4>();
		float v[3] = { value.x, value.y, value.z };
		if (ImGui::DragFloat3(label, v, 0.05f))
		{
			value.x = v[0];
			value.y = v[1];
			value.z = v[2];
			r.changed();
		}
		ImReflect::Detail::check_input_states(r);
	}

	// Color — RGBA picker. Color stores floats in 0..1 already.
	inline void tag_invoke(ImReflect::ImInput_t,
		const char* label,
		Color& value,
		ImSettings& /*settings*/,
		ImResponse& response)
	{
		auto& r = response.get<Color>();
		float v[4] = { value.r, value.g, value.b, value.a };
		if (ImGui::ColorEdit4(label, v))
		{
			value.r = v[0];
			value.g = v[1];
			value.b = v[2];
			value.a = v[3];
			r.changed();
		}
		ImReflect::Detail::check_input_states(r);
	}

	// Quaternion — render as Euler XYZ in degrees, store back as Quaternion.
	// Internal representation rebuilt from the Quaternion every frame so
	// successive edit sessions stay consistent.
	inline void tag_invoke(ImReflect::ImInput_t,
		const char* label,
		Quaternion& value,
		ImSettings& /*settings*/,
		ImResponse& response)
	{
		auto& r = response.get<Quaternion>();
		Vector4 eulerRad = MathUtil::QuatToEulerRad(value);
		float deg[3] = {
			eulerRad.x * MathUtil::RadToDeg,
			eulerRad.y * MathUtil::RadToDeg,
			eulerRad.z * MathUtil::RadToDeg };
		if (ImGui::DragFloat3(label, deg, 0.5f))
		{
			value = MathUtil::EulerRadToQuat(
				deg[0] * MathUtil::DegToRad,
				deg[1] * MathUtil::DegToRad,
				deg[2] * MathUtil::DegToRad);
			r.changed();
		}
		ImReflect::Detail::check_input_states(r);
	}

	// LightType enum — combo using EnumUtil::LightTypeToString for labels.
	inline void tag_invoke(ImReflect::ImInput_t,
		const char* label,
		LightType& value,
		ImSettings& /*settings*/,
		ImResponse& response)
	{
		auto& r = response.get<LightType>();
		static const LightType kAll[] = {
			LightType::DIRECTIONAL, LightType::POINT, LightType::SPOT };
		const std::string preview_str = EnumUtil::LightTypeToString(value);

		if (ImGui::BeginCombo(label, preview_str.c_str()))
		{
			for (LightType t : kAll)
			{
				bool is_selected = (t == value);
				const std::string name = EnumUtil::LightTypeToString(t);
				if (ImGui::Selectable(name.c_str(), is_selected))
				{
					if (t != value)
					{
						value = t;
						r.changed();
					}
				}
				if (is_selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImReflect::Detail::check_input_states(r);
	}
}

#endif // WITH_EDITOR

#endif // _FURY_EDITOR_REFLECT_H_
