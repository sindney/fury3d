#include "Fury/RenderSettings.h"

#include <algorithm>
#include <cmath>

#include "Fury/Archive.h"
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

	void RenderSettings::Serialize(Archive& ar)
	{
		ar << Optional("pipeline", m_PipelinePath);
		ar << Optional("hdr", m_HDR, false);
		ar << Optional("cascaded_shadow_map", m_CascadedShadowMap, true);
		ar << Optional("vegetation_prez", m_VegetationPreZ, true);
		ar << Optional("csm_map_size", m_CsmMapSize, 2048);
		ar << Optional("shadow_far", m_ShadowFar, 20000.0f);
		ar << Optional("csm_split_blend", m_CsmSplitBlend, 0.7f);
		ar << Optional("wind_params", m_WindParams, Vector4(1.0f, 0.0f, 1.0f, 1.0f));

		// Ordered chain: [{ effect, enabled, uniforms? }, ...]. Entries
		// without a usable "effect" are dropped.
		if (ar.IsLoading())
			m_Chain.clear();
		if (ar.EnterArray("chain", ArchiveFieldPolicy::Optional))
		{
			if (ar.IsLoading())
			{
				std::size_t count = ar.GetArraySize();
				for (std::size_t i = 0; i < count; ++i)
				{
					ar.SetArrayIndex(i);
					if (ar.EnterObject())
					{
						RenderChainEntry e;
						ArchiveFieldResult effectResult = ar.TransferNamed("effect", e.effectName,
							ArchiveFieldPolicy::Optional, true);
						if (effectResult == ArchiveFieldResult::Ok)
						{
							ar << Optional("enabled", e.enabled, true);
							if (ar.EnterArray("uniforms", ArchiveFieldPolicy::Optional))
							{
								std::size_t uniformCount = ar.GetArraySize();
								for (std::size_t j = 0; j < uniformCount; ++j)
								{
									ar.SetArrayIndex(j);
									if (ar.EnterObject())
									{
										std::string uname;
										ArchiveFieldResult nameResult = ar.TransferNamed("name", uname,
											ArchiveFieldPolicy::Optional, true);
										std::vector<float> values;
										ar.TransferNamed("value", values, ArchiveFieldPolicy::Optional, true);
										if (nameResult == ArchiveFieldResult::Ok)
										{
											switch (values.size())
											{
											case 1: e.uniformOverrides[uname] = Uniform1f::Create({values[0]}); break;
											case 2: e.uniformOverrides[uname] = Uniform2f::Create({values[0], values[1]}); break;
											case 3: e.uniformOverrides[uname] = Uniform3f::Create({values[0], values[1], values[2]}); break;
											case 4: e.uniformOverrides[uname] = Uniform4f::Create({values[0], values[1], values[2], values[3]}); break;
											default: break;
											}
										}
										ar.LeaveObject();
									}
								}
								ar.LeaveArray();
							}
							m_Chain.push_back(e);
						}
						ar.LeaveObject();
					}
				}
			}
			else
			{
				std::size_t count = m_Chain.size();
				for (std::size_t i = 0; i < count; ++i)
				{
					ar.EnterObject();
					const auto &e = m_Chain[i];
					std::string effect = e.effectName;
					ar << Required("effect", effect);
					bool enabled = e.enabled;
					ar << Required("enabled", enabled);
					if (!e.uniformOverrides.empty())
					{
						if (ar.EnterArray("uniforms", ArchiveFieldPolicy::Required))
						{
							for (const auto &kv : e.uniformOverrides)
							{
								auto floats = UniformFloats(kv.second);
								if (floats.empty()) continue;
								ar.EnterObject();
								std::string name = kv.first;
								ar << Required("name", name);
								ar << Required("value", floats);
								ar.LeaveObject();
							}
							ar.LeaveArray();
						}
					}
					ar.LeaveObject();
				}
			}
			ar.LeaveArray();
		}
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

	bool RenderSettings::IsVegetationPreZ() const
	{
		return m_VegetationPreZ;
	}

	void RenderSettings::SetVegetationPreZ(bool value)
	{
		m_VegetationPreZ = value;
	}

	int RenderSettings::GetCsmMapSize() const
	{
		return m_CsmMapSize;
	}

	void RenderSettings::SetCsmMapSize(int size)
	{
		// sane cascade sizes only
		if (size == 512 || size == 1024 || size == 2048 || size == 4096)
			m_CsmMapSize = size;
	}

	float RenderSettings::GetShadowFar() const
	{
		return m_ShadowFar;
	}

	void RenderSettings::SetShadowFar(float value)
	{
		m_ShadowFar = value;
	}

	float RenderSettings::GetCsmSplitBlend() const
	{
		return m_CsmSplitBlend;
	}

	void RenderSettings::SetCsmSplitBlend(float blend)
	{
		m_CsmSplitBlend = blend < 0.0f ? 0.0f : (blend > 1.0f ? 1.0f : blend);
	}

	void RenderSettings::ComputeCsmSplits(float nearPlane, float cameraFar, float *outSplits4) const
	{
		// never stretch the cascade range past the camera's far plane
		const float farCap = m_ShadowFar > 0.0f
			? std::min(m_ShadowFar, cameraFar) : cameraFar;
		const float range = farCap - nearPlane;
		const float lambda = GetCsmSplitBlend();
		for (int i = 1; i <= 4; i++)
		{
			const float f = static_cast<float>(i) / 4.0f;
			const float lin = nearPlane + range * f;
			const float log = nearPlane * powf((nearPlane + range) / nearPlane, f);
			outSplits4[i - 1] = lin * (1.0f - lambda) + log * lambda;
		}
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