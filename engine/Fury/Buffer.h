#ifndef _FURY_BUFFER_H_
#define _FURY_BUFFER_H_

#include <memory>

#include "Macros.h"

namespace fury
{
	class FURY_API Buffer
	{
	public:

		typedef std::shared_ptr<Buffer> Ptr;

	protected:

		static size_t m_CurBufferId;

		bool m_Dirty = true;

		size_t m_BufferId = m_CurBufferId++;

	public:

		virtual void UpdateBuffer() = 0;

		virtual void DeleteBuffer() = 0;

		void SetDirty();

		bool GetDirty() const;

		size_t GetBufferId() const;
	};
}

#endif // _FURY_BUFFER_H_