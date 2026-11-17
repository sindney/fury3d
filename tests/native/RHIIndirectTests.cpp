#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/RHIDevice.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <type_traits>

namespace
{
	using namespace fury;

	static_assert(std::is_standard_layout_v<RHIIndirectDrawArgs>);
	static_assert(std::is_trivially_copyable_v<RHIIndirectDrawArgs>);
	static_assert(sizeof(RHIIndirectDrawArgs) == 20);
	static_assert(offsetof(RHIIndirectDrawArgs, indexCount) == 0);
	static_assert(offsetof(RHIIndirectDrawArgs, instanceCount) == 4);
	static_assert(offsetof(RHIIndirectDrawArgs, firstIndex) == 8);
	static_assert(offsetof(RHIIndirectDrawArgs, vertexOffset) == 12);
	static_assert(offsetof(RHIIndirectDrawArgs, firstInstance) == 16);
	static_assert(RHI_BUFFER_TRANSFER_SRC != RHI_BUFFER_INDIRECT);

	int failures = 0;

	void Check(bool condition, const char *expression)
	{
		if (condition)
			return;
		std::cerr << "FAILED: " << expression << std::endl;
		++failures;
	}

#define CHECK(expression) Check((expression), #expression)
}

int main()
{
	RHIIndirectDrawArgs args;
	CHECK(args.instanceCount == 1);
	CHECK(args.firstInstance == 0);

	RHIBufferDesc buffer;
	CHECK(buffer.UsageFlags == 0);
	buffer.UsageFlags = RHI_BUFFER_TRANSFER_SRC | RHI_BUFFER_INDIRECT;
	CHECK(buffer.UsageFlags == 3);

	RHIIndirectBatchDesc batch;
	CHECK(batch.Capacity == 0);
	CHECK(batch.CountStride == 4);
	CHECK(batch.ArgumentOffset == 0);
	CHECK(batch.CountOffset == 0);
	CHECK(!batch.Arguments);
	CHECK(!batch.Counts);

	RHICapabilities capabilities;
	CHECK(!capabilities.SupportsIndirectCount);
	CHECK(!capabilities.SupportsMultiDrawIndirect);
	CHECK(!capabilities.SupportsIndirectFirstInstance);
	CHECK(!capabilities.SupportsShaderDrawID);
	CHECK(!capabilities.SupportsNativeIndirectBatchEncoding);
	CHECK(capabilities.MaxDrawIndirectCount == 0);
	CHECK(capabilities.IndirectArgumentStride == 0);
	CHECK(capabilities.MaxStorageBufferRange == 0);

	RHIBufferReadbackRequest request;
	RHIBufferReadbackResult result;
	CHECK(request.FrameIndex == 0);
	CHECK(result.FrameIndex == 0);
	CHECK(result.Bytes.empty());

	if (failures != 0)
		return 1;
	std::cout << "RHI indirect API tests passed" << std::endl;
	return 0;
}
