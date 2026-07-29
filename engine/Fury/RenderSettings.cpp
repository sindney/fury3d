#include "Fury/RenderSettings.h"

#include "Fury/Log.h"
#include "Fury/Uniform.h"

namespace fury
{
	// float1-4 overrides only (postprocess uniforms never use int/
	// matrix types in practice). Empty vector = unsupported type.
	static std::vector<float> UniformFloats(const UniformBase::Ptr &u)
	{
		if (!u) return {};
		if (auto p = std::dynamic_pointer_cast<Uniform1f>(u))
			return { p->GetDataAt(0) };
		if (auto p = std::dynamic_pointer_cast<Uniform2f>(u))
			return { p->GetDataAt(0), p->GetDataAt(1) };
		if (auto p = std::dynamic_pointer_cast<Uniform3f>(u))
			return { p->GetDataAt(0), p->GetDataAt(1), p->GetDataAt(2) };
		if (auto p = std::dynamic_pointer_cast<Uniform4f>(u))
			return { p->GetDataAt(0), p->GetDataAt(1), p->GetDataAt(2), p->GetDataAt(3) };
		return {};
	}
	RenderSettings::RenderSettings()
	{
	}

	bool RenderSettings::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "RenderSettings: Json node is not an object!";
			return false;
		}

		std::string str;
		if (LoadMemberValue(wrapper, "pipeline", str))
			m_PipelinePath = str;

		bool b = m_HDR;
		if (LoadMemberValue(wrapper, "hdr", b))
			m_HDR = b;
		else
			m_HDR = false;

		b = m_CascadedShadowMap;
		if (LoadMemberValue(wrapper, "cascaded_shadow_map", b))
			m_CascadedShadowMap = b;
		else
			m_CascadedShadowMap = true; // legacy default: CSM on

		m_Chain.clear();
		// Ordered chain: [{ effect, enabled, uniforms? }, ...]. Missing /
		// empty chain loads as empty (legacy compatible); entries without
		// "uniforms" get empty overrides (effect defaults).
		LoadArray(wrapper, "chain", [&](const void* node) -> bool
		{
			RenderChainEntry e;
			if (LoadMemberValue(node, "effect", e.effectName))
			{
				bool en = true;
				LoadMemberValue(node, "enabled", en);
				e.enabled = en;
				if (auto uniformsNode = FindMember(node, "uniforms"))
					LoadArray(uniformsNode, [&](const void* unode) -> bool
					{
						// {"name","value":[...]} — same shape as
						// PostProcessEffect descriptor defaults.
						if (!IsObject(unode)) return true;
						std::string uname;
						if (!LoadMemberValue(unode, "name", uname)) return true;
						std::vector<float> values;
						if (auto v = FindMember(unode, "value"))
							if (!LoadArray<float>(v, values)) return true;
						switch (values.size())
						{
						case 1: e.uniformOverrides[uname] = Uniform1f::Create({values[0]}); break;
						case 2: e.uniformOverrides[uname] = Uniform2f::Create({values[0], values[1]}); break;
						case 3: e.uniformOverrides[uname] = Uniform3f::Create({values[0], values[1], values[2]}); break;
						case 4: e.uniformOverrides[uname] = Uniform4f::Create({values[0], values[1], values[2], values[3]}); break;
						default: break;
						}
						return true;
					});
				m_Chain.push_back(e);
			}
			return true;
		});

		return true;
	}

	void RenderSettings::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		SaveKey(wrapper, "pipeline");
		SaveValue(wrapper, m_PipelinePath);

		SaveKey(wrapper, "hdr");
		SaveValue(wrapper, m_HDR);

		SaveKey(wrapper, "cascaded_shadow_map");
		SaveValue(wrapper, m_CascadedShadowMap);

		SaveKey(wrapper, "chain");
		StartArray(wrapper);
		for (const auto &e : m_Chain)
		{
			StartObject(wrapper);
			SaveKey(wrapper, "effect");
			SaveValue(wrapper, e.effectName);
			SaveKey(wrapper, "enabled");
			SaveValue(wrapper, e.enabled);
			if (!e.uniformOverrides.empty())
			{
				SaveKey(wrapper, "uniforms");
				StartArray(wrapper);
				for (const auto &kv : e.uniformOverrides)
				{
					auto floats = UniformFloats(kv.second);
					if (floats.empty()) continue;
					StartObject(wrapper);
					SaveKey(wrapper, "name");
					SaveValue(wrapper, kv.first);
					SaveKey(wrapper, "value");
					StartArray(wrapper);
					for (float f : floats)
						SaveValue(wrapper, f);
					EndArray(wrapper);
					EndObject(wrapper);
				}
				EndArray(wrapper);
			}
			EndObject(wrapper);
		}
		EndArray(wrapper);

		if (object)
			EndObject(wrapper);
	}

	const std::string &RenderSettings::GetPipelinePath() const
	{
		return m_PipelinePath;
	}

	void RenderSettings::SetPipelinePath(const std::string &path)
	{
		m_PipelinePath = path;
	}

	bool RenderSettings::IsHDR() const
	{
		return m_HDR;
	}

	void RenderSettings::SetHDR(bool value)
	{
		m_HDR = value;
	}

	bool RenderSettings::IsCascadedShadowMap() const
	{
		return m_CascadedShadowMap;
	}

	void RenderSettings::SetCascadedShadowMap(bool value)
	{
		m_CascadedShadowMap = value;
	}

	const std::vector<RenderChainEntry> &RenderSettings::GetChain() const
	{
		return m_Chain;
	}

	std::vector<RenderChainEntry> &RenderSettings::GetChainMutable()
	{
		return m_Chain;
	}

	void RenderSettings::ClearChain()
	{
		m_Chain.clear();
	}

	void RenderSettings::CopyChainFrom(const RenderSettings &other)
	{
		m_Chain = other.m_Chain;
	}

	void RenderSettings::AddEffect(const std::string &effectName, bool enabled)
	{
		// Append (chain order = render order). The editor / chain
		// reorder UI drives MoveEffect for reorders.
		RenderChainEntry e;
		e.effectName = effectName;
		e.enabled = enabled;
		m_Chain.push_back(e);
	}

	void RenderSettings::RemoveEffect(const std::string &effectName)
	{
		for (auto it = m_Chain.begin(); it != m_Chain.end(); ++it)
		{
			if (it->effectName == effectName)
			{
				m_Chain.erase(it);
				return;
			}
		}
	}

	void RenderSettings::MoveEffect(unsigned int srcIndex, unsigned int dstIndex)
	{
		if (srcIndex >= m_Chain.size() || dstIndex >= m_Chain.size()) return;
		if (srcIndex == dstIndex) return;
		auto entry = m_Chain[srcIndex];
		m_Chain.erase(m_Chain.begin() + srcIndex);
		m_Chain.insert(m_Chain.begin() + dstIndex, entry);
	}

	void RenderSettings::SetEffectEnabled(unsigned int index, bool enabled)
	{
		if (index >= m_Chain.size()) return;
		m_Chain[index].enabled = enabled;
	}
}