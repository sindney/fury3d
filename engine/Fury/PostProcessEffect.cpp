#include "Fury/PostProcessEffect.h"

#include "Fury/Log.h"
#include "Fury/Serializable.h"

namespace fury
{
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

	bool PostProcessEffect::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "PostProcessEffect: Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		std::string str;
		if (LoadMemberValue(wrapper, "shader", str))
			m_ShaderPath = str;

		// Optional defines array (mirrors Shader::Load's defines parsing).
		if (auto defs = FindMember(wrapper, "defines"))
		{
			LoadArray(defs, [&](const void* node) -> bool
			{
				std::string d;
				if (LoadValue(node, d))
					m_ShaderDefines.push_back(d);
				return true;
			});
		}

		// inputs: ordered list of declared input texture names. Mirrors
		// the way pipeline shaders declare textures.
		if (auto ins = FindMember(wrapper, "inputs"))
		{
			LoadArray(ins, [&](const void* node) -> bool
			{
				std::string in;
				if (LoadValue(node, in))
					m_Inputs.push_back(in);
				return true;
			});
		}

		// output format: defaults to RGBA8; ACES uses rgba16f to
		// receive HDR input.
		str.clear();
		if (LoadMemberValue(wrapper, "output_format", str))
		{
			auto fmt = EnumUtil::TextureFormatFromString(str);
			if (fmt != TextureFormat::UNKNOW)
				m_OutputFormat = fmt;
		}

		int w = 0, h = 0;
		if (LoadMemberValue(wrapper, "output_width", w))
			m_OutputWidth = (unsigned int)w;
		if (LoadMemberValue(wrapper, "output_height", h))
			m_OutputHeight = (unsigned int)h;

		// Optional defaults object: { "u_name": [v0, v1, v2, ...] }.
		// The size of the float array picks the Uniform1f/2f/3f/4f
		// template instantiation. Missing defaults are ignored.
		if (auto uniformsNode = FindMember(wrapper, "uniforms"))
		{
			LoadArray(uniformsNode, [&](const void* node) -> bool
			{
				if (!IsObject(node)) return true;
				std::string uname;
				if (!LoadMemberValue(node, "name", uname)) return true;
				std::vector<float> values;
				// Load the "value" member as a typed float array
				// (the LoadMemberValue overloads in Serializable
				// cover scalar + Color + Vector4, not arrays; for
				// the array form we use LoadArray with the typed
				// float template, which reads every element).
				if (auto v = FindMember(node, "value"))
				{
					if (!LoadArray<float>(v, values)) return true;
				}
				switch (values.size())
				{
				case 1: SetUniform(uname, Uniform1f::Create({values[0]})); break;
				case 2: SetUniform(uname, Uniform2f::Create({values[0], values[1]})); break;
				case 3: SetUniform(uname, Uniform3f::Create({values[0], values[1], values[2]})); break;
				case 4: SetUniform(uname, Uniform4f::Create({values[0], values[1], values[2], values[3]})); break;
				default:
					FURYW << "PostProcessEffect '" << m_Name << "': uniform '" << uname
						  << "' has unsupported arity " << values.size();
					break;
				}
				return true;
			});
		}

		return true;
	}

	void PostProcessEffect::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "shader");
		SaveValue(wrapper, m_ShaderPath);

		if (!m_ShaderDefines.empty())
		{
			SaveKey(wrapper, "defines");
			StartArray(wrapper);
			for (const auto &d : m_ShaderDefines)
				SaveValue(wrapper, d);
			EndArray(wrapper);
		}

		if (!m_Inputs.empty())
		{
			SaveKey(wrapper, "inputs");
			StartArray(wrapper);
			for (const auto &i : m_Inputs)
				SaveValue(wrapper, i);
			EndArray(wrapper);
		}

		SaveKey(wrapper, "output_format");
		SaveValue(wrapper, EnumUtil::TextureFormatToString(m_OutputFormat));

		if (m_OutputWidth > 0)
		{
			SaveKey(wrapper, "output_width");
			SaveValue(wrapper, (int)m_OutputWidth);
		}
		if (m_OutputHeight > 0)
		{
			SaveKey(wrapper, "output_height");
			SaveValue(wrapper, (int)m_OutputHeight);
		}

		if (!m_Uniforms.empty())
		{
			SaveKey(wrapper, "uniforms");
			StartArray(wrapper);
			for (const auto &kv : m_Uniforms)
			{
				StartObject(wrapper);
				SaveKey(wrapper, "name");
				SaveValue(wrapper, kv.first);
				SaveKey(wrapper, "value");
				kv.second->Save(wrapper);
				EndObject(wrapper);
			}
			EndArray(wrapper);
		}

		if (object)
			EndObject(wrapper);
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
}