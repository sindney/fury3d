#ifndef _FURY_ARRAYBUFFERS_H_
#define _FURY_ARRAYBUFFERS_H_

#include <string>
#include <vector>

#include "Fury/Buffer.h"
#include "Fury/RHI/RHITypes.h"
#include "Fury/Signal.h"
#include "Fury/TypeComparable.h"

namespace fury
{
	template<class DataType>
	class FURY_API ArrayBuffer : public Buffer, public TypeComparable
	{
	protected:

		std::type_index m_TypeIndex;

		RHIBufferRef m_RHIBuffer;

		unsigned int m_SizeOld;

		RHIBufferType m_BufferType;

		RHIBufferUsage m_BufferUsage;

	public:

		std::string Name;

		std::vector<DataType> Data;

		ArrayBuffer(const std::string &name, RHIBufferType bufferType, RHIBufferUsage bufferUsage);

		virtual std::type_index GetTypeIndex() const;

		virtual ~ArrayBuffer();

		void UpdateBuffer();

		virtual void DeleteBuffer();

		unsigned int GetID() const;

		RHIBufferRef GetRHIBuffer() const;

		void SetBufferUsage(RHIBufferUsage usage);
	};

	typedef ArrayBuffer<float> ArrayBufferf;

	typedef ArrayBuffer<int> ArrayBufferi;

	typedef ArrayBuffer<unsigned int> ArrayBufferui;
}

#endif // _FURY_ARRAYBUFFERS_H_
