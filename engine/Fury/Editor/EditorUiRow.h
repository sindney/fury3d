#ifndef _FURY_EDITOR_UI_ROW_H_
#define _FURY_EDITOR_UI_ROW_H_

#include <algorithm>

#include <imgui.h>

// Row-layout helpers for the inspector (Node Properties + friends).
//
// The rule these enforce: a row never draws past the panel's content width.
// - Field rows put the label on the right when [field + label] fits,
//   otherwise the label moves above and the field takes the full width.
// - Hints never render as a second text run on the row (that was the
//   overflow source) -- they ride the label as a hover tooltip.
//
// Usage:
//   EditorUi::FieldRow("Water Level", 220.0f, [&](float w) {
//       ImGui::SetNextItemWidth(w);
//       if (ImGui::DragFloat("##wl", &v, 1.0f)) { /* apply */ }
//   }, "units: cm, world-space surface height");
//
// The draw callback receives the width to hand to SetNextItemWidth and must
// use a hidden-label widget ("##id") -- the label text is drawn by the row.

namespace EditorUi
{
	// Hover tooltip for a hint on the last emitted item.
	inline void HintTooltip(const char* hint)
	{
		if (hint && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", hint);
	}

	// One field + label. fieldWidth is the preferred widget width (clamped
	// to what fits). The label never overflows: it wraps above the field.
	template <typename F>
	void FieldRow(const char* label, float fieldWidth, F&& draw, const char* hint = nullptr)
	{
		const float avail = ImGui::GetContentRegionAvail().x;
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float labelW = label ? ImGui::CalcTextSize(label).x + spacing : 0.0f;

		if (label && fieldWidth + labelW > avail)
		{
			ImGui::TextUnformatted(label);
			HintTooltip(hint);
			draw(std::max(80.0f, avail));
			return;
		}
		draw(std::min(fieldWidth, avail));
		if (label)
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(label);
			HintTooltip(hint);
		}
	}

	// N equal fields sharing one row with one label (vec3-style groups like
	// "Half Extent"). Same wrap rule as FieldRow.
	template <typename F>
	void FieldRowN(const char* label, int count, F&& draw, const char* hint = nullptr)
	{
		const float avail = ImGui::GetContentRegionAvail().x;
		const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
		const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
		const float labelW = label ? ImGui::CalcTextSize(label).x + spacing : 0.0f;
		const float each = (avail - labelW - itemSpacing * (count - 1)) / count;

		const bool wrap = label && each < 60.0f;  // narrower than usable -> wrap
		if (wrap)
		{
			ImGui::TextUnformatted(label);
			HintTooltip(hint);
			draw((avail - itemSpacing * (count - 1)) / count);
			return;
		}
		draw(each);
		if (label)
		{
			ImGui::SameLine();
			ImGui::TextUnformatted(label);
			HintTooltip(hint);
		}
	}

	// A checkbox row. Checked boxes keep their built-in label; the hint
	// becomes a hover tooltip instead of a trailing text run.
	inline bool CheckboxRow(const char* label, bool* value, const char* hint = nullptr)
	{
		bool changed = ImGui::Checkbox(label, value);
		HintTooltip(hint);
		return changed;
	}

	// A full-width button row (fits by construction).
	inline bool ButtonRow(const char* label, const char* hint = nullptr)
	{
		bool hit = ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
		HintTooltip(hint);
		return hit;
	}
}

#endif // _FURY_EDITOR_UI_ROW_H_
