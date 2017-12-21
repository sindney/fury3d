#ifndef _FURY_GLTFDOM_H_
#define _FURY_GLTFDOM_H_

#include <string>
#include <vector>

#include "Fury/Color.h"
#include "Fury/Serializable.h"
#include "Fury/Quaternion.h"
#include "Fury/Vector4.h"

#undef LoadImage

namespace fury
{
	class FURY_API GLTFScene
	{
	public:

		typedef std::shared_ptr<GLTFScene> Ptr;

		std::string Name;

		std::vector<int> Nodes;
	};

	class FURY_API GLTFNode
	{
	public:

		typedef std::shared_ptr<GLTFNode> Ptr;

		std::string Name;

		Quaternion Rotation;

		bool RotationSpecified = false;

		Vector4 Scale;

		bool ScaleSpecified = false;

		Vector4 Position;

		bool PositionSpecified = false;

		int Mesh;

		bool MeshSpecified = false;

		int Skin;

		bool SkinSpecified = false;

		std::vector<int> Children;

		bool ChildrenSpecified = false;
	};

	class FURY_API GLTFPrimitive
	{
	public:

		typedef std::shared_ptr<GLTFPrimitive> Ptr;

		int Material;

		int Mode;

		bool ModeSpecified = false;

		int Indices;

		int Position;

		int Normal;

		bool NormalSpecified = false;

		int Tangent;

		bool TangentSpecified = false;

		int TexCoord0;

		bool TexCoord0Specified = false;

		int TexCoord1;

		bool TexCoord1Specified = false;

		int Color0;

		bool Color0Specified = false;

		int Joints0;

		bool Joints0Specified = false;

		int Weights0;

		bool Weights0Specified = false;
	};

	class FURY_API GLTFMesh
	{
	public:

		typedef std::shared_ptr<GLTFMesh> Ptr;

		std::string Name;

		std::vector<GLTFPrimitive::Ptr> Primitives;
	};

	class FURY_API GLTFSkin
	{
	public:

		typedef std::shared_ptr<GLTFSkin> Ptr;

		std::vector<int> Joints;

		int InverseBindMatrices;

		int Skeleton;
	};

	class FURY_API GLTFMaterial
	{
	public:

		typedef std::shared_ptr<GLTFMaterial> Ptr;

		std::string Name;

		int BaseColorTextureIndex;

		bool BaseColorTextureIndexSpecified = false;

		int BaseColorTextureCoord;

		bool BaseColorTextureCoordSpecified = false;

		Color BaseColorFactor;

		bool BaseColorFactorSpecified = false;

		int MetallicTextureIndex;

		bool MetallicTextureIndexSpecified = false;

		int MetallicTextureCoord;

		bool MetallicTextureCoordSpecified = false;

		float MetallicFactor;

		bool MetallicFactorSpecified = false;

		float RoughnessFactor;

		bool RoughnessFactorSpecified = false;

		GLTFMaterial() : BaseColorFactor(Color::White) {}
	};

	class FURY_API GLTFBuffer
	{
	public:

		typedef std::shared_ptr<GLTFBuffer> Ptr;

		int ByteLength;

		std::string URI;
	};

	class FURY_API GLTFBufferView
	{
	public:

		typedef std::shared_ptr<GLTFBufferView> Ptr;

		int Buffer;

		int ByteOffset;

		int ByteLength;

		int ByteStride;

		bool ByteStrideSpecified = false;
	};

	class FURY_API GLTFAccessor
	{
	public:

		typedef std::shared_ptr<GLTFAccessor> Ptr;

		int BufferView;

		int ByteOffset;

		std::string Type;

		int ComponentType;

		int Count;

		bool Normalized;
	};

	class FURY_API GLTFTexture
	{
	public:

		typedef std::shared_ptr<GLTFTexture> Ptr;

		int Source;

		int Sampler;
	};

	class FURY_API GLTFImage 
	{
	public:

		typedef std::shared_ptr<GLTFImage> Ptr;

		std::string URI;

		bool URISpecified = false;

		int BufferView;

		bool BufferViewSpecified = false;

		std::string MIMEType;

		bool MIMETypeSpecified = false;
	};

	class FURY_API GLTFSampler
	{
	public:

		typedef std::shared_ptr<GLTFSampler> Ptr;

		int MagFilter;

		bool MagFilterSpecified = false;

		int MinFilter;

		bool MinFilterSpecified = false;

		int WrapS;

		bool WrapSSpecified = false;

		int WrapT;

		bool WrapTSpecified = false;
	};

	class FURY_API GLTFAnimChannel
	{
	public:

		typedef std::shared_ptr<GLTFAnimChannel> Ptr;

		int TargetNode;

		std::string TargetPath;

		int Sampler;
	};

	class FURY_API GLTFAnimSampler
	{
	public:

		typedef std::shared_ptr<GLTFAnimSampler> Ptr;

		// refer to time scalar
		int Input;

		std::string Interpolation;

		// refer to keyframe data
		int Output;
	};

	class FURY_API GLTFAnimation
	{
	public:

		typedef std::shared_ptr<GLTFAnimation> Ptr;

		std::string Name;

		std::vector<GLTFAnimChannel::Ptr> Channels;

		std::vector<GLTFAnimSampler::Ptr> Samplers;
	};

	class FURY_API GLTFDom : public Serializable
	{
	public:

		typedef std::shared_ptr<GLTFDom> Ptr;

		static const std::string SUPPORT_VERSION;

		static const std::string ACCESSOR_TYPE_SCALAR;

		static const std::string ACCESSOR_TYPE_VEC2;

		static const std::string ACCESSOR_TYPE_VEC3;

		static const std::string ACCESSOR_TYPE_VEC4;

		static const std::string ACCESSOR_TYPE_MAT2;

		static const std::string ACCESSOR_TYPE_MAT3;

		static const std::string ACCESSOR_TYPE_MAT4;

		static const int COMPONENT_TYPE_BYTE;

		static const int COMPONENT_TYPE_UNSIGNED_BYTE;

		static const int COMPONENT_TYPE_SHORT;

		static const int COMPONENT_TYPE_UNSIGNED_SHORT;

		static const int COMPONENT_TYPE_UNSIGNED_INT;

		static const int COMPONENT_TYPE_FLOAT;

		static const std::string ANIM_PATH_T;

		static const std::string ANIM_PATH_R;

		static const std::string ANIM_PATH_S;

		static const std::string MIME_JPEG;

		static const std::string MIME_PNG;

	public:

		std::vector<GLTFBuffer::Ptr> Buffers;

		std::vector<GLTFBufferView::Ptr> BufferViews;

		std::vector<GLTFScene::Ptr> Scenes;

		std::vector<GLTFAccessor::Ptr> Accessors;

		std::vector<GLTFImage::Ptr> Images;

		std::vector<GLTFSampler::Ptr> Samplers;

		std::vector<GLTFTexture::Ptr> Textures;

		std::vector<GLTFMaterial::Ptr> Materials;

		std::vector<GLTFMesh::Ptr> Meshes;

		std::vector<GLTFSkin::Ptr> Skins;

		std::vector<GLTFAnimation::Ptr> Animations;

		std::vector<GLTFNode::Ptr> Nodes;

		int Scene;

		std::string Version;

	public:

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		void Clear();

	private:

		bool LoadBuffer(const void* wrapper, bool object = true);

		bool LoadBufferView(const void* wrapper, bool object = true);

		bool LoadScene(const void* wrapper, bool object = true);

		bool LoadAccessor(const void* wrapper, bool object = true);

		bool LoadImage(const void* wrapper, bool object = true);

		bool LoadSampler(const void* wrapper, bool object = true);

		bool LoadTexture(const void* wrapper, bool object = true);

		bool LoadMaterial(const void* wrapper, bool object = true);

		bool LoadMesh(const void* wrapper, bool object = true);

		bool LoadPrimitive(GLTFPrimitive::Ptr ptr, const void* wrapper, bool object = true);

		bool LoadSkin(const void* wrapper, bool object = true);

		bool LoadAnimation(const void* wrapper, bool object = true);
		
		bool LoadAnimChannel(GLTFAnimChannel::Ptr ptr, const void* wrapper, bool object = true);

		bool LoadAnimSampler(GLTFAnimSampler::Ptr ptr, const void* wrapper, bool object = true);

		bool LoadNode(const void* wrapper, bool object = true);

		bool LoadAsset(const void* wrapper, bool object = true);
	};
}

#endif // _FURY_GLTFDOM_H_