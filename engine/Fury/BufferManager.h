#ifndef _FURY_BUFFER_MANAGER_H_
#define _FURY_BUFFER_MANAGER_H_

#include <unordered_map>
#include <type_traits>

#include "Fury/Buffer.h"
#include "Fury/Signal.h"
#include "Fury/Singleton.h"

namespace fury
{
	class FURY_API BufferManager final : public Singleton<BufferManager>
	{
	public:

		typedef std::shared_ptr<BufferManager> Ptr;

	private:

		std::unordered_map<size_t, std::weak_ptr<Buffer>> m_Buffers;

		// in byte
		unsigned int m_CPUMemories = 0;

		// in byte
		unsigned int m_GPUMemories = 0;

	public:

		template<class BufferType>
		bool Add(const std::shared_ptr<BufferType> &buffer)
		{
			static_assert(std::is_base_of<Buffer, BufferType>::value, "BufferType should extend Buffer Class");

			auto it = m_Buffers.find(buffer->GetBufferId());
			if (it != m_Buffers.end())
			{
				m_Buffers.emplace(buffer->GetBufferId(), std::static_pointer_cast<Buffer>(buffer));
				return true;
			}
			else
			{
				return false;
			}
		}

		// stop tracking buffer
		void Remove(size_t id);

		// stop tracking and release memories
		void Release(size_t id);

		void RemoveAll();

		void ReleaseAll();

		void IncreaseMemory(unsigned int byte, bool gpu = true);

		void DecreaseMemory(unsigned int byte, bool gpu = true);

		unsigned int GetMemoryInMegaByte(bool gpu = true);
	};
}

#endif // _FURY_BUFFER_MANAGER_H_