#include "Fury/BufferManager.h"

namespace fury
{
	void BufferManager::Remove(size_t id)
	{
		auto it = m_Buffers.find(id);
		if (it != m_Buffers.end())
			m_Buffers.erase(it);
	}

	void BufferManager::Release(size_t id)
	{
		auto it = m_Buffers.find(id);
		if (it != m_Buffers.end())
		{
			if (!it->second.expired())
				it->second.lock()->DeleteBuffer();

			m_Buffers.erase(it);
		}
	}

	void BufferManager::RemoveAll()
	{
		m_Buffers.clear();
	}

	void BufferManager::ReleaseAll()
	{
		for (auto pair : m_Buffers)
		{
			if (!pair.second.expired())
				pair.second.lock()->DeleteBuffer();
		}

		m_Buffers.clear();
	}

	void BufferManager::IncreaseMemory(unsigned int byte, bool gpu)
	{
		if (gpu)
			m_GPUMemories += byte;
		else
			m_GPUMemories += byte;
	}

	void BufferManager::DecreaseMemory(unsigned int byte, bool gpu)
	{
		if (gpu)
			m_GPUMemories -= byte;
		else
			m_CPUMemories -= byte;
	}

	unsigned int BufferManager::GetMemoryInMegaByte(bool gpu)
	{
		if (gpu)
			return m_GPUMemories / 1000000;
		else
			return m_CPUMemories / 1000000;
	}
}