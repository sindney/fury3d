#ifndef _FURY_ARRAYBUFFERS_H_
#define _FURY_ARRAYBUFFERS_H_

#include <string>
#include <vector>

#include "Buffer.h"
#include "Signal.h"
#include "TypeComparable.h"

namespace fury
{
	template<class DataType>
	class FURY_API ArrayBuffer : public Buffer, public TypeComparable
	{
	protected:

		std::type_index m_TypeIndex;

		unsigned int m_ID;

		unsigned int m_SizeOld;

		unsigned int m_BufferTarget;

		unsigned int m_BufferUsage;

	public:

		std::string Name;

		std::vector<DataType> Data;

		ArrayBuffer(const std::string &name, unsigned int bufferTarget, unsigned int bufferUsage);

		virtual std::type_index GetTypeIndex() const;

		virtual ~ArrayBuffer();

		void UpdateBuffer(bool force = false);

		virtual void DeleteBuffer();

		unsigned int GetID() const;

		void SetBufferUsage(unsigned int usage);
	};

	typedef ArrayBuffer<float> ArrayBufferf;

	typedef ArrayBuffer<int> ArrayBufferi;

	typedef ArrayBuffer<unsigned int> ArrayBufferui;
}

#endif // _FURY_ARRAYBUFFERS_H_