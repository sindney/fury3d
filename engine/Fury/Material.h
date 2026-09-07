#ifndef _FURY_MATERIALS_H_
#define _FURY_MATERIALS_H_

#include <unordered_map>

#include "Fury/Entity.h"
#include "Fury/EnumUtil.h"
#include "Fury/Buffer.h"

namespace fury
{
	class UniformBase;

	class Texture;

	class Shader;

	class FURY_API Material : public Entity, public Buffer
	{
	public:

		friend class Shader;

		typedef std::shared_ptr<Material> Ptr;

		typedef std::unordered_map<std::string, std::shared_ptr<Texture>> TextureMap;

		typedef std::unordered_map<std::string, std::shared_ptr<UniformBase>> UniformMap;

		// The diffuse slot carries the smoke/fire PNG for CPU-driven
		// billboard particles (see particle-system spec). ParticleRenderer
		// reads the same key -- no separate particle material class.
		static const std::string DIFFUSE_TEXTURE;

		static const std::string SPECULAR_TEXTURE;

		static const std::string NORMAL_TEXTURE;

		static const std::string SHININESS;

		static const std::string TRANSPARENCY;

		static const std::string AMBIENT_FACTOR;

		static const std::string DIFFUSE_FACTOR;

		static const std::string SPECULAR_FACTOR;

		static const std::string EMISSIVE_FACTOR;

		static const std::string AMBIENT_COLOR;

		static const std::string DIFFUSE_COLOR;

		static const std::string SPECULAR_COLOR;

		static const std::string EMISSIVE_COLOR;

		// ---- PBR (metallic-roughness) slots -----------------------------
		// Populated by GltfImporter when the active pipeline is HDR.
		// The HDR pipeline's lighting shaders (added in the same
		// change) read these to drive the PBR BRDF. LDR pipelines
		// ignore them -- the legacy Lambert shader chain doesn't bind
		// them.
		static const std::string METALLIC_FACTOR;

		static const std::string ROUGHNESS_FACTOR;

		static const std::string METALLIC_ROUGHNESS_TEXTURE;

		static const std::string OCCLUSION_TEXTURE;

		static const std::string MATERIAL_ID;

		static Ptr Create(const std::string &name);

	private:

		static unsigned int m_GlobalID;

	protected:

		unsigned int GetMaterialID();

		TextureMap m_Textures;

		UniformMap m_Uniforms;

		std::vector<std::shared_ptr<Shader>> m_Shaders;

		unsigned int m_TextureFlags;

		// Derived: m_AlphaMode != AlphaMode::BLEND. Kept as a stored
		// field because legacy files serialize it directly.
		bool m_Opaque;

		AlphaMode m_AlphaMode;

		float m_AlphaCutoff;

		// Vegetation flags. TwoSided: cull off + back-face normal flip.
		// WindEnabled: vertex-color-weighted sway in the WIND shader variant.
		bool m_TwoSided = false;

		bool m_WindEnabled = false;

		unsigned int m_ID;

	public:

		Material(const std::string &name);

		virtual ~Material();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		virtual void UpdateBuffer() override;

		virtual void DeleteBuffer() override;

		unsigned int GetTextureFlags() const;

		std::shared_ptr<Texture> GetTexture(const std::string &name) const;

		void SetTexture(const std::string &name, const std::shared_ptr<Texture> &ptr);

		unsigned int GetTextureCount() const;

		// Read-only access to the full texture map. Used by FileUtil's
		// save-time extraction of memory-backed textures.
		const TextureMap &GetTextures() const;

		void SetUniform(const std::string &name, const std::shared_ptr<UniformBase> &ptr);

		std::shared_ptr<UniformBase> GetUniform(const std::string &name);

		unsigned int GetUniformCount() const;

		// Read-only access to the full uniform map. Used by the
		// material editor's Uniforms table (asset-editor-windows).
		const UniformMap &GetUniforms() const { return m_Uniforms; }

		void SetShaderForPass(unsigned int index, const std::shared_ptr<Shader> &shader);

		std::shared_ptr<Shader> GetShaderForPass(unsigned int index);

		bool GetOpaque() const;

		void SetOpaque(bool value);

		AlphaMode GetAlphaMode() const;

		// Also syncs m_Opaque (mode != BLEND).
		void SetAlphaMode(AlphaMode mode);

		float GetAlphaCutoff() const;

		void SetAlphaCutoff(float value);

		bool GetTwoSided() const { return m_TwoSided; }

		void SetTwoSided(bool value) { m_TwoSided = value; }

		bool GetWindEnabled() const { return m_WindEnabled; }

		void SetWindEnabled(bool value) { m_WindEnabled = value; }

		// get this material's unique identifier for rendering.
		unsigned int GetID() const;
	};
}

#endif // _FURY_MATERIALS_H_