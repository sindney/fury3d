#include "Fury/ArrayBuffers.h"
#include "Fury/Log.h"
#include "Fury/RenderThread.h"
#include "Fury/RHI/RHIDevice.h"

namespace fury
{
	template<class DataType>
	ArrayBuffer<DataType>::ArrayBuffer(const std::string &name, RHIBufferType bufferType, RHIBufferUsage bufferUsage)
		: m_TypeIndex(typeid(ArrayBuffer<DataType>)), m_SizeOld(0),
		m_BufferType(bufferType), m_BufferUsage(bufferUsage), Name(name)
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
	void ArrayBuffer<DataType>::UpdateBuffer()
	{
		size_t sizeNew = Data.size();
		bool sizeChanged = false;

		if (sizeNew != m_SizeOld)
		{
			m_SizeOld = static_cast<unsigned int>(sizeNew);
			sizeChanged = true;
		}

		// An empty channel has nothing to upload; treat it as clean so
		// meshes without UVs (MeshUtil primitives) don't warn every frame.
		if (m_Dirty && sizeNew == 0)
			m_Dirty = false;

		if (m_Dirty && sizeNew > 0)
		{
			if (GRHIDevice == nullptr)
				return;

			m_Dirty = false;

			RHIBufferDesc desc;
			desc.Name = Name;
			desc.Type = m_BufferType;
			desc.Usage = m_BufferUsage;
			desc.Size = sizeNew * sizeof(DataType);
			desc.Stride = sizeof(DataType);
			desc.Index32 = sizeof(DataType) == 4;

			if (m_RHIBuffer == nullptr || sizeChanged)
				m_RHIBuffer = GRHIDevice->CreateBuffer(desc, Data.data());
			else
				GRHIDevice->UpdateBuffer(m_RHIBuffer, Data.data(), desc.Size, 0);
		}
	}

	template<class DataType>
	void ArrayBuffer<DataType>::DeleteBuffer()
	{
		m_Dirty = true;

		if (m_RHIBuffer != nullptr)
		{
			// GPU handles die on the render thread (direct-exec there).
			RHIBufferRef buffer = m_RHIBuffer;
			RenderThread::Get().EnqueueJob([buffer]() mutable { buffer.reset(); });
		}
		m_RHIBuffer = nullptr;
	}

	template<class DataType>
	unsigned int ArrayBuffer<DataType>::GetID() const
	{
		return m_RHIBuffer != nullptr ? (unsigned int)m_RHIBuffer->GetNativeHandle() : 0;
	}

	template<class DataType>
	RHIBufferRef ArrayBuffer<DataType>::GetRHIBuffer() const
	{
		return m_RHIBuffer;
	}

	template<class DataType>
	void ArrayBuffer<DataType>::SetBufferUsage(RHIBufferUsage usage)
	{
		if (m_BufferUsage != usage)
		{
			m_BufferUsage = usage;
			DeleteBuffer();
			UpdateBuffer();
		}
	}

	template class ArrayBuffer<float>;

	template class ArrayBuffer<int>;

	template class ArrayBuffer<unsigned int>;
}
