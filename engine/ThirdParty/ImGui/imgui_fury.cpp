#include <map>
#include <cstddef> // offsetof
#include <algorithm>
#include <string>

#include "ImGui/imgui.h"
#include "ImGui/imgui_fury.h"

namespace ImGui 
{
	struct PlotVarData
	{
	    ImGuiID        	ID;
	    ImVector<float>	Data;
	    int            	DataInsertIdx;
	    int            	LastFrame;

	    PlotVarData() : ID(0), DataInsertIdx(0), LastFrame(-1) {}
	};

	typedef std::map<ImGuiID, PlotVarData> PlotVarsMap;
	static PlotVarsMap  g_PlotVarsMap;

	// Plot value over time
	// Call with 'value == FLT_MAX' to draw without adding new value to the buffer
	void PlotVar(const char* label, float value, float scale_min, float scale_max, float width, float height, size_t buffer_size)
	{
	    IM_ASSERT(label);
	    if (buffer_size == 0)
	        buffer_size = 120;

	    ImGui::PushID(label);
	    ImGuiID id = ImGui::GetID("");

	    // Lookup O(log N)
	    PlotVarData& pvd = g_PlotVarsMap[id];

	    // Setup
	    if (pvd.Data.capacity() != buffer_size)
	    {
	        pvd.Data.resize(buffer_size);
	        memset(&pvd.Data[0], 0, sizeof(float) * buffer_size);
	        pvd.DataInsertIdx = 0;
	        pvd.LastFrame = -1;
	    }

	    // Insert (avoid unnecessary modulo operator)
	    if (pvd.DataInsertIdx == buffer_size)
	        pvd.DataInsertIdx = 0;
	    int display_idx = pvd.DataInsertIdx;
	    if (value != FLT_MAX)
	        pvd.Data[pvd.DataInsertIdx++] = value;

	    // Draw
	    int current_frame = ImGui::GetFrameCount();
	    if (pvd.LastFrame != current_frame)
	    {
	    	std::string hint = std::string(label) + " " + std::to_string((int)value);
	        ImGui::PlotLines("##plot", &pvd.Data[0], buffer_size, pvd.DataInsertIdx, hint.c_str(), scale_min, scale_max, ImVec2(width, height));
	        pvd.LastFrame = current_frame;
	    }

	    ImGui::PopID();
	}

	void PlotVarFlushOldEntries()
	{
	    int current_frame = ImGui::GetFrameCount();
	    for (PlotVarsMap::iterator it = g_PlotVarsMap.begin(); it != g_PlotVarsMap.end(); )
	    {
	        PlotVarData& pvd = it->second;
	        if (pvd.LastFrame < current_frame - std::max(400,(int)pvd.Data.size()))
	            it = g_PlotVarsMap.erase(it);
	        else
	            ++it;
	    }
	}
}