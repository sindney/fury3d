#include "Fury/Camera.h"
#include "Fury/Log.h"
#include "Fury/Pass.h"
#include "Fury/RenderThread.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/EnumUtil.h"
#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/RHIDevice.h"
#include "Fury/EntityManager.h"
#include "Fury/Pipeline.h"
#include "Fury/RenderTarget.h"
#include "Fury/InputUtil.h"

namespace fury
{
	namespace
	{
		RHICommandList *Cmd()
		{
			return GRHIDevice != nullptr ? GRHIDevice->GetCommandList() : nullptr;
		}

		bool IsDepthFormat(TextureFormat format)
		{
			return format == TextureFormat::DEPTH16
				|| format == TextureFormat::DEPTH24
				|| format == TextureFormat::DEPTH32F
				|| format == TextureFormat::DEPTH24_STENCIL8
				|| format == TextureFormat::DEPTH32F_STENCIL8;
		}
	}

	Pass::Ptr Pass::Create(const std::string &name)
	{
		return std::make_shared<Pass>(name);
	}

	Pass::Pass(const std::string &name) : Entity(name)
	{
		m_TypeIndex = typeid(Pass);
	}

	Pass::~Pass()
	{
		DestroyRenderTarget();
	}

	bool Pass::Load(const void* wrapper, bool object)
	{
		if (Pipeline::Active == nullptr)
		{
			FURYE << "Active Pipeline is null!";
			return false;
		}

		auto entityMgr = Pipeline::Active->GetEntityManager();
		std::string str;

		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Entity::Load(wrapper, false))
			return false;

		if (LoadMemberValue(wrapper, "clearMode", str))
			SetClearMode(EnumUtil::ClearModeFromString(str));
		else
			SetClearMode(ClearMode::COLOR_DEPTH_STENCIL);

		std::vector<float> color;
		LoadArray(wrapper, "clearColor", [&](const void* node) -> bool
		{
			float value;
			if (!LoadValue(node, value))
			{
				FURYE << "clearColor is a 4 float array!";
				return false;
			}
			color.push_back(value);
			return true;
		});
		while (color.size() < 4) color.push_back(0);
		SetClearColor(Color(color[0], color[1], color[2], color[3]));

		if (LoadMemberValue(wrapper, "blendMode", str))
			SetBlendMode(EnumUtil::BlendModeFromString(str));
		else
			SetBlendMode(BlendMode::REPLACE);

		if (LoadMemberValue(wrapper, "compareMode", str))
			SetCompareMode(EnumUtil::CompareModeFromString(str));
		else
			SetCompareMode(CompareMode::LESS);

		if (LoadMemberValue(wrapper, "cullMode", str))
			SetCullMode(EnumUtil::CullModeFromString(str));
		else
			SetCullMode(CullMode::BACK);

		if (!LoadMemberValue(wrapper, "drawMode", str))
		{
			FURYE << "Pass param 'drawMode' not found!";
			return false;
		}
		SetDrawMode(EnumUtil::DrawModeFromString(str));

		bool depthWrite = true;
		if (LoadMemberValue(wrapper, "depth_write", depthWrite))
			SetDepthWrite(depthWrite);
		else
			SetDepthWrite(true);

		if (!LoadMemberValue(wrapper, "index", (int&)m_RenderIndex))
		{
			FURYE << "Pass param 'index' not found!";
			return false;
		}

		LoadArray(wrapper, "shaders", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				FURYE << "Pass's shader name not found!";
				return false;
			}
			if (auto shader = entityMgr->Get<Shader>(str))
			{
				AddShader(shader);
				return true;
			}
			else
			{
				// Warn but keep going: capability-gated variants
				// (INSTANCE_SSBO shaders on GL < 4.3) are intentionally
				// absent from the manager; dropping the rest of the list
				// would silently strip the pass's remaining shaders.
				// The intentional-absence case is debug-level, not a
				// warning, so pipeline loads stay quiet on GL 3.3/4.1.
				if (str.size() > 5 && str.compare(str.size() - 5, 5, "_ssbo") == 0)
					FURYD << "Shader " << str << " not found (SSBO-gated; expected on this context)";
				else
					FURYW << "Shader " << str << " not found!";
				return true;
			}
		});

		if (!LoadArray(wrapper, "input", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				FURYE << "Pass's inputTexture name not found!";
				return false;
			}
			if (auto texture = entityMgr->Get<Texture>(str))
			{
				AddTexture(texture, true);
				return true;
			}
			else
			{
				FURYW << "Input Texture " << str << " not found!";
				return false;
			}
		}))
		{
			FURYW << "Input texture array empty!";
			return false;
		}

		if (!LoadArray(wrapper, "output", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				FURYE << "Pass's outputTexture name not found!";
				return false;
			}
			if (auto texture = entityMgr->Get<Texture>(str))
			{
				AddTexture(texture, false);
				return true;
			}
			else
			{
				FURYW << "Output Texture " << str << " not found!";
				return false;
			}
		}))
		{
			FURYW << "Output texture array empty!";
			return false;
		}

		return true;
	}

	void Pass::Save(void* wrapper, bool object)
	{
		std::vector<std::string> strs;
		std::string emptyStr;

		if (object)
			StartObject(wrapper);

		Entity::Save(wrapper, false);

		SaveKey(wrapper, "shaders");
		SaveArray(wrapper, static_cast<unsigned int>(m_Shaders.size()), [&](unsigned int index)
		{
			SaveValue(wrapper, m_Shaders[index]->GetName());
		});

		SaveKey(wrapper, "input");
		SaveArray(wrapper, static_cast<unsigned int>(m_InputTextures.size()), [&](unsigned int index)
		{
			SaveValue(wrapper, m_InputTextures[index]->GetName());
		});

		SaveKey(wrapper, "output");
		SaveArray(wrapper, static_cast<unsigned int>(m_OutputTextures.size()), [&](unsigned int index)
		{
			SaveValue(wrapper, m_OutputTextures[index]->GetName());
		});

		SaveKey(wrapper, "index");
		SaveValue(wrapper, (int&)m_RenderIndex);

		SaveKey(wrapper, "clearMode");
		SaveValue(wrapper, EnumUtil::ClearModeToString(m_ClearMode));

		SaveKey(wrapper, "clearColor");
		SaveValue(wrapper, m_ClearColor);

		SaveKey(wrapper, "blendMode");
		SaveValue(wrapper, EnumUtil::BlendModeToString(m_BlendMode));

		SaveKey(wrapper, "compareMode");
		SaveValue(wrapper, EnumUtil::CompareModeToString(m_CompareMode));

		SaveKey(wrapper, "cullMode");
		SaveValue(wrapper, EnumUtil::CullModeToString(m_CullMode));

		SaveKey(wrapper, "drawMode");
		SaveValue(wrapper, EnumUtil::DrawModeToString(m_DrawMode));

		SaveKey(wrapper, "depth_write");
		SaveValue(wrapper, m_DepthWrite);

		if (object)
			EndObject(wrapper);
	}

	void Pass::SetRenderIndex(unsigned int index)
	{
		m_RenderIndex = index;
	}

	unsigned int Pass::GetRenderIndex() const
	{
		return m_RenderIndex;
	}

	void Pass::SetClearMode(ClearMode mode)
	{
		m_ClearMode = mode;
	}

	ClearMode Pass::GetClearMode() const
	{
		return m_ClearMode;
	}

	void Pass::SetClearColor(Color color)
	{
		m_ClearColor = color;
	}

	Color Pass::GetClearColor() const
	{
		return m_ClearColor;
	}

	void Pass::SetCompareMode(CompareMode mode)
	{
		m_CompareMode = mode;
	}

	CompareMode Pass::GetCompareMode() const
	{
		return m_CompareMode;
	}

	void Pass::SetBlendMode(BlendMode mode)
	{
		m_BlendMode = mode;
	}

	BlendMode Pass::GetBlendMode() const
	{
		return m_BlendMode;
	}

	void Pass::SetCullMode(CullMode mode)
	{
		m_CullMode = mode;
	}

	CullMode Pass::GetCullMode() const
	{
		return m_CullMode;
	}

	void Pass::SetDrawMode(DrawMode mode)
	{
		m_DrawMode = mode;
	}

	DrawMode Pass::GetDrawMode() const
	{
		return m_DrawMode;
	}

	void Pass::SetDepthWrite(bool value)
	{
		m_DepthWrite = value;
	}

	bool Pass::GetDepthWrite() const
	{
		return m_DepthWrite;
	}

	void Pass::SetArrayTextureLayer(const std::string &name, int index)
	{
		if (m_RTSet == nullptr)
			return;

		auto texture = GetTexture(name, false);
		if (texture == nullptr || (texture->GetType() != TextureType::TEXTURE_2D_ARRAY
			&& texture->GetType() != TextureType::TEXTURE_3D))
			return;

		unsigned int attachIndex = GetAttachmentIndex(texture);
		if (attachIndex != 0xFFFFFFFFu)
			m_RTSet->SetAttachmentLayer(attachIndex, index);
	}

	void Pass::SetArrayTextureLayer(int index)
	{
		if (m_RTSet == nullptr)
			return;

		for (auto texture : m_OutputTextures)
		{
			if (texture->GetType() != TextureType::TEXTURE_2D_ARRAY
				&& texture->GetType() != TextureType::TEXTURE_3D)
				continue;

			unsigned int attachIndex = GetAttachmentIndex(texture);
			if (attachIndex != 0xFFFFFFFFu)
				m_RTSet->SetAttachmentLayer(attachIndex, index);
		}
	}

	void Pass::SetCubeTextureIndex(const std::string &name, int index)
	{
		if (m_RTSet == nullptr)
			return;

		auto texture = GetTexture(name, false);
		if (texture == nullptr || texture->GetType() != TextureType::TEXTURE_CUBE_MAP)
			return;

		unsigned int attachIndex = GetAttachmentIndex(texture);
		if (attachIndex != 0xFFFFFFFFu)
			m_RTSet->SetAttachmentCubeFace(attachIndex, index);
	}

	void Pass::SetCubeTextureIndex(int index)
	{
		if (m_RTSet == nullptr)
			return;

		for (auto texture : m_OutputTextures)
		{
			if (texture->GetType() != TextureType::TEXTURE_CUBE_MAP)
				continue;

			unsigned int attachIndex = GetAttachmentIndex(texture);
			if (attachIndex != 0xFFFFFFFFu)
				m_RTSet->SetAttachmentCubeFace(attachIndex, index);
		}
	}

	int Pass::GetViewPortWidth() const
	{
		return m_ViewPortWidth;
	}

	int Pass::GetViewPortHeight() const
	{
		return m_ViewPortHeight;
	}

	void Pass::AddShader(const std::shared_ptr<Shader> &shader)
	{
		m_Shaders.push_back(shader);
	}

	std::shared_ptr<Shader> Pass::GetShader(ShaderType type, unsigned int textures) const
	{
		for (auto shader : m_Shaders)
		{
			if (shader->GetType() != type)
				continue;

			if (textures == 0)
				return shader;

			if (textures == shader->GetTextureFlags())
				return shader;
		}

		return nullptr;
	}

	std::shared_ptr<Shader> Pass::GetFirstShader() const
	{
		if (m_Shaders.size() > 0)
			return m_Shaders.front();
		else
			return nullptr;
	}

	unsigned int Pass::GetShaderCount() const
	{
		return static_cast<int>(m_Shaders.size());
	}

	void Pass::AddTexture(const std::shared_ptr<Texture> &texture, bool input)
	{
		std::vector<std::shared_ptr<Texture>> &storage = input ? m_InputTextures : m_OutputTextures;
		storage.push_back(texture);

		if (!input)
			m_RenderTargetDirty = true;
	}

	std::shared_ptr<Texture> Pass::GetTextureAt(unsigned int index, bool input) const
	{
		unsigned int size = static_cast<unsigned int>(input ? m_InputTextures.size() : m_OutputTextures.size());
		if (index >= size) return nullptr;

		return input ? m_InputTextures[index] : m_OutputTextures[index];
	}

	std::shared_ptr<Texture> Pass::RemoveTextureAt(unsigned int index, bool input)
	{
		Texture::Ptr ptr = GetTextureAt(index, input);
		if (ptr == nullptr) return nullptr;

		std::vector<std::shared_ptr<Texture>> &storage = input ? m_InputTextures : m_OutputTextures;
		storage.erase(storage.begin() + index);

		if (!input)
			m_RenderTargetDirty = true;

		return ptr;
	}

	unsigned int Pass::GetTextureIndex(const std::string &name, bool input) const
	{
		if (input)
		{
			for (unsigned i = 0; i < m_InputTextures.size(); i++)
				if (m_InputTextures[i]->GetName() == name)
					return i;
		}
		else
		{
			for (unsigned i = 0; i < m_OutputTextures.size(); i++)
				if (m_OutputTextures[i]->GetName() == name)
					return i;
		}
		return 0xFFFFFFFFu;
	}

	std::shared_ptr<Texture> Pass::GetTexture(const std::string &name, bool input) const
	{
		unsigned int index = GetTextureIndex(name, input);
		if (index == 0xFFFFFFFFu) return nullptr;

		return input ? m_InputTextures[index] : m_OutputTextures[index];
	}

	unsigned int Pass::GetTextureCount(bool input) const
	{
		return static_cast<unsigned int>(input ? m_InputTextures.size() : m_OutputTextures.size());
	}

	void Pass::RemoveAllTextures()
	{
		m_InputTextures.clear();
		m_OutputTextures.clear();
		DestroyRenderTarget();
		m_RenderTargetDirty = false;
	}

	void Pass::RebuildRenderTarget()
	{
		DestroyRenderTarget();

		m_ViewPortWidth = m_ViewPortHeight = 0;

		RHIRenderTargetSetDesc desc;

		for (auto texture : m_OutputTextures)
		{
			auto format = texture->GetFormat();
			auto type = texture->GetType();

			if (texture->GetWidth() > m_ViewPortWidth)
				m_ViewPortWidth = texture->GetWidth();

			if (texture->GetHeight() > m_ViewPortHeight)
				m_ViewPortHeight = texture->GetHeight();

			if (texture->GetDirty())
			{
				FURYW << "Texture dirty!";
				continue;
			}

			if (format == TextureFormat::UNKNOW)
			{
				FURYW << "Invalide texture type!";
				continue;
			}

			RHIRenderTargetAttachment attach;
			attach.Texture = texture->GetRHITexture();
			if (attach.Texture == nullptr)
			{
				FURYW << "Texture buffer not created yet!";
				continue;
			}

			if (type == TextureType::TEXTURE_2D_ARRAY || type == TextureType::TEXTURE_3D)
				attach.Layer = 0;
			else if (type == TextureType::TEXTURE_CUBE_MAP)
				attach.CubeFace = 0;

			if (IsDepthFormat(format))
			{
				desc.Depth = attach;
				desc.HasDepth = true;
			}
			else
			{
				desc.Colors.push_back(attach);
			}
		}

		if (GRHIDevice != nullptr)
			m_RTSet = GRHIDevice->CreateRenderTargetSet(desc);

		m_RenderTargetDirty = false;
	}

	void Pass::DestroyRenderTarget()
	{
		if (m_RTSet != nullptr)
		{
			RHIRenderTargetSetRef set = std::move(m_RTSet);
			RenderThread::Get().EnqueueJob([set]() mutable { set.reset(); });
		}

		m_ViewPortWidth = m_ViewPortHeight = 0;
	}

	// Colors in attachment order, depth at GetColorCount().
	unsigned int Pass::GetAttachmentIndex(const std::shared_ptr<Texture> &texture) const
	{
		if (m_RTSet == nullptr)
			return 0xFFFFFFFFu;

		unsigned int colorIndex = 0;
		for (auto output : m_OutputTextures)
		{
			if (output->GetDirty() || output->GetFormat() == TextureFormat::UNKNOW
				|| output->GetRHITexture() == nullptr)
				continue;

			bool isDepth = IsDepthFormat(output->GetFormat());
			unsigned int attachIndex = isDepth ? m_RTSet->GetColorCount() : colorIndex;
			if (!isDepth)
				colorIndex++;

			if (output == texture)
				return attachIndex;
		}

		return 0xFFFFFFFFu;
	}

	void Pass::Clear(ClearMode mode, Color color)
	{
		if (auto *cmd = Cmd())
			cmd->Clear(mode, color);
	}

	void Pass::Bind(bool clear)
	{
		FURY_RHI_THREAD_GUARD();

		if (m_OutputTextures.size() > 0)
		{
			if (m_RTSet == nullptr || m_RenderTargetDirty)
				RebuildRenderTarget();
		}
		else
		{
			InputUtil::Instance()->GetWindowSize(m_ViewPortWidth, m_ViewPortHeight);
		}

		m_Binded = true;

		// Render-target override: a pipeline-level offscreen target captures
		// passes with no output textures (editor viewport); passes with their
		// own outputs keep their dedicated target set.
		RHIRenderTargetSetRef targetSet = m_RTSet;
		if (m_OutputTextures.size() == 0 && Pipeline::Active != nullptr)
		{
			if (auto* rt = Pipeline::Active->GetRenderTarget())
			{
				if (rt->IsAllocated())
				{
					targetSet = rt->GetTargetSet();
					m_ViewPortWidth = rt->GetWidth();
					m_ViewPortHeight = rt->GetHeight();
				}
			}
		}

		RHICommandList *cmd = Cmd();
		if (cmd == nullptr)
			return;

		if (targetSet != nullptr)
			cmd->BeginRenderTargets(targetSet, clear ? m_ClearMode : ClearMode::NONE, m_ClearColor);
		else
			cmd->BeginBackbuffer(m_ViewPortWidth, m_ViewPortHeight, clear ? m_ClearMode : ClearMode::NONE, m_ClearColor);

		cmd->SetRasterState(m_BlendMode, m_CompareMode, true, m_DepthWrite, m_CullMode);
	}

	void Pass::UnBind()
	{
		m_Binded = false;

		RHICommandList *cmd = Cmd();
		if (cmd == nullptr)
			return;

		// Restore raster defaults so state never leaks into unrelated draws.
		cmd->SetRasterState(BlendMode::REPLACE, CompareMode::LESS, true, true, CullMode::NONE);

		for (auto texture : m_OutputTextures)
		{
			if (texture->GetMipmap())
				texture->GenerateMipMap();
		}

		cmd->EndRenderTargets();
	}
}
