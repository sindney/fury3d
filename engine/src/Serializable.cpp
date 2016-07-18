#include <fstream>

#include <rapidjson/document.h>
#include <rapidjson/PrettyWriter.h>
#include <rapidjson/stringbuffer.h>

#include "BoxBounds.h"
#include "Log.h"
#include "Serializable.h"
#include "Color.h"
#include "Quaternion.h"
#include "Vector4.h"
#include "Matrix4.h"

namespace fury
{
	using namespace rapidjson;

	const void* Serializable::FindMember(const void* wrapper, const std::string &name)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);

		Value::ConstMemberIterator it = dom.FindMember(name.c_str());
		if (it == dom.MemberEnd())
			return nullptr;

		return static_cast<const void*>(&(it->value));
	}

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

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, unsigned int &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);

		Value::ConstMemberIterator it = dom.FindMember(name.c_str());
		if (it == dom.MemberEnd() || !it->value.IsUint())
			return false;

		value = it->value.GetUint();
		return true;
	}

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, float &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);

		Value::ConstMemberIterator it = dom.FindMember(name.c_str());
		if (it == dom.MemberEnd() || !it->value.IsFloat())
			return false;

		value = it->value.GetFloat();
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

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, Color &color)
	{
		std::vector<float> raw;
		if (!LoadArray(wrapper, name, [&](const void* node) -> bool
		{
			float value;
			if (!LoadValue(node, value))
			{
				FURYW << "Color is a 4 float array!";
				return false;
			}
			raw.push_back(value);
			return true;
		}))
		{
			return false;
		}

		while (raw.size() < 4)
			raw.push_back(0);

		color.r = raw[0];
		color.g = raw[1];
		color.b = raw[2];
		color.a = raw[3];

		return true;
	}

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, Vector4 &vector)
	{
		std::vector<float> raw;
		if (!LoadArray(wrapper, name, raw))
			return false;

		if (raw.size() < 4)
		{
			vector.Zero();
		}
		else
		{
			vector.x = raw[0];
			vector.y = raw[1];
			vector.z = raw[2];
			vector.w = raw[3];
		}
		return true;
	}

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, Quaternion &quaternion)
	{
		std::vector<float> raw;
		if (!LoadArray(wrapper, name, raw))
			return false;

		if (raw.size() < 4)
		{
			quaternion.Identity();
		}
		else
		{
			quaternion.x = raw[0];
			quaternion.y = raw[1];
			quaternion.z = raw[2];
			quaternion.w = raw[3];
		}
		return true;
	}

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, Matrix4 &matrix)
	{
		std::vector<float> raw;
		if (!LoadArray(wrapper, name, raw))
			return false;

		if (raw.size() < 16)
			matrix.Identity();
		else
			matrix = Matrix4(&raw[0]);

		return true;
	}

	bool Serializable::LoadMemberValue(const void* wrapper, const std::string &name, BoxBounds &aabb)
	{
		std::vector<float> raw;
		if (!LoadArray(wrapper, name, raw))
			return false;

		aabb.SetInfinite(false);
		if (raw.size() < 6)
			aabb.SetInfinite(true);
		else
			aabb.SetMinMax(Vector4(raw[0], raw[1], raw[2]), Vector4(raw[3], raw[4], raw[5]));

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

	bool Serializable::LoadValue(const void* wrapper, unsigned int &value)
	{
		const Value &dom = *static_cast<const Value*>(wrapper);
		if (dom.IsUint())
		{
			value = dom.GetUint();
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
		if (dom.IsFloat())
		{
			value = dom.GetFloat();
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

	bool Serializable::LoadArray(const void* wrapper, std::function<bool(const void*)> walker)
	{
		const Value &array = *static_cast<const Value*>(wrapper);

		if (!array.IsArray())
			return false;

		for (Value::ConstValueIterator it = array.Begin(); it != array.End(); ++it)
		{
			if (!walker(&(*it))) return false;
		}

		return true;
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

	template<typename T>
	bool Serializable::LoadArray(const void* wrapper, const std::string &name, std::vector<T> &raw)
	{
		return LoadArray(wrapper, name, [&](const void* node) -> bool
		{
			T value;
			if (!LoadValue(node, value))
			{
				FURYE << "Target is a " << typeid(T).name() << " array!";
				return false;
			}
			raw.push_back(value);
			return true;
		});
	}

	template bool Serializable::LoadArray<float>(const void* wrapper, const std::string &name, std::vector<float> &raw);

	template bool Serializable::LoadArray<int>(const void* wrapper, const std::string &name, std::vector<int> &raw);

	template bool Serializable::LoadArray<unsigned int>(const void* wrapper, const std::string &name, std::vector<unsigned int> &raw);

	template bool Serializable::LoadArray<bool>(const void* wrapper, const std::string &name, std::vector<bool> &raw);

	template bool Serializable::LoadArray<std::string>(const void* wrapper, const std::string &name, std::vector<std::string> &raw);

	template<typename T>
	bool Serializable::LoadArray(const void* wrapper, std::vector<T> &raw)
	{
		return LoadArray(wrapper, [&](const void* node) -> bool
		{
			T value;
			if (!LoadValue(node, value))
			{
				FURYE << "Target is a " << typeid(T).name() << " array!";
				return false;
			}
			raw.push_back(value);
			return true;
		});
	}

	template bool Serializable::LoadArray<float>(const void* wrapper, std::vector<float> &raw);

	template bool Serializable::LoadArray<int>(const void* wrapper, std::vector<int> &raw);

	template bool Serializable::LoadArray<unsigned int>(const void* wrapper, std::vector<unsigned int> &raw);

	template bool Serializable::LoadArray<bool>(const void* wrapper, std::vector<bool> &raw);

	template bool Serializable::LoadArray<std::string>(const void* wrapper, std::vector<std::string> &raw);

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

	void Serializable::SaveValue(void* wrapper, unsigned int value)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->Uint(value);
	}

	void Serializable::SaveValue(void* wrapper, float value)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->Double(value);
	}

	void Serializable::SaveValue(void* wrapper, const std::string &value)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->String(value.c_str(), value.size());
	}

	void Serializable::SaveValue(void* wrapper, const char *value)
	{
		static_cast<PrettyWriter<StringBuffer>*>(wrapper)->String(value);
	}

	void Serializable::SaveValue(void* wrapper, const Color &color)
	{
		float channels[4] = { color.r, color.g, color.b, color.a };
		SaveArray(wrapper, 4, [&](unsigned int index)
		{
			SaveValue(wrapper, channels[index]);
		});
	}

	void Serializable::SaveValue(void* wrapper, const Vector4 &vector)
	{
		float channels[4] = { vector.x, vector.y, vector.z, vector.w };
		SaveArray(wrapper, 4, [&](unsigned int index)
		{
			SaveValue(wrapper, channels[index]);
		});
	}

	void Serializable::SaveValue(void* wrapper, const Quaternion &quaternion)
	{
		float channels[4] = { quaternion.x, quaternion.y, quaternion.z, quaternion.w };
		SaveArray(wrapper, 4, [&](unsigned int index)
		{
			SaveValue(wrapper, channels[index]);
		});
	}

	void Serializable::SaveValue(void* wrapper, const Matrix4 &matrix)
	{
		SaveArray(wrapper, 16, [&](unsigned int index)
		{
			SaveValue(wrapper, matrix.Raw[index]);
		});
	}

	void Serializable::SaveValue(void* wrapper, const BoxBounds &aabb)
	{
		auto max = aabb.GetMax(), min = aabb.GetMin();
		float raw[6] = { min.x, min.y, min.z, max.x, max.y, max.z };
		SaveArray(wrapper, aabb.GetInfinite() ? 1 : 6, [&](unsigned int index)
		{
			SaveValue(wrapper, raw[index]);
		});
	}

	void Serializable::SaveArray(void *wrapper, unsigned int count, std::function<void(unsigned int)> walker)
	{
		auto writter = static_cast<PrettyWriter<StringBuffer>*>(wrapper);
		writter->StartArray();

		for (unsigned int i = 0; i < count; i++)
			walker(i);

		writter->EndArray();
	}

	template<typename T>
	void Serializable::SaveArray(void *wrapper, std::vector<T> &raw)
	{
		auto writter = static_cast<PrettyWriter<StringBuffer>*>(wrapper);
		writter->StartArray();

		auto count = raw.size();
		for (unsigned int i = 0; i < count; i++)
			SaveValue(wrapper, raw[i]);

		writter->EndArray();
	}

	template void Serializable::SaveArray<float>(void* wrapper, std::vector<float> &raw);

	template void Serializable::SaveArray<int>(void* wrapper, std::vector<int> &raw);

	template void Serializable::SaveArray<unsigned int>(void* wrapper, std::vector<unsigned int> &raw);

	template void Serializable::SaveArray<bool>(void* wrapper, std::vector<bool> &raw);

	template void Serializable::SaveArray<std::string>(void* wrapper, std::vector<std::string> &raw);

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