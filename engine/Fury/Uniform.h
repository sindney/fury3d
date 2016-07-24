#ifndef _FURY_UNIFORM_H_
#define _FURY_UNIFORM_H_

#include <memory>
#include <string>
#include <initializer_list>
#include <unordered_map>

#include "Fury/TypeComparable.h"
#include "Fury/Serializable.h"

namespace fury
{
	class FURY_API UniformBase : public TypeComparable, public Serializable
	{
	public:

		typedef std::shared_ptr<UniformBase> Ptr;

		UniformBase();

		virtual void Bind(unsigned int program, const std::string &name) = 0;

		virtual bool Load(const void* wrapper, bool object = true) override = 0;

		virtual void Save(void* wrapper, bool object = true) override = 0;

		virtual std::type_index GetTypeIndex() const override;

		unsigned int GetSize() const;

	protected:

		std::type_index m_TypeIndex;

		unsigned int m_Size;

		static const std::unordered_map<std::type_index, std::string> m_UniformTypeMap;
	};

	template<typename Datatype, unsigned int Size>
	class FURY_API Uniform : public UniformBase
	{
	protected:

		Datatype m_Data[Size];

	public:

		typedef std::shared_ptr<Uniform<Datatype, Size>> Ptr;

		static Ptr Create(std::initializer_list<Datatype> data);

		Uniform();

		virtual void Bind(unsigned int program, const std::string &name) override;

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		void SetData(std::initializer_list<Datatype> data);

		Datatype GetDataAt(unsigned int index);
	};

	typedef Uniform<float, 1> Uniform1f;

	typedef Uniform<float, 2> Uniform2f;

	typedef Uniform<float, 3> Uniform3f;

	typedef Uniform<float, 4> Uniform4f;

	typedef Uniform<float, 16> UniformMatrix4fv;

	typedef Uniform<int, 1> Uniform1i;

	typedef Uniform<int, 2> Uniform2i;

	typedef Uniform<int, 3> Uniform3i;

	typedef Uniform<int, 4> Uniform4i;

	typedef Uniform<unsigned int, 1> Uniform1ui;

	typedef Uniform<unsigned int, 2> Uniform2ui;

	typedef Uniform<unsigned int, 3> Uniform3ui;

	typedef Uniform<unsigned int, 4> Uniform4ui;
}

#endif // _FURY_UNIFORM_H_