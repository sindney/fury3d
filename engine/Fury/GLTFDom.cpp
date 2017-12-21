#include "Fury/GLTFDom.h"
#include "Fury/Log.h"

namespace fury
{
	const std::string GLTFDom::SUPPORT_VERSION = "2.0";

	const std::string GLTFDom::ACCESSOR_TYPE_SCALAR = "SCALAR";

	const std::string GLTFDom::ACCESSOR_TYPE_VEC2 = "VEC2";

	const std::string GLTFDom::ACCESSOR_TYPE_VEC3 = "VEC3";

	const std::string GLTFDom::ACCESSOR_TYPE_VEC4 = "VEC4";

	const std::string GLTFDom::ACCESSOR_TYPE_MAT2 = "MAT2";

	const std::string GLTFDom::ACCESSOR_TYPE_MAT3 = "MAT3";

	const std::string GLTFDom::ACCESSOR_TYPE_MAT4 = "MAT4";

	const int GLTFDom::COMPONENT_TYPE_BYTE = 5120;

	const int GLTFDom::COMPONENT_TYPE_UNSIGNED_BYTE = 5121;

	const int GLTFDom::COMPONENT_TYPE_SHORT = 5122;

	const int GLTFDom::COMPONENT_TYPE_UNSIGNED_SHORT = 5123;

	const int GLTFDom::COMPONENT_TYPE_UNSIGNED_INT = 5125;

	const int GLTFDom::COMPONENT_TYPE_FLOAT = 5126;

	const std::string GLTFDom::ANIM_PATH_T = "translation";

	const std::string GLTFDom::ANIM_PATH_R = "rotation";

	const std::string GLTFDom::ANIM_PATH_S = "scale";

	const std::string GLTFDom::MIME_JPEG = "image/jpeg";

	const std::string GLTFDom::MIME_PNG = "image/png";

	bool GLTFDom::Load(const void* wrapper, bool object)
	{
		Clear();

		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		// load main scene index
		if (!LoadMemberValue(wrapper, "scene", Scene))
		{
			FURYE << "Active Scene not found!";
			return false;
		}

		// load asset
		if (auto assetNodeWrapper = FindMember(wrapper, "asset"))
		{
			if (!LoadAsset(assetNodeWrapper))
				return false;
		}

		// load buffers
		if (!LoadArray(wrapper, "buffers", [&](const void* node) -> bool 
		{
			return LoadBuffer(node);
		}))
		{
			FURYE << "Error load buffers!";
			return false;
		}

		// load bufferviews
		if (!LoadArray(wrapper, "bufferViews", [&](const void* node) -> bool
		{
			return LoadBufferView(node);
		}))
		{
			FURYE << "Error load bufferViews!";
			return false;
		}

		// load scenes
		if (!LoadArray(wrapper, "scenes", [&](const void* node) -> bool
		{
			return LoadScene(node);
		}))
		{
			FURYE << "Error load scenes!";
			return false;
		}

		// load accessors
		if (!LoadArray(wrapper, "accessors", [&](const void* node) -> bool
		{
			return LoadAccessor(node);
		}))
		{
			FURYE << "Error load accessors!";
			return false;
		}

		// [optional] load images
		if (FindMember(wrapper, "images") != nullptr)
		{
			if (!LoadArray(wrapper, "images", [&](const void* node) -> bool
			{
				return LoadImage(node);
			}))
			{
				FURYE << "Error load images!";
				return false;
			}
		}

		// [optional] load samplers
		if (FindMember(wrapper, "samplers") != nullptr)
		{
			if (!LoadArray(wrapper, "samplers", [&](const void* node) -> bool
			{
				return LoadSampler(node);
			}))
			{
				FURYE << "Error load samplers!";
				return false;
			}
		}

		// [optional] load textures
		if (FindMember(wrapper, "textures") != nullptr)
		{
			if (!LoadArray(wrapper, "textures", [&](const void* node) -> bool
			{
				return LoadTexture(node);
			}))
			{
				FURYE << "Error load textures!";
				return false;
			}
		}

		// load materials
		if (!LoadArray(wrapper, "materials", [&](const void* node) -> bool
		{
			return LoadMaterial(node);
		}))
		{
			FURYE << "Error load materials!";
			return false;
		}

		// load meshes
		if (!LoadArray(wrapper, "meshes", [&](const void* node) -> bool
		{
			return LoadMesh(node);
		}))
		{
			FURYE << "Error load meshes!";
			return false;
		}

		// [optional] load skins
		if (FindMember(wrapper, "skins") != nullptr)
		{
			if (!LoadArray(wrapper, "skins", [&](const void* node) -> bool
			{
				return LoadSkin(node);
			}))
			{
				FURYE << "Error load skins!";
				return false;
			}
		}

		// [optional] load animations
		if (FindMember(wrapper, "animations") != nullptr)
		{
			if (!LoadArray(wrapper, "animations", [&](const void* node) -> bool
			{
				return LoadAnimation(node);
			}))
			{
				FURYE << "Error load animations!";
				return false;
			}
		}

		// load nodes
		if (!LoadArray(wrapper, "nodes", [&](const void* node) -> bool
		{
			return LoadNode(node);
		}))
		{
			FURYE << "Error load nodes!";
			return false;
		}

		return true;
	}

	bool GLTFDom::LoadBuffer(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFBuffer::Ptr bufferPtr = std::make_shared<GLTFBuffer>();

		if (!LoadMemberValue(wrapper, "byteLength", bufferPtr->ByteLength))
		{
			FURYE << "Buffer's byteLength not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "uri", bufferPtr->URI))
		{
			FURYE << "Buffer's uri not found!";
			return false;
		}

		Buffers.push_back(bufferPtr);
		return true;
	}

	bool GLTFDom::LoadBufferView(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFBufferView::Ptr bufferViewPtr = std::make_shared<GLTFBufferView>();

		if (!LoadMemberValue(wrapper, "buffer", bufferViewPtr->Buffer))
		{
			FURYE << "Buffer's buffer not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "byteLength", bufferViewPtr->ByteLength))
		{
			FURYE << "Buffer's byteLength not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "byteOffset", bufferViewPtr->ByteOffset))
		{
			FURYE << "Buffer's byteOffset not found!";
			return false;
		}

		if (LoadMemberValue(wrapper, "byteStride", bufferViewPtr->ByteStride))
		{
			bufferViewPtr->ByteStrideSpecified = true;
			// TODO: figure out why and when byte stride exist in real cases.
			FURYE << "Byte Stride not supported!";
			return false;
		}

		BufferViews.push_back(bufferViewPtr);
		return true;
	}

	bool GLTFDom::LoadScene(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFScene::Ptr scenePtr = std::make_shared<GLTFScene>();

		if (!LoadMemberValue(wrapper, "name", scenePtr->Name))
		{
			FURYE << "Scene's name not found!";
			return false;
		}

		if (!LoadArray(wrapper, "nodes", scenePtr->Nodes))
		{
			FURYE << "Error loading scene.nodes array!";
			return false;
		}

		Scenes.push_back(scenePtr);
		return true;
	}

	bool GLTFDom::LoadAccessor(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (FindMember(wrapper, "sparse") != NULL)
		{
			FURYE << "Dont's support sparse accessors!";
			return false;
		}

		GLTFAccessor::Ptr accessorPtr = std::make_shared<GLTFAccessor>();

		if (!LoadMemberValue(wrapper, "componentType", accessorPtr->ComponentType))
		{
			FURYE << "Accessor's componentType not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "type", accessorPtr->Type))
		{
			FURYE << "Accessor's type not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "count", accessorPtr->Count))
		{
			FURYE << "Accessor's count not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "bufferView", accessorPtr->BufferView))
		{
			FURYE << "Accessor's bufferView not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "byteOffset", accessorPtr->ByteOffset))
			accessorPtr->ByteOffset = 0;

		if (!LoadMemberValue(wrapper, "normalized ", accessorPtr->Normalized))
			accessorPtr->Normalized = false;

		Accessors.push_back(accessorPtr);
		return true;
	}

	bool GLTFDom::LoadImage(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFImage::Ptr imagePtr = std::make_shared<GLTFImage>();

		if (LoadMemberValue(wrapper, "uri", imagePtr->URI))
		{
			imagePtr->URISpecified = true;
		}
		else
		{
			if (LoadMemberValue(wrapper, "bufferView", imagePtr->BufferView))
			{
				imagePtr->BufferViewSpecified = true;
			}
			else
			{
				FURYE << "No uri specified nor bufferView specified!";
				return false;
			}

			if (LoadMemberValue(wrapper, "mimeType", imagePtr->MIMEType))
			{
				imagePtr->MIMETypeSpecified = true;
			}
			else
			{
				FURYE << "Image's mimeType not found!";
				return false;
			}
		}

		Images.push_back(imagePtr);
		return true;
	}

	bool GLTFDom::LoadSampler(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFSampler::Ptr samplerPtr = std::make_shared<GLTFSampler>();

		if (LoadMemberValue(wrapper, "magFilter", samplerPtr->MagFilter))
			samplerPtr->MagFilterSpecified = true;

		if (LoadMemberValue(wrapper, "minFilter", samplerPtr->MinFilter))
			samplerPtr->MinFilterSpecified = true;

		if (LoadMemberValue(wrapper, "wrapS", samplerPtr->WrapS))
			samplerPtr->WrapSSpecified = true;

		if (LoadMemberValue(wrapper, "wrapT", samplerPtr->WrapT))
			samplerPtr->WrapTSpecified = true;

		Samplers.push_back(samplerPtr);
		return true;
	}

	bool GLTFDom::LoadTexture(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFTexture::Ptr texturePtr = std::make_shared<GLTFTexture>();

		if (!LoadMemberValue(wrapper, "source", texturePtr->Source))
		{
			FURYE << "Texture's source not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "sampler", texturePtr->Sampler))
		{
			FURYE << "Texture's sampler not found!";
			return false;
		}

		Textures.push_back(texturePtr);
		return true;
	}

	bool GLTFDom::LoadMaterial(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFMaterial::Ptr materialPtr = std::make_shared<GLTFMaterial>();
		if (!LoadMemberValue(wrapper, "name", materialPtr->Name))
		{
			FURYE << "Material's name not found!";
			return false;
		}

		if (auto metallicNodePtr = FindMember(wrapper, "pbrMetallicRoughness"))
		{
			// baseColorTexture
			if (auto baseTexturePtr = FindMember(metallicNodePtr, "baseColorTexture"))
			{
				if (LoadMemberValue(baseTexturePtr, "index", materialPtr->BaseColorTextureIndex))
				{
					materialPtr->BaseColorTextureIndexSpecified = true;
				}
				else
				{
					FURYE << "Material's pbrMetallicRoughness.baseColorTexture.index not found!";
					return false;
				}

				if (LoadMemberValue(baseTexturePtr, "texCoord", materialPtr->BaseColorTextureCoord))
					materialPtr->BaseColorTextureCoordSpecified = true;
			}

			// baseColorFactor
			if (LoadMemberValue(metallicNodePtr, "baseColorFactor", materialPtr->BaseColorFactor))
			{
				materialPtr->BaseColorFactorSpecified = true;
			}
			else
			{
				FURYE << "Material's pbrMetallicRoughness.baseColorFactor not found!";
				return false;
			}

			// metallicRoughnessTexture
			if (auto metallicTexturePtr = FindMember(metallicNodePtr, "metallicRoughnessTexture"))
			{
				if (LoadMemberValue(metallicTexturePtr, "index", materialPtr->MetallicTextureIndex))
				{
					materialPtr->MetallicTextureIndexSpecified = true;
				}
				else
				{
					FURYE << "Material's pbrMetallicRoughness.metallicRoughnessTexture.index not found!";
					return false;
				}

				if (LoadMemberValue(metallicTexturePtr, "texCoord", materialPtr->MetallicTextureCoord))
					materialPtr->MetallicTextureCoordSpecified = true;
			}

			if (LoadMemberValue(metallicNodePtr, "roughnessFactor", materialPtr->RoughnessFactor))
				materialPtr->RoughnessFactorSpecified = true;

			if (LoadMemberValue(metallicNodePtr, "metallicFactor", materialPtr->MetallicFactor))
				materialPtr->MetallicFactorSpecified = true;
		}

		Materials.push_back(materialPtr);
		return true;
	}

	bool GLTFDom::LoadMesh(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (FindMember(wrapper, "weights") != NULL)
		{
			FURYE << "Don't support morph target animation!";
			return false;
		}

		GLTFMesh::Ptr meshPtr = std::make_shared<GLTFMesh>();

		if (!LoadMemberValue(wrapper, "name", meshPtr->Name))
		{
			FURYE << "Mesh's name not found!";
			return false;
		}

		if (!LoadArray(wrapper, "primitives", [&](const void* node) -> bool
		{
			GLTFPrimitive::Ptr primitivePtr = std::make_shared<GLTFPrimitive>();
			if (LoadPrimitive(primitivePtr, node))
			{
				meshPtr->Primitives.push_back(primitivePtr);
				return true;
			}
			else
			{
				return false;
			}
		}))
		{
			FURYE << "Error loading mesh's primitives array!";
			return false;
		}

		Meshes.push_back(meshPtr);
		return true;
	}

	bool GLTFDom::LoadPrimitive(GLTFPrimitive::Ptr ptr, const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "material", ptr->Material))
		{
			FURYE << "Primitive's material not found!";
			return false;
		}

		if (LoadMemberValue(wrapper, "mode", ptr->Mode))
			ptr->ModeSpecified = true;

		if (!LoadMemberValue(wrapper, "indices", ptr->Indices))
		{
			FURYE << "Primitive's indices not found!";
			return false;
		}

		if (auto attributesPtr = FindMember(wrapper, "attributes"))
		{
			if (!LoadMemberValue(attributesPtr, "POSITION", ptr->Position))
			{
				FURYE << "Primitive's POSITION attribute not found!";
				return false;
			}

			if (LoadMemberValue(attributesPtr, "NORMAL", ptr->Normal))
				ptr->NormalSpecified = true;

			if (LoadMemberValue(attributesPtr, "TANGENT", ptr->Tangent))
				ptr->TangentSpecified = true;

			if (LoadMemberValue(attributesPtr, "TEXCOORD_0", ptr->TexCoord0))
				ptr->TexCoord0Specified = true;

			if (LoadMemberValue(attributesPtr, "TEXCOORD_1", ptr->TexCoord1))
				ptr->TexCoord1Specified = true;

			if (LoadMemberValue(attributesPtr, "COLOR_0", ptr->Color0))
				ptr->Color0Specified = true;

			if (LoadMemberValue(attributesPtr, "JOINTS_0", ptr->Joints0))
				ptr->Joints0Specified = true;

			if (LoadMemberValue(attributesPtr, "WEIGHTS_0", ptr->Weights0))
				ptr->Weights0Specified = true;
		}
		else
		{
			FURYE << "Primitive's attributes not found!";
			return false;
		}

		return true;
	}

	bool GLTFDom::LoadSkin(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFSkin::Ptr skinPtr = std::make_shared<GLTFSkin>();

		if (!LoadArray(wrapper, "joints", skinPtr->Joints))
		{
			FURYE << "Skin's joints not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "inverseBindMatrices", skinPtr->InverseBindMatrices))
		{
			FURYE << "Skin's inverseBindMatrices not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "skeleton", skinPtr->Skeleton))
		{
			FURYE << "Skin's skeleton not found!";
			return false;
		}

		Skins.push_back(skinPtr);
		return true;
	}

	bool GLTFDom::LoadAnimation(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFAnimation::Ptr animationPtr = std::make_shared<GLTFAnimation>();

		if (!LoadMemberValue(wrapper, "name", animationPtr->Name))
		{
			FURYE << "Animation's name not found!";
			return false;
		}

		// load animation.channels
		if (!LoadArray(wrapper, "channels", [&](const void* node) -> bool 
		{
			GLTFAnimChannel::Ptr channelPtr = std::make_shared<GLTFAnimChannel>();
			if (LoadAnimChannel(channelPtr, node))
			{
				animationPtr->Channels.push_back(channelPtr);
				return true;
			}
			else
			{
				return false;
			}
		}))
		{
			FURYE << "Animation's channels not found!";
			return false;
		}

		// load animation.samplers
		if (!LoadArray(wrapper, "samplers", [&](const void* node) -> bool
		{
			GLTFAnimSampler::Ptr samplerPtr = std::make_shared<GLTFAnimSampler>();
			if (LoadAnimSampler(samplerPtr, node))
			{
				animationPtr->Samplers.push_back(samplerPtr);
				return true;
			}
			else
			{
				return false;
			}
		}))
		{
			FURYE << "Animation's samplers not found!";
			return false;
		}

		Animations.push_back(animationPtr);
		return true;
	}

	bool GLTFDom::LoadAnimChannel(GLTFAnimChannel::Ptr ptr, const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "sampler", ptr->Sampler))
		{
			FURYE << "Animation.channels[x].sampler not found!";
			return false;
		}

		if (auto targetPtr = FindMember(wrapper, "target"))
		{
			if (!LoadMemberValue(targetPtr, "node", ptr->TargetNode))
			{
				FURYE << "Animation.channels[x].target.node not found!";
				return false;
			}

			if (!LoadMemberValue(targetPtr, "path", ptr->TargetPath))
			{
				FURYE << "Animation.channels[x].target.path not found!";
				return false;
			}
		}
		else
		{
			FURYE << "Animation.channels[x].target not found!";
			return false;
		}

		return true;
	}

	bool GLTFDom::LoadAnimSampler(GLTFAnimSampler::Ptr ptr, const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "input", ptr->Input))
		{
			FURYE << "Animation.samplers[x].input not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "interpolation", ptr->Interpolation))
		{
			FURYE << "Animation.samplers[x].interpolation not found!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "output", ptr->Output))
		{
			FURYE << "Animation.samplers[x].output not found!";
			return false;
		}

		return true;
	}

	bool GLTFDom::LoadNode(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		GLTFNode::Ptr nodePtr = std::make_shared<GLTFNode>();

		if (!LoadMemberValue(wrapper, "name", nodePtr->Name))
		{
			FURYE << "Node's name not found!";
			return false;
		}

		if (LoadMemberValue(wrapper, "translation", nodePtr->Position))
			nodePtr->PositionSpecified = true;

		if (LoadMemberValue(wrapper, "rotation", nodePtr->Rotation))
			nodePtr->RotationSpecified = true;

		if (LoadMemberValue(wrapper, "scale", nodePtr->Scale))
			nodePtr->ScaleSpecified = true;

		if (LoadArray(wrapper, "children", nodePtr->Children))
			nodePtr->ChildrenSpecified = true;

		if (LoadMemberValue(wrapper, "mesh", nodePtr->Mesh))
			nodePtr->MeshSpecified = true;

		if (LoadMemberValue(wrapper, "skin", nodePtr->Skin))
			nodePtr->SkinSpecified = true;

		Nodes.push_back(nodePtr);
		return true;
	}

	bool GLTFDom::LoadAsset(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!LoadMemberValue(wrapper, "version", Version))
		{
			FURYE << "Assets' Version not found!";
			return false;
		}

		if (GLTFDom::SUPPORT_VERSION != Version)
		{
			FURYE << "Version " << Version << " not supported, supported version is " << GLTFDom::SUPPORT_VERSION;
			return false;
		}

		return true;
	}

	void GLTFDom::Save(void* wrapper, bool object)
	{
		FURYE << "GLTFDom Save not implimented!";
	}

	void GLTFDom::Clear()
	{
		Buffers.clear();
		BufferViews.clear();
		Scenes.clear();
		Accessors.clear();
		Samplers.clear();
		Materials.clear();
		Meshes.clear();
		Nodes.clear();
		Scene = 0;
		Version = std::string();
	}
}