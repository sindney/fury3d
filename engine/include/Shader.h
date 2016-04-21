#ifndef _FURY_SHADER_H_
#define _FURY_SHADER_H_

#include <iostream>

#include "Entity.h"
#include "EnumUtil.h"
#include "Matrix4.h"
#include "Serializable.h"

namespace fury
{
	class Material;

	class Mesh;

	class SceneNode;

	class Texture;

	// always bind shader first. then material and meshes.
	class FURY_API Shader : public Entity, public Serializable
	{
	public:

		typedef std::shared_ptr<Shader> Ptr;

		static Ptr Create(const std::string &name, ShaderType type, unsigned int textureFlags = 0);

	protected:

		std::string m_FilePath;

		ShaderType m_Type;

		unsigned int m_TextureFlags;

		unsigned int m_Program = 0;

		unsigned int m_TextureID = 0;

		bool m_Dirty = true;

	public:

		Shader(const std::string &name, ShaderType type, unsigned int textureFlags = 0);

		virtual ~Shader();

		virtual bool Load(const void* wrapper) override;

		virtual bool Save(void* wrapper) override;

		unsigned int GetProgram() const;

		bool GetDirty() const;

		std::string GetFilePath() const;

		ShaderType GetType() const;

		unsigned int GetTextureFlags() const;

		void SetTextureFlags(unsigned int flags);

		bool LoadAndCompile(const std::string &shaderPath);

		bool Compile(const std::string &vsData, const std::string &fsData);

		void DeleteProgram();

		void Bind();

		void BindCamera(const std::shared_ptr<SceneNode> &camNode);

		void BindLight(const std::shared_ptr<SceneNode> &lightNode);

		void BindTexture(const std::string &name, const std::shared_ptr<Texture> &texture);

		void BindTexture(const std::string &name, size_t textureId);

		void BindMaterial(const std::shared_ptr<Material> &material);

		void BindMesh(const std::shared_ptr<Mesh> &mesh);

		void BindSubMesh(const std::shared_ptr<Mesh> &mesh, unsigned int index);

		void BindMatrix(const std::string &name, const Matrix4 &matrix);

		void BindMatrix(const std::string &name, const float *raw);

		void BindMatrices(const std::string &name, int count, const float *raw);

		void BindFloat(const std::string &name, float v0);

		void BindFloat(const std::string &name, float v0, float v1);

		void BindFloat(const std::string &name, float v0, float v1, float v2);

		void BindFloat(const std::string &name, float v0, float v1, float v2, float v3);

		void BindFloat(const std::string &name, int size, int count, const float *value);

		void BindInt(const std::string &name, int v0);

		void BindInt(const std::string &name, int v0, int v1);

		void BindInt(const std::string &name, int v0, int v1, int v2);

		void BindInt(const std::string &name, int v0, int v1, int v2, int v3);

		void BindInt(const std::string &name, int size, int count, const int *value);

		void BindUInt(const std::string &name, unsigned int v0);

		void BindUInt(const std::string &name, unsigned int v0, unsigned int v1);

		void BindUInt(const std::string &name, unsigned int v0, unsigned int v1, unsigned int v2);

		void BindUInt(const std::string &name, unsigned int v0, unsigned int v1, unsigned int v2, unsigned int v3);

		void BindUInt(const std::string &name, int size, int count, const unsigned int *value);

		void UnBind();

	protected:

		void BindMeshData(const std::shared_ptr<Mesh> &mesh);

		int GetUniformLocation(const std::string &name) const;

		void GetVersionInfo(const std::string &source, std::string &versionStr, std::string &mainStr);

		void GetDefines(std::string &definesStr);

	};
}

#endif // _FURY_SHADER_H_