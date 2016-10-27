#include "Fury/Buffer.h"

namespace fury
{
	size_t Buffer::m_CurBufferId = 0;

	void Buffer::SetDirty()
	{
		m_Dirty = true;
	}

	bool Buffer::GetDirty() const
	{
		return m_Dirty;
	}

	size_t Buffer::GetBufferId() const
	{
		return m_BufferId;
	}
}