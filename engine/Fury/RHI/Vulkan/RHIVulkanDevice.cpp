#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_VULKAN

#include "Fury/Log.h"

namespace fury
{
	RHIDevice *CreateVulkanDevice(const RHIWindowInfo &window)
	{
		FURYE << "VulkanRHI device creation is not implemented yet";
		return nullptr;
	}
}

#endif // FURY_RHI_VULKAN
