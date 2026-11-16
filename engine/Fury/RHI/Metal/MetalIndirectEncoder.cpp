#include "Fury/RHI/Metal/MetalIndirectEncoder.h"

#include "Fury/DdcStore.h"
#include "Fury/HashUtil.h"

namespace fury
{
	const char *const MetalIndirectEncoderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct NeutralArgs
{
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int vertexOffset;
	uint firstInstance;
};

struct Binding
{
	uint vertexIndex;
	uint fragmentIndex;
};

struct EncoderConfig
{
	uint argumentOffset;
	uint countOffset;
	uint countStride;
	uint capacity;
	uint maxDrawCount;
	uint index32;
	uint primitive;
	uint storageCount;
	uint vertexBufferBase;
	uint fragmentArgumentIndex;
	uint globalArgumentIndices[2];
	Binding storage[32];
};

struct EncoderResources
{
	command_buffer commands;
	render_pipeline_state pipeline;
	device const NeutralArgs *arguments;
	device const uint *counts;
	device MTLIndirectCommandBufferExecutionRange *ranges;
	device const void *indices;
	device const void *vertices[4];
	device void *storage[32];
	device const void *fragmentArguments;
	device const void *globalArguments[2];
};

kernel void computeMain(constant EncoderResources &resources [[buffer(0)]],
	constant EncoderConfig &config [[buffer(1)]],
	device const NeutralArgs *inputArgs [[buffer(2)]], device const uint *inputCounts [[buffer(3)]],
	device MTLIndirectCommandBufferExecutionRange *outputRanges [[buffer(4)]],
	device const void *inputIndices [[buffer(5)]], uint id [[thread_position_in_grid]])
{
	const uint chunkCount = (config.capacity + config.maxDrawCount - 1) / config.maxDrawCount;
	if (id < chunkCount)
	{
		const uint first = id * config.maxDrawCount;
		const uint limit = min(config.maxDrawCount, config.capacity - first);
		outputRanges[id].location = first;
		outputRanges[id].length = min(inputCounts[config.countOffset + id * config.countStride], limit);
	}
	if (id >= config.capacity)
		return;

	render_command command(resources.commands, id);
	command.reset();
	command.set_render_pipeline_state(resources.pipeline);
	for (uint i = 0; i < 4; ++i)
	{
		if (resources.vertices[i] != nullptr)
			command.set_vertex_buffer(resources.vertices[i], config.vertexBufferBase + i);
	}
	for (uint i = 0; i < config.storageCount; ++i)
	{
		if (config.storage[i].vertexIndex != 0xffffffffu)
			command.set_vertex_buffer(resources.storage[i], config.storage[i].vertexIndex);
		if (config.storage[i].fragmentIndex != 0xffffffffu)
			command.set_fragment_buffer(resources.storage[i], config.storage[i].fragmentIndex);
	}
	if (config.fragmentArgumentIndex != 0xffffffffu && resources.fragmentArguments != nullptr)
		command.set_fragment_buffer(resources.fragmentArguments, config.fragmentArgumentIndex);
	if (config.globalArgumentIndices[0] != 0xffffffffu && resources.globalArguments[0] != nullptr)
		command.set_vertex_buffer(resources.globalArguments[0], config.globalArgumentIndices[0]);
	if (config.globalArgumentIndices[1] != 0xffffffffu && resources.globalArguments[1] != nullptr)
		command.set_fragment_buffer(resources.globalArguments[1], config.globalArgumentIndices[1]);

	const NeutralArgs args = inputArgs[config.argumentOffset + id];
	if (config.index32 != 0)
	{
		const device uint *indices = static_cast<const device uint *>(inputIndices) + args.firstIndex;
		command.draw_indexed_primitives(primitive_type(config.primitive), args.indexCount,
			indices, args.instanceCount, uint(args.vertexOffset), args.firstInstance);
	}
	else
	{
		const device ushort *indices = static_cast<const device ushort *>(inputIndices) + args.firstIndex;
		command.draw_indexed_primitives(primitive_type(config.primitive), args.indexCount,
			indices, args.instanceCount, uint(args.vertexOffset), args.firstInstance);
	}
}
)MSL";

	std::string MetalIndirectEncoderCacheKey()
	{
		return DdcStore::MakeKey("MSLICB_", "version=" + std::to_string(MetalIndirectEncoderVersion) + "|msl=2.1",
			HashUtil::Sha1Hex(MetalIndirectEncoderSource));
	}
}
