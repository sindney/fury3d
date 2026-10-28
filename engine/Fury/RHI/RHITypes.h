#ifndef _FURY_RHI_TYPES_H_
#define _FURY_RHI_TYPES_H_

#include <memory>
#include <string>
#include <vector>

#include "Fury/Color.h"
#include "Fury/EnumUtil.h"
#include "Fury/Macros.h"

namespace fury
{
	class RHITexture;

	class RHIBuffer;

	class RHISampler;

	class RHIShaderModule;

	class RHIGraphicsPipeline;

	class RHIRenderTargetSet;

	typedef std::shared_ptr<RHITexture> RHITextureRef;

	typedef std::shared_ptr<RHIBuffer> RHIBufferRef;

	typedef std::shared_ptr<RHISampler> RHISamplerRef;

	typedef std::shared_ptr<RHIShaderModule> RHIShaderModuleRef;

	typedef std::shared_ptr<RHIGraphicsPipeline> RHIGraphicsPipelineRef;

	typedef std::shared_ptr<RHIRenderTargetSet> RHIRenderTargetSetRef;

	// Backend-resolved uniform identifier (GL location, reflection-table
	// index elsewhere). -1 = uniform absent.
	typedef int RHIUniformHandle;

	enum class RHIBackend : unsigned int
	{
		OPENGL = 0,
		VULKAN,
		METAL
	};

	// Sampler state is carried on the texture (GL texparam model); backends
	// that need standalone sampler objects create a paired one from this desc.
	struct RHISamplerDesc
	{
		FilterMode Filter = FilterMode::LINEAR;

		WrapMode Wrap = WrapMode::REPEAT;

		Color BorderColor = Color(0, 0, 0, 0);

		// Shadow comparison sampler (sampler2DShadow / sampler2DArrayShadow / samplerCubeShadow).
		bool ShadowCompare = false;
	};

	struct RHITextureDesc
	{
		std::string Name;

		TextureType Type = TextureType::TEXTURE_2D;

		TextureFormat Format = TextureFormat::RGBA8;

		int Width = 0;

		int Height = 0;

		// Array layer count (TEXTURE_2D_ARRAY) or depth (TEXTURE_3D).
		int Depth = 1;

		// Allocated mip count. 1 = base level only; FURY_MIPMAP_LEVEL when
		// mips are generated; file-provided count for KTX2 uploads.
		int MipLevels = 1;

		// Usable as a render-target attachment.
		bool RenderTarget = false;

		RHISamplerDesc Sampler;
	};

	enum class RHIBufferType : unsigned int
	{
		VERTEX = 0,
		INDEX,
		UNIFORM,
		STORAGE
	};

	enum class RHIBufferUsage : unsigned int
	{
		STATIC = 0,
		DYNAMIC
	};

	struct RHIBufferDesc
	{
		std::string Name;

		RHIBufferType Type = RHIBufferType::VERTEX;

		RHIBufferUsage Usage = RHIBufferUsage::STATIC;

		size_t Size = 0;

		// Vertex stride; element size for storage buffers.
		unsigned int Stride = 0;

		// Index buffers only: 16 or 32 bit elements.
		bool Index32 = true;
	};

	enum class RHIVertexFormat : unsigned int
	{
		FLOAT = 0,
		FLOAT2,
		FLOAT3,
		FLOAT4,
		INT4,
		UINT4
	};

	// GL resolves by attribute name; Vulkan/Metal resolve via reflection.
	struct RHIVertexAttribute
	{
		std::string Name;

		RHIVertexFormat Format = RHIVertexFormat::FLOAT3;

		unsigned int Offset = 0;

		unsigned int Stride = 0;

		// Per-instance divisor (divisor-VBO instancing path).
		unsigned int Divisor = 0;

		// -1 = declaration order; >= 0 = explicit attribute index (mesh
		// channel layouts resolve indices from the shader module).
		int Location = -1;

		// Vertex stream (buffer index) this attribute reads from.
		unsigned int Stream = 0;

		// Constant-fill stream (stride-0 constant buffer standing in for
		// GL's disabled attribute = constant (0,0,0,1)).
		bool ConstantFill = false;
	};

	// One mesh vertex channel as backends need it. Attrib is the
	// module-resolved attribute index (-1 = skip). A null Buffer with Empty
	// set binds the constant-fill stream (GL's disabled-attribute constant).
	struct RHIMeshChannelDesc
	{
		const char *Name = "";

		int Attrib = -1;

		RHIBufferRef Buffer;

		bool Dirty = false;

		bool Empty = true;

		int Components = 3;

		bool Integer = false;
	};

	struct RHIMeshGeometryDesc
	{
		// constexpr (not const): odr-used by reference in std::min.
		static constexpr int MAX_CHANNELS = 8;

		RHIMeshChannelDesc Channels[MAX_CHANNELS];

		int ChannelCount = 0;

		const char *MeshName = "";
	};

	enum class RHIPrimitiveMode : unsigned int
	{
		TRIANGLES = 0,
		LINES,
		LINE_STRIP,
		// Debug draws only; not portable to Metal (emulate there).
		LINE_LOOP
	};

	enum class RHIFillMode : unsigned int
	{
		SOLID = 0,
		// Debug draws; Vulkan needs the fillModeNonSolid feature.
		WIREFRAME
	};

	// Shader blob handed to CreateShaderModule: source text for GL/Metal,
	// SPIR-V binary for Vulkan.
	struct RHIShaderBlob
	{
		std::vector<unsigned char> Data;

		bool IsSource = true;
	};

	struct RHIGraphicsPipelineDesc
	{
		std::string Name;

		BlendMode Blend = BlendMode::REPLACE;

		CompareMode DepthFunc = CompareMode::LESS;

		bool DepthTest = true;

		bool DepthWrite = true;

		CullMode Cull = CullMode::BACK;

		RHIPrimitiveMode Primitive = RHIPrimitiveMode::TRIANGLES;

		RHIFillMode Fill = RHIFillMode::SOLID;

		std::vector<RHIVertexAttribute> VertexAttributes;
	};

	struct RHIRenderTargetAttachment
	{
		std::shared_ptr<RHITexture> Texture;

		int MipLevel = 0;

		// -1: attach all layers (layered rendering); else one array layer.
		int Layer = -1;

		// -1: not a cube attachment; else cube face 0-5.
		int CubeFace = -1;
	};

	struct RHIRenderTargetSetDesc
	{
		std::vector<RHIRenderTargetAttachment> Colors;

		RHIRenderTargetAttachment Depth;

		bool HasDepth = false;
	};

	struct RHICapabilities
	{
		// false on GL ([-1,1]), true on Vulkan/Metal ([0,1]).
		bool NdcDepthZeroToOne = false;

		// true when framebuffer Y points down (Vulkan/Metal) and projections must flip.
		bool ClipSpaceYDown = false;

		// true when the backbuffer format always sRGB-encodes shader output.
		bool BackbufferSRGB = false;

		// true when UI passes can use an unorm view of an sRGB backbuffer.
		bool SupportsUnormBackbufferView = false;

		bool SupportsSSBO = false;

		bool SupportsCompute = false;

		bool SupportsTexture3D = false;

		bool SupportsCompressedBC = false;

		int MaxTextureSize = 0;

		int MaxTextureArrayLayers = 0;
	};
}

#endif // _FURY_RHI_TYPES_H_
