#include <type_traits>

#include "Fury/Log.h"
#include "Fury/Uniform.h"
#include "Fury/Serializable.h"

#include "Fury/RHI/RHICommandList.h"
#include "Fury/RHI/RHIDevice.h"

namespace fury
{
	namespace
	{
		RHICommandList *Cmd()
		{
			return GRHIDevice ? GRHIDevice->GetCommandList() : nullptr;
		}
	}

	const std::unordered_map<std::type_index, std::string> UniformBase::m_UniformTypeMap =
	{
		{ typeid(Uniform1f), "Uniform1f" },
		{ typeid(Uniform2f), "Uniform2f" },
		{ typeid(Uniform3f), "Uniform3f" },
		{ typeid(Uniform4f), "Uniform4f" },
		{ typeid(UniformMatrix4fv), "UniformMatrix4fv" },
		{ typeid(Uniform1i), "Uniform1i" },
		{ typeid(Uniform2i), "Uniform2i" },
		{ typeid(Uniform3i), "Uniform3i" },
		{ typeid(Uniform4i), "Uniform4i" },
		{ typeid(Uniform1ui), "Uniform1ui" },
		{ typeid(Uniform2ui), "Uniform2ui" },
		{ typeid(Uniform3ui), "Uniform3ui" },
		{ typeid(Uniform4ui), "Uniform4ui" }
	};

	UniformBase::UniformBase()
		: m_TypeIndex(typeid(UniformBase)), m_Size(0)
	{

	}

	std::type_index UniformBase::GetTypeIndex() const
	{
		return m_TypeIndex;
	}

	unsigned int UniformBase::GetSize() const
	{
		return m_Size;
	}

	template<typename Datatype, unsigned int Size>
	typename Uniform<Datatype, Size>::Ptr Uniform<Datatype, Size>::Create(std::initializer_list<Datatype> data)
	{
		auto ptr = std::make_shared<Uniform<Datatype, Size>>();
		ptr->SetData(data);
		return ptr;
	}

	template<typename Datatype, unsigned int Size>
	Uniform<Datatype, Size>::Uniform()
	{
		m_TypeIndex = typeid(Datatype);
		m_Size = Size;
	}

	template<typename Datatype, unsigned int Size>
	void Uniform<Datatype, Size>::Bind(const std::string &name)
	{
		RHICommandList *cmd = Cmd();
		if (cmd == nullptr)
			return;

		if (std::is_floating_point<Datatype>::value)
		{
			switch (Size)
			{
			case 1:
				cmd->SetUniformFloat(name, (float)m_Data[0]);
				break;
			case 2:
				cmd->SetUniformFloat(name, (float)m_Data[0], (float)m_Data[1]);
				break;
			case 3:
				cmd->SetUniformFloat(name, (float)m_Data[0], (float)m_Data[1], (float)m_Data[2]);
				break;
			case 4:
				cmd->SetUniformFloat(name, (float)m_Data[0], (float)m_Data[1], (float)m_Data[2], (float)m_Data[3]);
				break;
			case 16:
				cmd->SetUniformMatrix(name, (const float*)&m_Data[0]);
				break;
			default:
				FURYW << "Invalide data size!";
				break;
			}
		}
		else if (std::is_integral<Datatype>::value)
		{
			if (std::is_signed<Datatype>::value)
			{
				switch (Size)
				{
				case 1:
					cmd->SetUniformInt(name, (int)m_Data[0]);
					break;
				case 2:
					cmd->SetUniformInt(name, (int)m_Data[0], (int)m_Data[1]);
					break;
				case 3:
					cmd->SetUniformInt(name, (int)m_Data[0], (int)m_Data[1], (int)m_Data[2]);
					break;
				case 4:
					cmd->SetUniformInt(name, (int)m_Data[0], (int)m_Data[1], (int)m_Data[2], (int)m_Data[3]);
					break;
				default:
					FURYW << "Invalide data size!";
					break;
				}
			}
			else
			{
				// No multi-value uint RHI entry; int uploads match for in-range values.
				switch (Size)
				{
				case 1:
					cmd->SetUniformUInt(name, (unsigned int)m_Data[0]);
					break;
				case 2:
					cmd->SetUniformInt(name, (int)m_Data[0], (int)m_Data[1]);
					break;
				case 3:
					cmd->SetUniformInt(name, (int)m_Data[0], (int)m_Data[1], (int)m_Data[2]);
					break;
				case 4:
					cmd->SetUniformInt(name, (int)m_Data[0], (int)m_Data[1], (int)m_Data[2], (int)m_Data[3]);
					break;
				default:
					FURYW << "Invalide data size!";
					break;
				}
			}
		}
	}

	template<typename Datatype, unsigned int Size>
	void Uniform<Datatype, Size>::BindAt(RHIUniformHandle handle)
	{
		if (handle == -1)
			return;

		RHICommandList *cmd = Cmd();
		if (cmd == nullptr)
			return;

		if (std::is_floating_point<Datatype>::value)
		{
			switch (Size)
			{
			case 1:
				cmd->SetUniformFloatAt(handle, (float)m_Data[0]);
				break;
			case 2:
				cmd->SetUniformFloatAt(handle, (float)m_Data[0], (float)m_Data[1]);
				break;
			case 3:
				cmd->SetUniformFloatAt(handle, (float)m_Data[0], (float)m_Data[1], (float)m_Data[2]);
				break;
			case 4:
				cmd->SetUniformFloatAt(handle, (float)m_Data[0], (float)m_Data[1], (float)m_Data[2], (float)m_Data[3]);
				break;
			default:
				break;
			}
		}
		else
		{
			switch (Size)
			{
			case 1:
				cmd->SetUniformIntAt(handle, (int)m_Data[0]);
				break;
			case 2:
			case 3:
			case 4:
				// RHI has no multi-value integer At entry.
				FURYW << "Integer vector BindAt is not supported by the RHI!";
				break;
			default:
				break;
			}
		}
	}

	template<typename Datatype, unsigned int Size>
	bool Uniform<Datatype, Size>::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		std::vector<Datatype> raw;
		raw.reserve(Size);
		if (!LoadArray(wrapper, "data", raw))
			return false;

		for (unsigned int i = 0; i < Size; i++)
			m_Data[i] = raw[i];

		return true;
	}

	template<typename Datatype, unsigned int Size>
	void Uniform<Datatype, Size>::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		auto it = m_UniformTypeMap.find((std::type_index)typeid(Uniform<Datatype, Size>));
		if (it == m_UniformTypeMap.end())
		{
			FURYE << "Uniform Type not found!";
			return;
		}

		SaveKey(wrapper, "type");
		SaveValue(wrapper, it->second);

		SaveKey(wrapper, "data");
		SaveArray(wrapper, Size, [&](unsigned int index)
		{
			SaveValue(wrapper, m_Data[index]);
		});

		if (object)
			EndObject(wrapper);
	}

	template<typename Datatype, unsigned int Size>
	void Uniform<Datatype, Size>::SetData(std::initializer_list<Datatype> data)
	{
		if (data.size() != m_Size)
			return;

		for (auto it = data.begin(); it != data.end(); ++it)
		{
			m_Data[it - data.begin()] = *it;
		}
	}

	template<typename Datatype, unsigned int Size>
	Datatype Uniform<Datatype, Size>::GetDataAt(unsigned int index)
	{
		if (index > Size)
			return 0;

		return m_Data[index];
	}

	template class Uniform < float, 1 > ;

	template class Uniform < float, 2 > ;

	template class Uniform < float, 3 > ;

	template class Uniform < float, 4 > ;

	template class Uniform < float, 16 > ;

	template class Uniform < int, 1 > ;

	template class Uniform < int, 2 > ;

	template class Uniform < int, 3 > ;

	template class Uniform < int, 4 > ;

	template class Uniform < unsigned int, 1 > ;

	template class Uniform < unsigned int, 2 > ;

	template class Uniform < unsigned int, 3 > ;

	template class Uniform < unsigned int, 4 > ;

}
