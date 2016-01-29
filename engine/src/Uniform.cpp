#include <type_traits>

#include "Debug.h"
#include "GLLoader.h"
#include "Uniform.h"

namespace fury
{
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
	void Uniform<Datatype, Size>::Bind(unsigned int program, const std::string &name)
	{
		unsigned int id = glGetUniformLocation(program, name.c_str());

		if (id == -1)
			return;

		if (std::is_floating_point<Datatype>::value)
		{
			switch (Size)
			{
			case 1:
				glUniform1f(id, (float)m_Data[0]);
				break;
			case 2:
				glUniform2f(id, (float)m_Data[0], (float)m_Data[1]);
				break;
			case 3:
				glUniform3f(id, (float)m_Data[0], (float)m_Data[1], (float)m_Data[2]);
				break;
			case 4:
				glUniform4f(id, (float)m_Data[0], (float)m_Data[1], (float)m_Data[2], (float)m_Data[3]);
				break;
			case 16:
				glUniformMatrix4fv(id, 1, false, (float*)&m_Data[0]);
				break;
			default:
				LOGW << "Invalide data size!";
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
					glUniform1ui(id, (int)m_Data[0]);
					break;
				case 2:
					glUniform2ui(id, (int)m_Data[0], (int)m_Data[1]);
					break;
				case 3:
					glUniform3ui(id, (int)m_Data[0], (int)m_Data[1], (int)m_Data[2]);
					break;
				case 4:
					glUniform4ui(id, (int)m_Data[0], (int)m_Data[1], (int)m_Data[2], (int)m_Data[3]);
					break;
				default:
					LOGW << "Invalide data size!";
					break;
				}
			}
			else
			{
				switch (Size)
				{
				case 1:
					glUniform1i(id, (unsigned int)m_Data[0]);
					break;
				case 2:
					glUniform2i(id, (unsigned int)m_Data[0], (unsigned int)m_Data[1]);
					break;
				case 3:
					glUniform3i(id, (unsigned int)m_Data[0], (unsigned int)m_Data[1], (unsigned int)m_Data[2]);
					break;
				case 4:
					glUniform4i(id, (unsigned int)m_Data[0], (unsigned int)m_Data[1], (unsigned int)m_Data[2], (unsigned int)m_Data[3]);
					break;
				default:
					LOGW << "Invalide data size!";
					break;
				}
			}
		}
	}

	template<typename Datatype, unsigned int Size>
	void Uniform<Datatype, Size>::SetData(std::initializer_list<Datatype> data)
	{
		if (data.size() != m_Size)
		{
			LOGW << "Invalide data size!";
			return;
		}

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