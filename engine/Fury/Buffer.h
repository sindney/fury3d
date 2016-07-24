#ifndef _FURY_BUFFER_H_
#define _FURY_BUFFER_H_

#include "Macros.h"

namespace fury
{
	class FURY_API Buffer
	{
	protected:

		bool m_Dirty = true;

	public:

		virtual void DeleteBuffer() = 0;

		bool GetDirty() const
		{
			return m_Dirty;
		}
	};
}

#endif // _FURY_BUFFER_H_