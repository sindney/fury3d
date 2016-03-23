#include "Camera.h"
#include "Debug.h"
#include "GLLoader.h"
#include "Pass.h"
#include "SceneNode.h"
#include "Shader.h"
#include "StringUtil.h"
#include "Texture.h"
#include "EnumUtil.h"
#include "EntityUtil.h"

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

	bool Pass::Load(const void* wrapper)
	{
		EntityUtil::Ptr entityUtil = EntityUtil::Instance();
		EnumUtil::Ptr enumUtil = EnumUtil::Instance();

		std::string str;

		if (!IsObject(wrapper)) return false;

		if (!LoadMemberValue(wrapper, "camera", str))
		{
			LOGE << "Pass param 'camera' not found!";
			return false;
		}
		if (auto camNode = entityUtil->FindEntity<SceneNode>(str))
			SetCameraNode(camNode);

		if (!LoadMemberValue(wrapper, "blendMode", str))
		{
			LOGE << "Pass param 'blendMode' not found!";
			return false;
		}
		SetBlendMode(enumUtil->BlendModeFromString(str));

		if (!LoadMemberValue(wrapper, "compareMode", str))
		{
			LOGE << "Pass param 'compareMode' not found!";
			return false;
		}
		SetCompareMode(enumUtil->CompareModeFromString(str));

		if (!LoadMemberValue(wrapper, "cullMode", str))
		{
			LOGE << "Pass param 'cullMode' not found!";
			return false;
		}
		SetCullMode(enumUtil->CullModeFromString(str));

		if (!LoadMemberValue(wrapper, "drawMode", str))
		{
			LOGE << "Pass param 'drawMode' not found!";
			return false;
		}
		SetDrawMode(enumUtil->DrawModeFromString(str));

		if (!LoadMemberValue(wrapper, "index", (int&)m_RenderIndex))
		{
			LOGE << "Pass param 'index' not found!";
			return false;
		}

		if (!LoadArray(wrapper, "shaders", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				LOGE << "Pass's shader name not found!";
				return false;
			}
			if (auto shader = entityUtil->FindEntity<Shader>(str))
			{
				AddShader(shader);
				return true;
			}
			else
			{
				LOGW << "Shader " << str << " not found!";
				return false;
			}
		})) return false;

		if (!LoadArray(wrapper, "input", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				LOGE << "Pass's inputTexture name not found!";
				return false;
			}
			if (auto texture = entityUtil->FindEntity<Texture>(str))
			{
				AddTexture(texture, true);
				return true;
			}
			else
			{
				LOGW << "Input Texture " << str << " not found!";
				return false;
			}
		})) return false;

		if (!LoadArray(wrapper, "output", [&](const void* node) -> bool
		{
			if (!LoadValue(node, str))
			{
				LOGE << "Pass's outputTexture name not found!";
				return false;
			}
			if (auto texture = entityUtil->FindEntity<Texture>(str))
			{
				AddTexture(texture, false);
				return true;
			}
			else
			{
				LOGW << "Output Texture " << str << " not found!";
				return false;
			}
		})) return false;

		return true;
	}

	bool Pass::Save(void* wrapper)
	{
		EnumUtil::Ptr enumUtil = EnumUtil::Instance();

		std::vector<std::string> strs;
		std::string emptyStr;

		StartObject(wrapper);

		SaveKey(wrapper, "name");
		SaveValue(wrapper, m_Name);

		SaveKey(wrapper, "camera");
		SaveValue(wrapper, m_CameraNode == nullptr ? "" : m_CameraNode->GetName());

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

		SaveKey(wrapper, "blendMode");
		SaveValue(wrapper, enumUtil->BlendModeToString(m_BlendMode));

		SaveKey(wrapper, "compareMode");
		SaveValue(wrapper, enumUtil->CompareModeToString(m_CompareMode));

		SaveKey(wrapper, "cullMode");
		SaveValue(wrapper, enumUtil->CullModeToString(m_CullMode));

		SaveKey(wrapper, "drawMode");
		SaveValue(wrapper, enumUtil->DrawModeToString(m_DrawMode));

		EndObject(wrapper);

		return true;
	}

	void Pass::SetRenderIndex(unsigned int index)
	{
		m_RenderIndex = index;
	}

	unsigned int Pass::GetRenderIndex() const
	{
		return m_RenderIndex;
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

	void Pass::SetCameraNode(const std::shared_ptr<SceneNode> &cameraNode)
	{
		if (cameraNode->GetComponent<Camera>())
			m_CameraNode = cameraNode;
		else
			LOGW << "Invalide camera node";
	}

	std::shared_ptr<SceneNode> Pass::GetCameraNode() const
	{
		return m_CameraNode;
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

	void Pass::CreateFrameBuffer()
	{
		if (m_FrameBuffer != 0)
			DeleteFrameBuffer();

		if (m_OutputTextures.size() > 0)
		{
			glGenFramebuffers(1, &m_FrameBuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);

			for (auto texture : m_OutputTextures)
			{
				TextureFormat format = texture->GetFormat();

				if (texture->GetDirty())
				{
					LOGW << "Texture dirty!";
					continue;
				}

				if (format == TextureFormat::DEPTH16 ||
					format == TextureFormat::DEPTH24 ||
					format == TextureFormat::DEPTH32F)
				{
					glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture->GetID(), 0);
				}
				else if (format == TextureFormat::DEPTH24_STENCIL8 ||
					format == TextureFormat::DEPTH32F_STENCIL8)
				{
					glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, texture->GetID(), 0);
				}
				else if (format != TextureFormat::UNKNOW)
				{
					glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + m_ColorAttachmentCount,
						texture->GetID(), 0);

					m_ColorAttachmentCount++;
				}
				else
				{
					LOGW << "Invalide texture type!";
				}
			}

			std::vector<unsigned int> m_ColorAttachs(m_ColorAttachmentCount);
			for (unsigned int i = 0; i < m_ColorAttachmentCount; i++)
				m_ColorAttachs[i] = GL_COLOR_ATTACHMENT0 + i;

			glDrawBuffers(m_ColorAttachmentCount, &m_ColorAttachs[0]);
		}

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void Pass::DeleteFrameBuffer()
	{
		if (m_FrameBuffer != 0)
			glDeleteFramebuffers(1, &m_FrameBuffer);

		m_FrameBuffer = 0;
		m_ColorAttachmentCount = 0;
	}

	void Pass::Bind()
	{
		if (m_OutputTextures.size() > 0 && m_FrameBuffer == 0)
			CreateFrameBuffer();

		glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);

		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(EnumUtil::Instance()->CompareModeToUint(m_CompareMode));

		if (m_BlendMode != BlendMode::REPLACE)
		{
			glEnable(GL_BLEND);
			glBlendFunc(EnumUtil::Instance()->BlendModeSrc(m_BlendMode), 
				EnumUtil::Instance()->BlendModeDest(m_BlendMode));
		}
		else
		{
			glDisable(GL_BLEND);
		}

		if (m_CullMode != CullMode::NONE)
		{
			glEnable(GL_CULL_FACE);
			glCullFace(EnumUtil::Instance()->CullModeToUint(m_CullMode).second);
		}
		else
		{
			glDisable(GL_CULL_FACE);
		}
	}

	void Pass::UnBind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}