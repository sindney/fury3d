#include <fstream>
#include <sstream>
#include <algorithm>

#if defined(_WIN32)
#include <winsock.h>
#else 
#include <arpa/inet.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP

#include "stb_image.h"

#include "lz4.h"

#include "Fury/FileUtil.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshUtil.h"
#include "Fury/GLTFDom.h"
#include "Fury/Log.h"
#include "Fury/Uniform.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Serializable.h"
#include "Fury/Texture.h"

#undef far
#undef near
#undef max

namespace fury
{
	std::string FileUtil::m_AbsPath = "";

	unsigned int* FileUtil::m_UIntBuffer = NULL;
	int FileUtil::m_UIntBufferLength = 0;

	unsigned short* FileUtil::m_UShortBuffer = NULL;
	int FileUtil::m_UShortBufferLength = 0;

	unsigned char* FileUtil::m_UCharBuffer = NULL;
	int FileUtil::m_UCharBufferLength = 0;

	float* FileUtil::m_FloatBuffer = NULL;
	int FileUtil::m_FloatBufferLength = 0;

	short* FileUtil::m_ShortBuffer = NULL;
	int FileUtil::m_ShortBufferLength = 0;

	char* FileUtil::m_CharBuffer = NULL;
	int FileUtil::m_CharBufferLength = 0;

	std::string FileUtil::GetAbsPath()
	{
#if defined(__APPLE__)
		if (m_AbsPath.size() == 0)
		{
			CFBundleRef mainBundle = CFBundleGetMainBundle();
			CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
			char path[512];
			if (!CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8*)path, 512))
			{
				FURYE << "Absolute Path Not Found!";
			}
			CFRelease(resourcesURL);
			m_AbsPath = std::string(path) + '/';
		}
#endif
		return m_AbsPath;
	}

	std::string FileUtil::GetAbsPath(const std::string &source, bool toForwardSlash)
	{
		std::string clone = source;
		if (toForwardSlash)
			std::replace(clone.begin(), clone.end(), '\\', '/');

		return GetAbsPath() + clone;
	}

	bool FileUtil::FileExist(const std::string &path)
	{
		std::ifstream stream(path.c_str());
		if (stream.good())
		{
			stream.close();
			return true;
		}
		else
		{
			FURYE << "File " << path << " not exist!";
			stream.close();
			return false;
		}
	}

	// file io

	bool FileUtil::LoadString(const std::string &path, std::string &output)
	{
		std::ifstream stream(path, std::ios::in);
		if (stream)
		{
			stream.seekg(0, std::ios::end);
			size_t size = (size_t)stream.tellg();
			stream.seekg(0, std::ios::beg);

			output.resize(size);

			stream.read(&output[0], size);
			stream.close();

			return size == output.size();
		}
		else
		{
			FURYW << "Failed to load chars: " << path;
			return false;
		}
	}

	bool FileUtil::LoadImage(const std::string &path, std::vector<unsigned char> &output, int &width, int &height, int &channels)
	{
		if (!FileExist(path))
			return false;

		unsigned char* ptr = stbi_load(path.c_str(), &width, &height, &channels, 0);
		if (ptr && width && height)
		{
			output.resize(width * height * channels);
			memcpy(&output[0], ptr, output.size());

			stbi_image_free(ptr);

			return true;
		}
		else
		{
			FURYW << "Failed to load image: " << path;
			return false;
		}
	}

	bool FileUtil::LoadFile(const Serializable::Ptr &source, const std::string &filePath)
	{
		using namespace rapidjson;

		std::ifstream stream(filePath);
		if (stream)
		{
			Document dom;

			std::stringstream buffer;
			buffer << stream.rdbuf();
			stream.close();

			dom.Parse(buffer.str().c_str());

			if (dom.HasParseError())
			{
				FURYE << "Error parsing json file " << filePath << ": " << dom.GetParseError();
				return false;
			}

			if (!source->Load(&dom))
			{
				FURYE << "Serialization failed!";
				return false;
			}

			FURYD << filePath << " successfully deserialized!";
			return true;
		}
		else
		{
			FURYE << "Path " << filePath << " not found!";
			return false;
		}
	}

	bool FileUtil::SaveFile(const Serializable::Ptr &source, const std::string &filePath, int maxDecimalPlaces)
	{
		using namespace rapidjson;

		std::ofstream output(filePath);
		if (output)
		{
			StringBuffer sb;
			PrettyWriter<StringBuffer> writer(sb);
			writer.SetMaxDecimalPlaces(maxDecimalPlaces);

			source->Save(&writer);

			output.write(sb.GetString(), sb.GetSize());
			output.close();

			FURYD << filePath << " successfully serialized!";
			return true;
		}
		else
		{
			FURYE << "Path " << filePath << " not found!";
			return false;
		}
	}

	bool FileUtil::LoadCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath)
	{
		using namespace rapidjson;

		std::ifstream stream(filePath, std::ios_base::binary);
		if (stream)
		{
			Document dom;

			{
				uint32_t orgSize, compressSize, netOrgSize, netCompressSize;

				stream.read((char*)&netOrgSize, sizeof(uint32_t));
				orgSize = ntohl(netOrgSize);

				stream.read((char*)&netCompressSize, sizeof(uint32_t));
				compressSize = ntohl(netCompressSize);

				char *srcBuffer = new char[compressSize];
				stream.read(srcBuffer, compressSize);

				char* buffer = new char[orgSize];

				int size = LZ4_decompress_fast(srcBuffer, buffer, orgSize);
				if (size == 0)
				{
					FURYE << "Failed to decompress data!";
					return false;
				}

				dom.Parse(buffer, orgSize);

				delete[] buffer;
				delete[] srcBuffer;
			}

			if (dom.HasParseError())
			{
				FURYE << "Error parsing json file " << filePath << ": " << dom.GetParseError();
				return false;
			}

			if (!source->Load(&dom))
			{
				FURYE << "Deserialization failed!";
				return false;
			}

			FURYD << filePath << " successfully deserialized!";
			return true;
		}
		else
		{
			FURYE << "Path " << filePath << " not found!";
			return false;
		}
	}

	bool FileUtil::SaveCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces)
	{
		using namespace rapidjson;

		std::ofstream stream(filePath, std::ios_base::binary);
		if (stream)
		{
			StringBuffer sb;
			PrettyWriter<StringBuffer> writer(sb);
			writer.SetMaxDecimalPlaces(maxDecimalPlaces);

			source->Save(&writer);

			const char *src = sb.GetString();
			uint32_t srcSize = sb.GetSize();

			uint32_t bufferSize = LZ4_compressBound(srcSize);
			char* buffer = new char[bufferSize];

			uint32_t size = LZ4_compress_default(src, buffer, srcSize, bufferSize);
			if (size == 0)
			{
				FURYE << "Failed to compress json string!";
				return false;
			}
			else
			{
				FURYD << "Before: " << srcSize << " After: " << size;
			}

			uint32_t netSrcSize = htonl(srcSize);
			uint32_t netSize = htonl(size);

			stream.write((char*)&netSrcSize, sizeof(uint32_t));
			stream.write((char*)&netSize, sizeof(uint32_t));
			stream.write(buffer, size);
			stream.flush();
			stream.close();

			delete[] buffer;

			FURYD << filePath << " successfully serialized!";
			return true;
		}
		else
		{
			FURYE << "Path " << filePath << " not found!";
			return false;
		}
	}

	bool FileUtil::LoadGLTFFile(const std::shared_ptr<Scene> &scene, const std::string &jsonPath, const unsigned int options)
	{
		using namespace rapidjson;

		std::size_t slashIndex = jsonPath.find_last_of('/');
		std::string workingDir;
		if (slashIndex != std::string::npos)
		{
			workingDir = jsonPath.substr(0, slashIndex + 1);
		}
		else
		{
			workingDir = FileUtil::GetAbsPath();
			FURYW << "Can't find forward slash in jsonPath, using executable's directory as working dir.";
		}

		std::ifstream jsonStream(jsonPath, std::ios_base::binary);
		if (jsonStream)
		{
			Document jsonDom;

			std::stringstream buffer;
			buffer << jsonStream.rdbuf();
			jsonStream.close();

			jsonDom.Parse(buffer.str().c_str());

			if (jsonDom.HasParseError())
			{
				FURYE << "Error parsing json file " << jsonPath << ": " << jsonDom.GetParseError();
				return false;
			}

			auto gltfDom = std::make_shared<GLTFDom>();
			if (!gltfDom->Load(&jsonDom))
			{
				FURYE << "Deserialization failed!";
				return false;
			}

			// open filebuffers
			std::vector<std::shared_ptr<std::ifstream>> buffers;
			for (unsigned int i = 0; i < gltfDom->Buffers.size(); i++)
			{
				auto glbufferPtr = gltfDom->Buffers[i];
				auto bufferPtr = std::make_shared<std::ifstream>(
					workingDir + glbufferPtr->URI, std::ios_base::binary);
				if (!*bufferPtr)
				{
					FURYE << "Failed to load buffer " << glbufferPtr->URI << "!";
					return false;
				}
				buffers.emplace_back(bufferPtr);
			}

			// load gltf data
			bool status = PrivateLoadGLTFFile(gltfDom, buffers, scene, workingDir, options);

			// close file buffers
			for (unsigned int i = 0; i < buffers.size(); i++)
				buffers[i]->close();
			jsonStream.close();

			FURYD << jsonPath << (status ? " successfully deserialized!" : " deserialization failed!");

			// delete data buffers
			DeleteDataBuffers();

			return status;
		}
		else
		{
			FURYE << "Json gltf file not found!";
			return false;
		}
	}

	bool FileUtil::PrivateLoadGLTFFile(const std::shared_ptr<GLTFDom> &gltfDom, std::vector<std::shared_ptr<std::ifstream>> &buffers,
		const std::shared_ptr<Scene> &scene, const std::string &workingDir, const unsigned int options)
	{
		// textures
		std::vector<Texture::Ptr> textures;
		for (unsigned int i = 0; i < gltfDom->Textures.size(); i++)
		{
			auto glTexPtr = gltfDom->Textures[i];
			auto glImgPtr = gltfDom->Images[glTexPtr->Source];
			auto samplerPtr = gltfDom->Samplers.size() > 0 ? gltfDom->Samplers[glTexPtr->Sampler] : nullptr;
			std::string texName = glImgPtr->URISpecified ? glImgPtr->URI : "texture" + std::to_string(i);
			auto texPtr = Texture::Create(texName);
			texPtr->CreateFromImage(workingDir + glImgPtr->URI, true, false);
			if (samplerPtr != nullptr)
			{
				if (samplerPtr->MagFilterSpecified)
					texPtr->SetFilterMode(EnumUtil::FilterModeFromUint(samplerPtr->MagFilter));
				if (samplerPtr->WrapSSpecified)
					texPtr->SetWrapMode(EnumUtil::WrapModeFromUint(samplerPtr->WrapS));
			}
			textures.emplace_back(texPtr);
		}

		// materials
		// TODO: impliment pbr material
		std::vector<Material::Ptr> materials;
		for (unsigned int i = 0; i < gltfDom->Materials.size(); i++)
		{
			auto glMaterialPtr = gltfDom->Materials[i];
			auto materialPtr = Material::Create(glMaterialPtr->Name);
			if (glMaterialPtr->BaseColorFactorSpecified)
			{
				materialPtr->SetUniform(Material::DIFFUSE_COLOR, Uniform3f::Create({
					glMaterialPtr->BaseColorFactor.r, glMaterialPtr->BaseColorFactor.g, glMaterialPtr->BaseColorFactor.b }));
			}
			else
			{
				materialPtr->SetUniform(Material::DIFFUSE_COLOR, Uniform3f::Create({ 1, 1, 1 }));
			}
			if (glMaterialPtr->BaseColorTextureIndexSpecified)
			{
				materialPtr->SetTexture(Material::DIFFUSE_TEXTURE, textures[glMaterialPtr->BaseColorTextureIndex]);
			}
			materials.emplace_back(materialPtr);
			scene->GetEntityManager()->Add(materialPtr);
		}

		// load meshes
		std::vector<Mesh::Ptr> meshes;
		bool optOptimizeMesh = (options & GLTFImportFlags::OPTMZ_MESH) == 1;
		bool optGenNormal = (options & GLTFImportFlags::GEN_NORMAL) == 1;
		bool optGenTangent = (options & GLTFImportFlags::GEN_TANGENT) == 1;

		for (unsigned int i = 0; i < gltfDom->Meshes.size(); i++)
		{
			auto glmeshPtr = gltfDom->Meshes[i];
			auto meshPtr = Mesh::Create(glmeshPtr->Name);

			int vertexCount = 0;
			unsigned int subMeshCount = glmeshPtr->Primitives.size();
			for (unsigned int j = 0; j < subMeshCount; j++)
			{
				auto glsubmeshPtr = glmeshPtr->Primitives[j];
				int subVertexCount = 0;

				// position
				{
					auto accessorPtr = gltfDom->Accessors[glsubmeshPtr->Position];
					int floatCount = AccessBuffer(gltfDom, buffers, accessorPtr, meshPtr->Positions.Data);
					if (floatCount == -1)
					{
						FURYE << "Failed to read mesh " << glmeshPtr->Name << "'s position data!";
						return false;
					}
					subVertexCount = floatCount / 3;
				}

				// normal
				if (glsubmeshPtr->NormalSpecified)
				{
					auto accessorPtr = gltfDom->Accessors[glsubmeshPtr->Normal];
					if (AccessBuffer(gltfDom, buffers, accessorPtr, meshPtr->Normals.Data) == -1)
					{
						FURYE << "Failed to read mesh " << glmeshPtr->Name << "'s normal data!";
						return false;
					}
				}

				// tangent
				if (glsubmeshPtr->TangentSpecified)
				{
					auto accessorPtr = gltfDom->Accessors[glsubmeshPtr->Tangent];
					if (AccessBuffer(gltfDom, buffers, accessorPtr, meshPtr->Tangents.Data) == -1)
					{
						FURYE << "Failed to read mesh " << glmeshPtr->Name << "'s tangent data!";
						return false;
					}
				}

				// texcoord0
				if (glsubmeshPtr->TexCoord0Specified)
				{
					auto accessorPtr = gltfDom->Accessors[glsubmeshPtr->TexCoord0];
					if (AccessBuffer(gltfDom, buffers, accessorPtr, meshPtr->UVs.Data) == -1)
					{
						FURYE << "Failed to read mesh " << glmeshPtr->Name << "'s texcoord0 data!";
						return false;
					}
				}

				// joints
				if (glsubmeshPtr->Joints0Specified)
				{
					auto accessorPtr = gltfDom->Accessors[glsubmeshPtr->Joints0];
					if (AccessBuffer(gltfDom, buffers, accessorPtr, meshPtr->IDs.Data) == -1)
					{
						FURYE << "Failed to read mesh " << glmeshPtr->Name << "'s joints data!";
						return false;
					}
				}

				// weights
				if (glsubmeshPtr->Weights0Specified)
				{
					auto accessorPtr = gltfDom->Accessors[glsubmeshPtr->Weights0];
					if (AccessBuffer(gltfDom, buffers, accessorPtr, meshPtr->Weights.Data) == -1)
					{
						FURYE << "Failed to read mesh " << glmeshPtr->Name << "'s weights data!";
						return false;
					}
					// we store 3 weight components, and calculate 4th weights on the fly.
					if (accessorPtr->Type == GLTFDom::ACCESSOR_TYPE_VEC4)
					{
						int count = meshPtr->Weights.Data.size() / 4;
						for (int k = 1; k < count; k++)
						{
							int k4 = k * 4;
							int k3 = k * 3;
							meshPtr->Weights.Data[k3] = meshPtr->Weights.Data[k4];
							meshPtr->Weights.Data[k3 + 1] = meshPtr->Weights.Data[k4 + 1];
							meshPtr->Weights.Data[k3 + 2] = meshPtr->Weights.Data[k4 + 2];
						}
						meshPtr->Weights.Data.resize(count * 3);
						meshPtr->Weights.Data.shrink_to_fit();
					}
				}

				// indices
				{
					auto accessorPtr = gltfDom->Accessors[glsubmeshPtr->Indices];
					if (subMeshCount == 1)
					{
						int bufferLen = AccessBuffer(gltfDom, buffers, accessorPtr, meshPtr->Indices.Data);
						if (bufferLen == -1)
						{
							FURYE << "Failed to read mesh " << glmeshPtr->Name << "'s indices data!";
							return false;
						}
					}
					else
					{
						auto submeshPtr = SubMesh::Create();
						int bufferLen = AccessBuffer(gltfDom, buffers, accessorPtr, submeshPtr->Indices.Data);
						if (bufferLen == -1)
						{
							FURYE << "Failed to read mesh " << glmeshPtr->Name << "'s submesh" << j << "'s indices data!";
							return false;
						}
						meshPtr->AddSubMesh(submeshPtr);

						if (vertexCount != 0)
						{
							for (int k = 0; k < bufferLen; k++)
								submeshPtr->Indices.Data[k] += vertexCount;
						}
						meshPtr->Indices.Data.insert(meshPtr->Indices.Data.end(), submeshPtr->Indices.Data.begin(), submeshPtr->Indices.Data.end());
						vertexCount += subVertexCount;
					}
				}
			}

			if (optGenNormal && meshPtr->Indices.Data.size() > 0 && meshPtr->Normals.Data.size() == 0)
				MeshUtil::CalculateNormal(meshPtr);

			if (optGenTangent && meshPtr->Indices.Data.size() > 0 && meshPtr->Normals.Data.size() > 0)
				MeshUtil::CalculateTangent(meshPtr);

			if (optOptimizeMesh)
				MeshUtil::OptimizeMesh(meshPtr);

			meshPtr->CalculateAABB();
			meshes.emplace_back(meshPtr);
			scene->GetEntityManager()->Add(meshPtr);
		}

		// load skins
		for (unsigned int i = 0; i < gltfDom->Skins.size(); i++)
		{
			auto glSkinPtr = gltfDom->Skins[i];
			auto accessorPtr = gltfDom->Accessors[glSkinPtr->InverseBindMatrices];
			if (accessorPtr->Type != "MAT4")
			{
				FURYE << "Skin's inverseBindMatrices buffer muse be MAT4!";
				return false;
			}

			std::vector<float> floatBuffer;
			int floatCount = AccessBuffer(gltfDom, buffers, accessorPtr, floatBuffer);
			if (floatCount == -1)
			{
				FURYE << "Failed to read skin" << i << "'s inverseBindMatrices data!";
				return false;
			}

			std::vector<Matrix4> matrices;
			int matrixCount = floatCount / 16;
			matrices.resize(matrixCount);
			for (int i = 0; i < matrixCount; i++)
				matrices[i] = Matrix4(floatBuffer.data(), i * 16);
		}

		// load nodes
		std::vector<SceneNode::Ptr> nodes;
		for (unsigned int i = 0; i < gltfDom->Nodes.size(); i++)
		{
			auto glNodePtr = gltfDom->Nodes[i];
			auto nodePtr = SceneNode::Create(glNodePtr->Name);

			if (glNodePtr->PositionSpecified)
				nodePtr->SetLocalPosition(glNodePtr->Position);
			if (glNodePtr->RotationSpecified)
				nodePtr->SetLocalRoattion(glNodePtr->Rotation);
			if (glNodePtr->ScaleSpecified)
				nodePtr->SetLocalScale(glNodePtr->Scale);

			if (glNodePtr->MeshSpecified)
			{
				auto glmeshPtr = gltfDom->Meshes[glNodePtr->Mesh];
				auto meshPtr = meshes[glNodePtr->Mesh];
				auto meshRenderPtr = MeshRender::Create(nullptr, meshPtr);
				nodePtr->AddComponent(meshRenderPtr);
				unsigned int subMeshCount = meshPtr->GetSubMeshCount();
				if (subMeshCount > 0)
				{
					for (unsigned int j = 0; j < subMeshCount; j++)
						meshRenderPtr->SetMaterial(materials[glmeshPtr->Primitives[j]->Material], j);
				}
				else
				{
					meshRenderPtr->SetMaterial(materials[glmeshPtr->Primitives[0]->Material]);
				}
			}
			nodes.emplace_back(nodePtr);
		}

		// build nodes relationship
		for (unsigned int i = 0; i < gltfDom->Nodes.size(); i++)
		{
			auto glNodePtr = gltfDom->Nodes[i];
			auto nodePtr = nodes[i];
			if (glNodePtr->ChildrenSpecified)
			{
				for (unsigned int j = 0; j < glNodePtr->Children.size(); j++)
				{
					auto childIndex = glNodePtr->Children[j];
					auto childPtr = nodes[childIndex];
					nodePtr->AddChild(childPtr);
				}
			}
		}

		// load aniamtion
		for (unsigned int i = 0; i < gltfDom->Animations.size(); i++)
		{
			auto glAnimPtr = gltfDom->Animations[i];
			for (unsigned int j = 0; j < glAnimPtr->Channels.size(); j++)
			{
				auto glChannelPtr = glAnimPtr->Channels[j];
				auto glSamplerPtr = glAnimPtr->Samplers[glChannelPtr->Sampler];

			}
		}

		// load scene
		if (gltfDom->Scenes.size() > 0)
		{
			auto glscenePtr = gltfDom->Scenes[gltfDom->Scene];
			auto rootNodePtr = scene->GetRootNode();
			for (unsigned int i = 0; i < glscenePtr->Nodes.size(); i++)
				rootNodePtr->AddChild(nodes[glscenePtr->Nodes[i]]);
			rootNodePtr->Recompose(true);
		}
		else
		{
			FURYE << "Scene not found!";
			return false;
		}

		return true;
	}

	template<typename T>
	int FileUtil::AccessBuffer(const std::shared_ptr<GLTFDom> &gltfDom, std::vector<std::shared_ptr<std::ifstream>> &buffers,
		const std::shared_ptr<GLTFAccessor> &accessor, std::vector<T> &output)
	{
		auto bufferViewPtr = gltfDom->BufferViews[accessor->BufferView];
		auto buffer = buffers[bufferViewPtr->Buffer];
		buffer->seekg(accessor->ByteOffset + bufferViewPtr->ByteOffset);
		unsigned int prevLength = output.size();
		int arrLength = 0;

		if (accessor->ComponentType == GLTFDom::COMPONENT_TYPE_UNSIGNED_INT)
		{
			arrLength = bufferViewPtr->ByteLength / 4;

			unsigned int* tmpBuffer = ResizeDataBuffer(m_UIntBuffer, m_UIntBufferLength, arrLength);
			buffer->read(reinterpret_cast<char*>(tmpBuffer), bufferViewPtr->ByteLength);

			output.reserve(prevLength + arrLength);
			for (int i = 0; i < arrLength; i++)
				output.emplace_back((T)tmpBuffer[i]);
		}
		else if (accessor->ComponentType == GLTFDom::COMPONENT_TYPE_UNSIGNED_SHORT)
		{
			arrLength = bufferViewPtr->ByteLength / 2;

			unsigned short* tmpBuffer = ResizeDataBuffer(m_UShortBuffer, m_UShortBufferLength, arrLength);
			buffer->read(reinterpret_cast<char*>(tmpBuffer), bufferViewPtr->ByteLength);

			output.reserve(prevLength + arrLength);
			for (int i = 0; i < arrLength; i++)
			{
				if (accessor->Normalized)
				{
					unsigned short vushort = tmpBuffer[i];
					float vfloat = vushort / 65535.0f;
					output.emplace_back((T)vfloat);
				}
				else
				{
					output.emplace_back((T)tmpBuffer[i]);
				}
			}
		}
		else if (accessor->ComponentType == GLTFDom::COMPONENT_TYPE_UNSIGNED_BYTE)
		{
			arrLength = bufferViewPtr->ByteLength;

			unsigned char* tmpBuffer = ResizeDataBuffer(m_UCharBuffer, m_UCharBufferLength, arrLength);
			buffer->read(reinterpret_cast<char*>(tmpBuffer), bufferViewPtr->ByteLength);

			output.reserve(prevLength + arrLength);
			for (int i = 0; i < arrLength; i++)
			{
				if (accessor->Normalized)
				{
					unsigned char vubyte = tmpBuffer[i];
					float vfloat = vubyte / 255.0f;
					output.emplace_back((T)vfloat);
				}
				else
				{
					output.emplace_back((T)tmpBuffer[i]);
				}
			}
		}
		else if (accessor->ComponentType == GLTFDom::COMPONENT_TYPE_FLOAT)
		{
			arrLength = bufferViewPtr->ByteLength / 4;

			float* tmpBuffer = ResizeDataBuffer(m_FloatBuffer, m_FloatBufferLength, arrLength);
			buffer->read(reinterpret_cast<char*>(tmpBuffer), bufferViewPtr->ByteLength);

			output.reserve(prevLength + arrLength);
			for (int i = 0; i < arrLength; i++)
				output.emplace_back((T)tmpBuffer[i]);
		}
		else if (accessor->ComponentType == GLTFDom::COMPONENT_TYPE_SHORT)
		{
			arrLength = bufferViewPtr->ByteLength / 2;

			short* tmpBuffer = ResizeDataBuffer(m_ShortBuffer, m_ShortBufferLength, arrLength);
			buffer->read(reinterpret_cast<char*>(tmpBuffer), bufferViewPtr->ByteLength);

			output.reserve(prevLength + arrLength);
			for (int i = 0; i < arrLength; i++)
			{
				if (accessor->Normalized)
				{
					short vshort = tmpBuffer[i];
					float vfloat = std::max(vshort / 32767.0f, -1.0f);
					output.emplace_back((T)vfloat);
				}
				else
				{
					output.emplace_back((T)tmpBuffer[i]);
				}
			}
		}
		else if (accessor->ComponentType == GLTFDom::COMPONENT_TYPE_BYTE)
		{
			arrLength = bufferViewPtr->ByteLength;

			char* tmpBuffer = ResizeDataBuffer(m_CharBuffer, m_CharBufferLength, arrLength);
			buffer->read(tmpBuffer, bufferViewPtr->ByteLength);

			output.reserve(prevLength + arrLength);
			for (int i = 0; i < arrLength; i++)
			{
				if (accessor->Normalized)
				{
					char vbyte = tmpBuffer[i];
					float vfloat = std::max(vbyte / 127.0f, -1.0f);
					output.emplace_back((T)vfloat);
				}
				else
				{
					output.emplace_back((T)tmpBuffer[i]);
				}
			}
		}
		else
		{
			FURYE << "Component type " << accessor->ComponentType << " not supported!";
			return -1;
		}

		return arrLength;
	}

	template<typename T>
	T* FileUtil::ResizeDataBuffer(T *buffer, int &oldLength, int newLength)
	{
		if (buffer == NULL)
			buffer = new T[newLength];
		else if (newLength > oldLength)
			buffer = new T[newLength];
		oldLength = newLength;
		return buffer;
	}

	void FileUtil::DeleteDataBuffers()
	{
		if (m_UIntBuffer != NULL)
		{
			delete m_UIntBuffer;
			m_UIntBuffer = NULL;
		}
		m_UIntBufferLength = 0;

		if (m_UShortBuffer != NULL)
		{
			delete m_UShortBuffer;
			m_UShortBuffer = NULL;
		}
		m_UShortBufferLength = 0;

		if (m_UCharBuffer != NULL)
		{
			delete m_UCharBuffer;
			m_UCharBuffer = NULL;
		}
		m_UCharBufferLength = 0;

		if (m_FloatBuffer != NULL)
		{
			delete m_FloatBuffer;
			m_FloatBuffer = NULL;
		}
		m_FloatBufferLength = 0;

		if (m_ShortBuffer != NULL)
		{
			delete m_ShortBuffer;
			m_ShortBuffer = NULL;
		}
		m_ShortBufferLength = 0;

		if (m_CharBuffer != NULL)
		{
			delete m_CharBuffer;
			m_CharBuffer = NULL;
		}
		m_CharBufferLength = 0;
	}
}