#include "Fury/PostProcessEffect.h"

#include <cmath>
#include <map>

#include "Fury/Archive.h"

#include "Fury/Log.h"
#include "Fury/Serializable.h"
#include "Fury/Shader.h"

namespace fury
{
	namespace
	{
		std::vector<float> UniformFloats(const UniformBase::Ptr& uniform)
		{
			if (!uniform) return {};
			if (auto p = std::dynamic_pointer_cast<Uniform1f>(uniform)) return { p->GetDataAt(0) };
			if (auto p = std::dynamic_pointer_cast<Uniform2f>(uniform)) return { p->GetDataAt(0), p->GetDataAt(1) };
			if (auto p = std::dynamic_pointer_cast<Uniform3f>(uniform)) return { p->GetDataAt(0), p->GetDataAt(1), p->GetDataAt(2) };
			if (auto p = std::dynamic_pointer_cast<Uniform4f>(uniform)) return { p->GetDataAt(0), p->GetDataAt(1), p->GetDataAt(2), p->GetDataAt(3) };
			return {};
		}
	}

	PostProcessEffect::Ptr PostProcessEffect::Create(const std::string &name)
	{
		return std::make_shared<PostProcessEffect>(name);
	}

	PostProcessEffect::PostProcessEffect(const std::string &name)
		: Entity(name)
	{
		m_TypeIndex = typeid(PostProcessEffect);
	}

	PostProcessEffect::~PostProcessEffect()
	{
	}

	void PostProcessEffect::Serialize(Archive& ar)
	{
		Entity::SerializeFields(ar);
		if (ar.IsLoading())
		{
			m_ShaderPath.clear();
			m_ShaderDefines.clear();
			m_Inputs.clear();
			m_OutputFormat = TextureFormat::RGBA8;
			m_Stage = PostProcessStage::POST_TONEMAP;
			m_Order = 0;
			m_OutputWidth = m_OutputHeight = 0;
			m_Uniforms.clear();
			m_UniformMeta.clear();
			m_Description.clear();
		}

		ar << OptionalPathReference<Shader>("shader", m_ShaderPath);
		ar << Conditional("defines", m_ShaderDefines, !m_ShaderDefines.empty());
		ar << Conditional("inputs", m_Inputs, !m_Inputs.empty());
		std::string outputFormat = EnumUtil::TextureFormatToString(m_OutputFormat);
		std::string stage = ar.IsLoading() ? "" : EnumUtil::PostProcessStageToString(m_Stage);
		ar << Optional("output_format", outputFormat);
		ar << Optional("stage", stage);
		ar << Conditional("order", m_Order, m_Order != 0);
		int outputWidth = static_cast<int>(m_OutputWidth);
		int outputHeight = static_cast<int>(m_OutputHeight);
		ar << Conditional("output_width", outputWidth, outputWidth > 0);
		ar << Conditional("output_height", outputHeight, outputHeight > 0);
		ar << Conditional("description", m_Description, !m_Description.empty());
		if (ar.IsLoading())
		{
			if (outputWidth < 0 || outputHeight < 0)
			{
				ar.SetError("output dimensions must be non-negative");
				return;
			}
			m_OutputWidth = static_cast<unsigned int>(outputWidth);
			m_OutputHeight = static_cast<unsigned int>(outputHeight);
			auto format = EnumUtil::TextureFormatFromString(outputFormat);
			if (format != TextureFormat::UNKNOW) m_OutputFormat = format;
			if (!stage.empty())
			{
				m_Stage = EnumUtil::PostProcessStageFromString(stage);
			}
			else
			{
				for (const auto& input : m_Inputs)
					if (input.rfind("$gbuffer", 0) == 0) m_Stage = PostProcessStage::PRE_TONEMAP;
			}
		}

		if (!ar.EnterArray("uniforms", ArchiveFieldPolicy::Conditional, !m_Uniforms.empty())) return;
		if (ar.IsLoading())
		{
			for (size_t i = 0; i < ar.GetArraySize(); ++i)
			{
				ar.SetArrayIndex(i);
				if (!ar.EnterObject()) continue;
				std::string name;
				std::vector<float> values;
				UniformMeta meta;
				float min = NAN, max = NAN;
				ar << Required("name", name);
				ar << Optional("value", values);
				ar << Optional("tip", meta.tip);
				ar << Optional("min", min);
				ar << Optional("max", max);
				if (std::isfinite(min) && std::isfinite(max))
				{
					meta.min = min;
					meta.max = max;
					meta.hasRange = true;
				}
				switch (values.size())
				{
				case 1: SetUniform(name, Uniform1f::Create({ values[0] })); break;
				case 2: SetUniform(name, Uniform2f::Create({ values[0], values[1] })); break;
				case 3: SetUniform(name, Uniform3f::Create({ values[0], values[1], values[2] })); break;
				case 4: SetUniform(name, Uniform4f::Create({ values[0], values[1], values[2], values[3] })); break;
				default: ar.SetError("uniform value must contain one to four floats"); break;
				}
				if (!meta.tip.empty() || meta.hasRange) m_UniformMeta[name] = meta;
				ar.LeaveObject();
			}
		}
		else
		{
			std::map<std::string, UniformBase::Ptr> sorted(m_Uniforms.begin(), m_Uniforms.end());
			for (const auto& kv : sorted)
			{
				if (!ar.EnterObject()) continue;
				std::string name = kv.first;
				std::vector<float> values = UniformFloats(kv.second);
				ar << Required("name", name);
				ar << Required("value", values);
				if (values.empty()) ar.SetError("unsupported uniform type");
				UniformMeta meta;
				if (const auto* existing = GetUniformMeta(name)) meta = *existing;
				ar << Conditional("tip", meta.tip, !meta.tip.empty());
				ar << Conditional("min", meta.min, meta.hasRange);
				ar << Conditional("max", meta.max, meta.hasRange);
				ar.LeaveObject();
			}
		}
		ar.LeaveArray();
	}


	const std::string &PostProcessEffect::GetShaderPath() const
	{
		return m_ShaderPath;
	}

	void PostProcessEffect::SetShaderPath(const std::string &path)
	{
		m_ShaderPath = path;
	}

	const std::vector<std::string> &PostProcessEffect::GetShaderDefines() const
	{
		return m_ShaderDefines;
	}

	void PostProcessEffect::AddShaderDefine(const std::string &define)
	{
		m_ShaderDefines.push_back(define);
	}

	const std::vector<std::string> &PostProcessEffect::GetInputs() const
	{
		return m_Inputs;
	}

	void PostProcessEffect::AddInput(const std::string &name)
	{
		m_Inputs.push_back(name);
	}

	TextureFormat PostProcessEffect::GetOutputFormat() const
	{
		return m_OutputFormat;
	}

	void PostProcessEffect::SetOutputFormat(TextureFormat fmt)
	{
		m_OutputFormat = fmt;
	}

	PostProcessStage PostProcessEffect::GetStage() const
	{
		return m_Stage;
	}

	void PostProcessEffect::SetStage(PostProcessStage stage)
	{
		m_Stage = stage;
	}

	int PostProcessEffect::GetOrder() const
	{
		return m_Order;
	}

	void PostProcessEffect::SetOrder(int order)
	{
		m_Order = order;
	}

	unsigned int PostProcessEffect::GetOutputWidth() const
	{
		return m_OutputWidth;
	}

	unsigned int PostProcessEffect::GetOutputHeight() const
	{
		return m_OutputHeight;
	}

	void PostProcessEffect::SetOutputSize(unsigned int w, unsigned int h)
	{
		m_OutputWidth = w;
		m_OutputHeight = h;
	}

	const std::unordered_map<std::string, std::shared_ptr<UniformBase>> &PostProcessEffect::GetUniforms() const
	{
		return m_Uniforms;
	}

	void PostProcessEffect::SetUniform(const std::string &name, const std::shared_ptr<UniformBase> &ptr)
	{
		m_Uniforms[name] = ptr;
	}

	std::shared_ptr<UniformBase> PostProcessEffect::GetUniform(const std::string &name) const
	{
		auto it = m_Uniforms.find(name);
		if (it == m_Uniforms.end()) return nullptr;
		return it->second;
	}

	const UniformMeta *PostProcessEffect::GetUniformMeta(const std::string &name) const
	{
		auto it = m_UniformMeta.find(name);
		if (it == m_UniformMeta.end()) return nullptr;
		return &it->second;
	}

	const std::string &PostProcessEffect::GetDescription() const
	{
		return m_Description;
	}
}