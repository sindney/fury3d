#ifndef _FURY_PASS_H_
#define _FURY_PASS_H_

#include <unordered_map>
#include <vector>

#include "Color.h"
#include "Entity.h"
#include "EnumUtil.h"
#include "Vector4.h"
#include "Serializable.h"

#undef OPAQUE
#undef TRANSPARENT
#undef IGNORE

namespace fury
{
	class SceneNode;

	class Shader;

	class Texture;
	
	class FURY_API Pass : public Entity, public Serializable
	{
		friend class FileUtil;

	public:

		typedef std::shared_ptr<Pass> Ptr;

		static Ptr Create(const std::string &name);

	protected:

		std::vector<std::shared_ptr<Texture>> m_OutputTextures;

		std::vector<std::shared_ptr<Texture>> m_InputTextures;

		ClearMode m_ClearMode = ClearMode::COLOR_DEPTH_STENCIL;

		CompareMode m_CompareMode = CompareMode::LESS;

		BlendMode m_BlendMode = BlendMode::REPLACE;

		CullMode m_CullMode = CullMode::BACK;

		DrawMode m_DrawMode = DrawMode::RENDERABLE;

		std::shared_ptr<SceneNode> m_CameraNode;

		std::vector<std::shared_ptr<Shader>> m_Shaders;

		unsigned int m_RenderIndex = 0;

		unsigned int m_FrameBuffer = 0;

		unsigned int m_ColorAttachmentCount = 0;

	public:

		Pass(const std::string &name);

		virtual ~Pass();

		virtual bool Load(const void* wrapper) override;

		virtual bool Save(void* wrapper) override;

		void SetRenderIndex(unsigned int index);

		unsigned int GetRenderIndex() const;

		void SetClearMode(ClearMode mode);

		ClearMode GetClearMode() const;

		void SetCompareMode(CompareMode mode);

		CompareMode GetCompareMode() const;

		void SetBlendMode(BlendMode mode);

		BlendMode GetBlendMode() const;

		void SetCullMode(CullMode mode);

		CullMode GetCullMode() const;

		void SetDrawMode(DrawMode mode);

		DrawMode GetDrawMode() const;

		void SetCameraNode(const std::shared_ptr<SceneNode> &name);

		std::shared_ptr<SceneNode> GetCameraNode() const;

		void AddShader(const std::shared_ptr<Shader> &shader);

		std::shared_ptr<Shader> GetShader(ShaderType type, unsigned int textures = 0) const;

		std::shared_ptr<Shader> GetFirstShader() const;

		unsigned int GetShaderCount() const;

		void AddTexture(const std::shared_ptr<Texture> &texture, bool input);

		std::shared_ptr<Texture> GetTextureAt(unsigned int index, bool input) const;

		std::shared_ptr<Texture> RemoveTextureAt(unsigned int index, bool input);

		unsigned int GetTextureIndex(const std::string &name, bool input) const;

		std::shared_ptr<Texture> GetTexture(const std::string &name, bool input) const;

		unsigned int GetTextureCount(bool input) const;

		void CreateFrameBuffer();	

		void DeleteFrameBuffer();

		void Bind();

		void UnBind();
	};
}

#endif // _FURY_PASS_H_