#include "Fury/RHI/RHIBackend.h"

#if FURY_RHI_VULKAN

#include <volk.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

#endif // FURY_RHI_VULKAN
