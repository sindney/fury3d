#ifndef _FURY_METAL_INDIRECT_ENCODER_H_
#define _FURY_METAL_INDIRECT_ENCODER_H_

#include "Fury/RHI/RHIBackend.h"

#include <cstdint>
#include <string>

namespace fury
{
	inline constexpr std::uint32_t MetalIndirectEncoderVersion = 2;
	extern const char *const MetalIndirectEncoderSource;
	std::string MetalIndirectEncoderCacheKey();

#if FURY_RHI_METAL
	constexpr std::uint32_t MetalIndirectMaxVertexBuffers = 4;
	constexpr std::uint32_t MetalIndirectMaxStorageBuffers = 32;

	constexpr std::uint32_t MetalIndirectResourceCommands = 0;
	constexpr std::uint32_t MetalIndirectResourcePipeline = 1;
	constexpr std::uint32_t MetalIndirectResourceArguments = 2;
	constexpr std::uint32_t MetalIndirectResourceCounts = 3;
	constexpr std::uint32_t MetalIndirectResourceRanges = 4;
	constexpr std::uint32_t MetalIndirectResourceIndices = 5;
	constexpr std::uint32_t MetalIndirectResourceVertices = 6;
	constexpr std::uint32_t MetalIndirectResourceStorage = MetalIndirectResourceVertices + MetalIndirectMaxVertexBuffers;
	constexpr std::uint32_t MetalIndirectResourceFragmentArguments = MetalIndirectResourceStorage + MetalIndirectMaxStorageBuffers;
	constexpr std::uint32_t MetalIndirectResourceGlobalArguments = MetalIndirectResourceFragmentArguments + 1;
	constexpr std::uint32_t MetalIndirectResourceCount = MetalIndirectResourceGlobalArguments + 2;
	constexpr std::uint32_t MetalIndirectResourceDescriptorCount = 10;

	struct MetalIndirectBinding
	{
		std::uint32_t vertexIndex;
		std::uint32_t fragmentIndex;
	};

	struct MetalIndirectEncoderConfig
	{
		std::uint32_t argumentOffset;
		std::uint32_t countOffset;
		std::uint32_t countStride;
		std::uint32_t capacity;
		std::uint32_t maxDrawCount;
		std::uint32_t index32;
		std::uint32_t primitive;
		std::uint32_t storageCount;
		std::uint32_t vertexBufferBase;
		std::uint32_t fragmentArgumentIndex;
		std::uint32_t globalArgumentIndices[2];
		MetalIndirectBinding storage[MetalIndirectMaxStorageBuffers];
	};
#endif // FURY_RHI_METAL
}

#endif // _FURY_METAL_INDIRECT_ENCODER_H_
