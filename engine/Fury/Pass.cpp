#include "Fury/Camera.h"
#include "Fury/Log.h"
#include "Fury/GLLoader.h"
#include "Fury/Pass.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/EnumUtil.h"
#include "Fury/EntityManager.h"
#include "Fury/Pipeline.h"
#include "Fury/InputUtil.h"

namespace fury
{
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
		DeleteFrameBuffer();
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
				FURYW << "Shader " << str << " not found!";
				return false;
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
		SaveArray(wrapper, m_Shaders.size(), [&](unsigned int index)
		{
			SaveValue(wrapper, m_Shaders[index]->GetName());
		});

		SaveKey(wrapper, "input");
		SaveArray(wrapper, m_InputTextures.size(), [&](unsigned int index)
		{
			SaveValue(wrapper, m_InputTextures[index]->GetName());
		});

		SaveKey(wrapper, "output");
		SaveArray(wrapper, m_OutputTextures.size(), [&](unsigned int index)
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

	void Pass::SetArrayTextureLayer(const std::string &name, int index)
	{
		for (auto &pair : m_LayerTextures)
		{
			if (pair.second->GetName() == name)
			{
				glFramebufferTextureLayer(GL_FRAMEBUFFER, pair.first, pair.second->GetID(), 0, index);
				break;
			}
		}
	}

	void Pass::SetArrayTextureLayer(int index)
	{
		for (auto &pair : m_LayerTextures)
			glFramebufferTextureLayer(GL_FRAMEBUFFER, pair.first, pair.second->GetID(), 0, index);
	}

	void Pass::SetCubeTextureIndex(const std::string &name, int index)
	{
		for (auto &pair : m_CubeTextures)
		{
			if (pair.second->GetName() == name)
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER, pair.first, GL_TEXTURE_CUBE_MAP_POSITIVE_X + index, pair.second->GetID(), 0);
				break;
			}
		}
	}

	void Pass::SetCubeTextureIndex(int index)
	{
		for (auto &pair : m_CubeTextures)
			glFramebufferTexture2D(GL_FRAMEBUFFER, pair.first, GL_TEXTURE_CUBE_MAP_POSITIVE_X + index, pair.second->GetID(), 0);
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
		return m_Shaders.size();
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
		unsigned int size = input ? m_InputTextures.size() : m_OutputTextures.size();
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

		// remove from cube textures
		for (unsigned int i = 0; i < m_CubeTextures.size(); i++)
		{
			if (m_CubeTextures[i].second == ptr)
			{
				m_CubeTextures.erase(m_CubeTextures.begin() + i);
				break;
			}
		}

		// remove from layer textures
		for (unsigned int i = 0; i < m_LayerTextures.size(); i++)
		{
			if (m_LayerTextures[i].second == ptr)
			{
				m_LayerTextures.erase(m_LayerTextures.begin() + i);
				break;
			}
		}

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
		return -1;
	}

	std::shared_ptr<Texture> Pass::GetTexture(const std::string &name, bool input) const
	{
		unsigned int index = GetTextureIndex(name, input);
		if (index == -1) return nullptr;

		return input ? m_InputTextures[index] : m_OutputTextures[index];
	}

	unsigned int Pass::GetTextureCount(bool input) const
	{
		return input ? m_InputTextures.size() : m_OutputTextures.size();
	}

	unsigned int Pass::GetFBO() const
	{
		return m_FrameBuffer;
	}

	void Pass::RemoveAllTextures()
	{
		m_InputTextures.clear();
		m_OutputTextures.clear();
		m_LayerTextures.clear();
		m_CubeTextures.clear();
		UnBindRenderTargets();
	}

	void Pass::CreateFrameBuffer()
	{
		if (m_FrameBuffer != 0)
			DeleteFrameBuffer();

		glGenFramebuffers(1, &m_FrameBuffer);
		BindRenderTargets();
	}

	void Pass::BindRenderTargets()
	{
		if (m_FrameBuffer == 0)
			return;

		if (m_OutputTextures.size() == 0)
		{
			m_RenderTargetDirty = false;
			return;
		}

		if (!m_Binded)
			glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);

		m_ViewPortWidth = m_ViewPortHeight = 0;
		m_ColorAttachmentCount = 0;

		m_LayerTextures.clear();
		m_CubeTextures.clear();

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

			unsigned int attachId = GL_DEPTH_ATTACHMENT;

			if (format == TextureFormat::DEPTH16 ||
				format == TextureFormat::DEPTH24 ||
				format == TextureFormat::DEPTH32F)
			{
				attachId = GL_DEPTH_ATTACHMENT;
			}
			else if (format == TextureFormat::DEPTH24_STENCIL8 ||
				format == TextureFormat::DEPTH32F_STENCIL8)
			{
				attachId = GL_DEPTH_STENCIL_ATTACHMENT;
			}
			else if (format != TextureFormat::UNKNOW)
			{
				attachId = GL_COLOR_ATTACHMENT0 + m_ColorAttachmentCount;
				m_ColorAttachmentCount++;
			}
			else
			{
				FURYW << "Invalide texture type!";
				continue;
			}

			if (type == TextureType::TEXTURE_2D_ARRAY)
			{
				glFramebufferTextureLayer(GL_FRAMEBUFFER, attachId, texture->GetID(), 0, 0);
				m_LayerTextures.push_back(std::make_pair(attachId, texture));
			}
			if (type == TextureType::TEXTURE_CUBE_MAP)
			{
				glFramebufferTexture2D(GL_FRAMEBUFFER, attachId, GL_TEXTURE_CUBE_MAP_POSITIVE_X, texture->GetID(), 0);
				m_CubeTextures.push_back(std::make_pair(attachId, texture));
			}
			else
			{
				glFramebufferTexture(GL_FRAMEBUFFER, attachId, texture->GetID(), 0);
			}
		}

		if (m_ColorAttachmentCount > 0)
		{
			std::vector<unsigned int> m_ColorAttachs(m_ColorAttachmentCount);
			for (unsigned int i = 0; i < m_ColorAttachmentCount; i++)
				m_ColorAttachs[i] = GL_COLOR_ATTACHMENT0 + i;

			glDrawBuffers(m_ColorAttachmentCount, &m_ColorAttachs[0]);
		}
		else
		{
			glDrawBuffers(0, GL_NONE);
		}

		if (!m_Binded)
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

		m_RenderTargetDirty = false;
	}

	void Pass::UnBindRenderTargets()
	{
		if (m_FrameBuffer == 0)
			return;

		if (!m_Binded)
			glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);

		glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0, 0);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, 0, 0);

		for (unsigned int i = 0; i < m_ColorAttachmentCount; i++)
			glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, 0, 0);
		glDrawBuffers(0, GL_NONE);

		if (!m_Binded)
			glBindFramebuffer(GL_FRAMEBUFFER, 0);

		m_ViewPortWidth = m_ViewPortHeight = 0;
	}

	void Pass::DeleteFrameBuffer()
	{
		if (m_FrameBuffer != 0)
			glDeleteFramebuffers(1, &m_FrameBuffer);

		m_FrameBuffer = 0;
		m_ColorAttachmentCount = 0;
	}

	void Pass::Clear(ClearMode mode, Color color)
	{
		glClearColor(color.r, color.g, color.b, color.a);
		switch (mode)
		{
		case ClearMode::COLOR:
			glClear(GL_COLOR_BUFFER_BIT);
			break;
		case ClearMode::COLOR_DEPTH:
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			break;
		case ClearMode::COLOR_DEPTH_STENCIL:
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			break;
		case ClearMode::COLOR_STENCIL:
			glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			break;
		case ClearMode::DEPTH:
			glClear(GL_DEPTH_BUFFER_BIT);
			break;
		case ClearMode::STENCIL:
			glClear(GL_STENCIL_BUFFER_BIT);
			break;
		case ClearMode::STENCIL_DEPTH:
			glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			break;
		case ClearMode::NONE:
			break;
		}
	}

	void Pass::Bind(bool clear)
	{
		if (m_OutputTextures.size() > 0)
		{
			if (m_FrameBuffer == 0)
				CreateFrameBuffer();

			if (m_RenderTargetDirty)
			{
				UnBindRenderTargets();
				BindRenderTargets();
			}
		}
		else
		{
			InputUtil::Instance()->GetWindowSize(m_ViewPortWidth, m_ViewPortHeight);
		}

		m_Binded = true;

		glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);
		glViewport(0, 0, m_ViewPortWidth, m_ViewPortHeight);

		if (clear)
			Clear(m_ClearMode, m_ClearColor);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(EnumUtil::CompareModeToUint(m_CompareMode));

		if (m_BlendMode != BlendMode::REPLACE)
		{
			glEnable(GL_BLEND);
			glBlendFunc(EnumUtil::BlendModeSrc(m_BlendMode),
				EnumUtil::BlendModeDest(m_BlendMode));
			glBlendEquation(EnumUtil::BlendModeOp(m_BlendMode));
		}
		else
		{
			glDisable(GL_BLEND);
		}

		if (m_CullMode != CullMode::NONE)
		{
			glEnable(GL_CULL_FACE);
			glCullFace(EnumUtil::CullModeToUint(m_CullMode).second);
		}
		else
		{
			glDisable(GL_CULL_FACE);
		}
	}

	void Pass::UnBind()
	{
		m_Binded = false;
		for (auto texture : m_OutputTextures)
		{
			if (texture->GetMipmap())
				texture->GenerateMipMap();
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}