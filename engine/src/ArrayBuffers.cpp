#include "ArrayBuffers.h"
#include "Log.h"
#include "GLLoader.h"

namespace fury
{
	template<class DataType>
	ArrayBuffer<DataType>::ArrayBuffer(const std::string &name, unsigned int bufferTarget, unsigned int bufferUsage)
		: m_ID(0), m_SizeOld(0), Name(name), 
		m_BufferTarget(bufferTarget), m_BufferUsage(bufferUsage), 
		m_TypeIndex(typeid(ArrayBuffer<DataType>))
	{

	}

	template<class DataType>
	ArrayBuffer<DataType>::~ArrayBuffer()
	{
		DeleteBuffer();
	}

	template<class DataType>
	std::type_index ArrayBuffer<DataType>::GetTypeIndex() const
	{
		return m_TypeIndex;
	}

	template<class DataType>
	void ArrayBuffer<DataType>::UpdateBuffer(bool force)
	{
		int sizeNew = Data.size();
		bool sizeChanged = false;
		bool isNewBuffer = false;

		if (force)
			m_Dirty = true;

		if (sizeNew != m_SizeOld)
		{
			m_SizeOld = sizeNew;
			sizeChanged = true;
		}

		if (m_Dirty && sizeNew > 0)
		{
			m_Dirty = false;

			if (m_ID == 0)
			{
				glGenBuffers(1, &m_ID);
				isNewBuffer = true;
			}

			glBindBuffer(m_BufferTarget, m_ID);

			if (sizeChanged || isNewBuffer)
				glBufferData(m_BufferTarget, sizeNew * sizeof(DataType), Data.data(), m_BufferUsage);
			else
				glBufferSubData(m_BufferTarget, 0, sizeNew * sizeof(DataType), Data.data());

			glBindBuffer(m_BufferTarget, 0);
		}
	}

	template<class DataType>
	void ArrayBuffer<DataType>::DeleteBuffer()
	{
		m_Dirty = true;
		
		if (m_ID != 0)
			glDeleteBuffers(1, &m_ID);
		m_ID = 0;
	}

	template<class DataType>
	unsigned int ArrayBuffer<DataType>::GetID() const
	{
		return m_ID;
	}

	template<class DataType>
	void ArrayBuffer<DataType>::SetBufferUsage(unsigned int usage)
	{
		if (m_BufferUsage != usage)
		{
			DeleteBuffer();
			UpdateBuffer();
		}
	}

	template class ArrayBuffer<float>;

	template class ArrayBuffer<int>;

	template class ArrayBuffer<unsigned int>;
}