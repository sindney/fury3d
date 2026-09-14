#ifndef _FURY_SHADER_H_
#define _FURY_SHADER_H_

#include <iostream>

#include "Fury/Entity.h"
#include "Fury/EnumUtil.h"
#include "Fury/Matrix4.h"

namespace fury
{
	class Material;

	class Mesh;

	class SceneNode;

	struct PacketCamera;

	struct PacketLight;

	class Texture;

	// always bind shader first. then material and meshes.
	class FURY_API Shader : public Entity, public std::enable_shared_from_this<Shader>
	{
	public:

		typedef std::shared_ptr<Shader> Ptr;

		static Ptr Create(const std::string &name, ShaderType type, unsigned int textureFlags = 0);

	protected:

		std::string m_FilePath;

		ShaderType m_Type;

		unsigned int m_TextureFlags;

		std::vector<std::string> m_Defines;

		unsigned int m_Program = 0;

		unsigned int m_TextureID = 0;

		bool m_Dirty = true;

		bool m_UseGeomShader = false;

	public:

		Shader(const std::string &name, ShaderType type, unsigned int textureFlags = 0);

		virtual ~Shader();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		unsigned int GetProgram() const;

		bool GetDirty() const;

		std::string GetFilePath() const;

		ShaderType GetType() const;

		unsigned int GetTextureFlags() const;

		void SetTextureFlags(unsigned int flags);

		void AddDefine(std::string define);

		bool LoadAndCompile(const std::string &shaderPath, bool useGeomShader = false);

		// Compile only the COMPUTE_SHADER section of a shader file as a
		// single-stage GL_COMPUTE_SHADER program (#version 430 injected when
		// the file declares none). Requires gl::HasComputeShaders().
		bool LoadAndCompileCompute(const std::string &shaderPath);

		// Same, from an in-memory source string.
		bool CompileCompute(const std::string &source);

		// UseProgram + glDispatchCompute. No-op with a warning when compute
		// entry points are missing or the program failed to build.
		void DispatchCompute(unsigned int groupsX, unsigned int groupsY, unsigned int groupsZ);

		// glMemoryBarrier over image/fetch/storage/update bits. Call between
		// a compute write pass and any later image/sampler read.
		static void ComputeBarrier();

		bool Compile(const std::string &vsData, const std::string &fsData, const std::string &gsData);

		void DeleteProgram();

		void Bind();

		void BindCamera(const std::shared_ptr<SceneNode> &camNode);

		// Packet variants (render-thread path): same uniforms from copied
		// frame data instead of live nodes.
		void BindCameraData(const PacketCamera &cam);

		void BindLight(const std::shared_ptr<SceneNode> &lightNode);

		void BindLightData(const PacketLight &light);

		// bind texture to 1st texture
		void BindTexture(const std::shared_ptr<Texture> &texture);

		// bind texture to 1st texture
		void BindTexture(size_t textureId, TextureType type);

		void BindTexture(const std::string &name, const std::shared_ptr<Texture> &texture);

		void BindTexture(const std::string &name, size_t textureId, TextureType type);

		void BindMaterial(const std::shared_ptr<Material> &material);

		void BindMesh(const std::shared_ptr<Mesh> &mesh);

		// palette != nullptr: bind those joint matrices instead of reading
		// the mesh's live Joint objects (render-thread path).
		void BindMesh(const std::shared_ptr<Mesh> &mesh, const Matrix4 *palette, int paletteCount);

		void BindSubMesh(const std::shared_ptr<Mesh> &mesh, unsigned int index);

		void BindMatrix(const std::string &name, const Matrix4 &matrix);

		void BindMatrix(const std::string &name, const float *raw);

		void BindMatrices(const std::string &name, int count, const float *raw);

		void BindMatrices(const std::string &name, int count, const Matrix4 *matrices);

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

		// palette != nullptr: bind those joint matrices instead of reading
		// the mesh's live Joint objects (render-thread path).
		void BindMeshData(const std::shared_ptr<Mesh> &mesh, const Matrix4 *palette, int paletteCount);

	public:

		int GetUniformLocation(const std::string &name) const;

		// Location-cached binds for the draw-command cache replay.
		void BindFloatLocation(int location, float x);
		void BindFloatLocation(int location, float x, float y);
		void BindFloatLocation(int location, float x, float y, float z);
		void BindFloatLocation(int location, float x, float y, float z, float w);
		void BindIntLocation(int location, int x);
		void BindMatrixLocation(int location, const float *matrix);
		void BindMatricesLocation(int location, int count, const float *matrices);
		// glActiveTexture(GL_TEXTURE0 + unitOffset) + bind + sampler uniform;
		// returns the next unit offset.
		int BindTextureAt(int unitOffset, int location, const std::shared_ptr<Texture> &texture);
		void SetTextureUnitCursor(int unitOffset);

		void GetVersionInfo(const std::string &source, std::string &versionStr, std::string &mainStr);

	};
}

#endif // _FURY_SHADER_H_