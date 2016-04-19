#include <fstream>

#include <rapidjson/document.h>
#include <rapidjson/PrettyWriter.h>
#include <rapidjson/stringbuffer.h>

#include "Log.h"
#include "Serializable.h"

namespace fury
{
	using namespace rapidjson;

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, bool &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);

		Value::ConstMemberIterator it = dom.FindMember(name.c_str());
		if (it == dom.MemberEnd() || !it->value.IsBool())
			return false;

		value = it->value.GetBool();
		return true;
	}

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, int &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);

		Value::ConstMemberIterator it = dom.FindMember(name.c_str());
		if (it == dom.MemberEnd() || !it->value.IsInt())
			return false;

		value = it->value.GetInt();
		return true;
	}

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, float &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);

		Value::ConstMemberIterator it = dom.FindMember(name.c_str());
		if (it == dom.MemberEnd() || !it->value.IsDouble())
			return false;

		value = it->value.GetDouble();
		return true;
	}

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, std::string &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);

		Value::ConstMemberIterator it = dom.FindMember(name.c_str());
		if (it == dom.MemberEnd() || !it->value.IsString())
			return false;

		value = std::string(it->value.GetString(), it->value.GetStringLength());
		return true;
	}

	bool Serializable::LoadValue(const void* wrapper, bool &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);
		if (dom.IsBool())
		{
			value = dom.GetBool();
			return true;
		}
		else
		{
			return false;
		}
	}

	bool Serializable::LoadValue(const void* wrapper, int &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);
		if (dom.IsInt())
		{
			value = dom.GetInt();
			return true;
		}
		else
		{
			return false;
		}
	}

	bool Serializable::LoadValue(const void* wrapper, float &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);
		if (dom.IsDouble())
		{
			value = dom.GetDouble();
			return true;
		}
		else
		{
			return false;
		}
	}

	bool Serializable::LoadValue(const void* wrapper, std::string &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);
		if (dom.IsString())
		{
			value = std::string(dom.GetString(), dom.GetStringLength());
			return true;
		}
		else
		{
			return false;
		}
	}

	bool Serializable::LoadArray(const void* wrapper, const std::string &name, std::function<bool(const void*)> walker)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);

		Value::ConstMemberIterator array = dom.FindMember(name.c_str());
		if (array == dom.MemberEnd() || !array->value.IsArray())
			return false;

		for (Value::ConstValueIterator it = array->value.Begin(); it != array->value.End(); ++it)
		{
			if (!walker(&(*it))) return false;
		}

		return true;
	}

	void Serializable::SaveKey(void* wrapper, const std::string &key)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->Key(key.c_str(), key.size());
	}

	void Serializable::SaveValue(void* wrapper, bool value)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->Bool(value);
	}

	void Serializable::SaveValue(void* wrapper, int value)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->Int(value);
	}

	void Serializable::SaveValue(void* wrapper, float value)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->Double(value);
	}

	void Serializable::SaveValue(void* wrapper, const std::string &value)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->String(value.c_str(), value.size());
	}

	void Serializable::SaveArray(void *wrapper, unsigned int count, std::function<void(unsigned int)> walker)
	{
		auto writter = static_cast<PrettyWriter<StringBuffer>*>(wrapper);
		writter->StartArray();

		for (unsigned int i = 0; i < count; i++)
			walker(i);

		writter->EndArray();
	}

	void Serializable::StartObject(void* wrapper)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->StartObject();
	}

	void Serializable::EndObject(void* wrapper)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->EndObject();
	}

	void Serializable::StartArray(void* wrapper)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->StartArray();
	}

	void Serializable::EndArray(void* wrapper)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->EndArray();
	}

	bool Serializable::IsObject(const void* wrapper)
	{
		return static_cast<const Value*>(wrapper)->IsObject();
	}

	bool Serializable::IsObject(const void* wrapper, const std::string &member)
	{
		auto value = static_cast<const Value*>(wrapper);
		auto it = value->FindMember(member.c_str());
		return it != value->MemberEnd() && it->value.IsObject();
	}

	bool Serializable::IsArray(const void* wrapper)
	{
		return static_cast<const Value*>(wrapper)->IsArray();
	}

	bool Serializable::IsArray(const void* wrapper, const std::string &member)
	{
		auto value = static_cast<const Value*>(wrapper);
		auto it = value->FindMember(member.c_str());
		return it != value->MemberEnd() && it->value.IsArray();
	}
}