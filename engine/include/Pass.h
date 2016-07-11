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

		Color m_ClearColor = Color(0, 0, 0, 0);

		CompareMode m_CompareMode = CompareMode::LESS;

		BlendMode m_BlendMode = BlendMode::REPLACE;

		CullMode m_CullMode = CullMode::BACK;

		DrawMode m_DrawMode = DrawMode::OPAQUE;

		std::shared_ptr<SceneNode> m_CameraNode;

		std::vector<std::shared_ptr<Shader>> m_Shaders;

		unsigned int m_RenderIndex = 0;

		unsigned int m_FrameBuffer = 0;

		unsigned int m_ColorAttachmentCount = 0;

		int m_ViewPortWidth = 0;

		int m_ViewPortHeight = 0;

		bool m_RenderTargetDirty = false;

		bool m_Binded = false;

		std::vector<std::pair<unsigned int, std::shared_ptr<Texture>>> m_LayerTextures;

		std::vector<std::pair<unsigned int, std::shared_ptr<Texture>>> m_CubeTextures;

	public:

		Pass(const std::string &name);

		virtual ~Pass();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual bool Save(void* wrapper, bool object = true) override;

		void SetRenderIndex(unsigned int index);

		unsigned int GetRenderIndex() const;

		void SetClearMode(ClearMode mode);

		ClearMode GetClearMode() const;

		void SetClearColor(Color color);

		Color GetClearColor() const;

		void SetCompareMode(CompareMode mode);

		CompareMode GetCompareMode() const;

		void SetBlendMode(BlendMode mode);

		BlendMode GetBlendMode() const;

		void SetCullMode(CullMode mode);

		CullMode GetCullMode() const;

		void SetDrawMode(DrawMode mode);

		DrawMode GetDrawMode() const;

		// After binding, call this to specify the layer of the texture array to render to.
		void SetArrayTextureLayer(const std::string &name, int index);

		// After binding, call this to specify the layer of all texture array to render to.
		void SetArrayTextureLayer(int index);

		void SetCubeTextureIndex(const std::string &name, int index);

		void SetCubeTextureIndex(int index);

		int GetViewPortWidth() const;

		int GetViewPortHeight() const;

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

		unsigned int GetFBO() const;

		void RemoveAllTextures();

		void CreateFrameBuffer();

		void BindRenderTargets();

		void UnBindRenderTargets();

		void DeleteFrameBuffer();

		void Clear(ClearMode mode, Color color = Color(0.0f, 0.0f, 0.0f, 0.0f));

		void Bind(bool clear = true);

		void UnBind();
	};
}

#endif // _FURY_PASS_H_