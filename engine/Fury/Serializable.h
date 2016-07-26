#ifndef _FURY_SERIALIZEABLE_H_
#define _FURY_SERIALIZEABLE_H_

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include "Fury/Macros.h"

namespace fury
{
	class BoxBounds;

	class Color;

	class Vector4;

	class Quaternion;

	class Matrix4;

	class FURY_API Serializable
	{
	public:

		typedef std::shared_ptr<Serializable> Ptr;

		// wrapper's type should be rapidjson::Value
		virtual bool Load(const void* wrapper, bool object = true) = 0;

		// wrapper's type should be rapidjson::Writer<rapidjson::StringBuffer>
		virtual void Save(void* wrapper, bool object = true) = 0;

	protected:

		static std::string GetValueType(const void* wrapper);

		static const void* FindMember(const void* wrapper, const std::string &name);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, bool &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, int &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, unsigned int &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, float &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, std::string &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, Color &color);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, Vector4 &vector);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, Quaternion &quaternion);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, Matrix4 &matrix);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, BoxBounds &aabb);

		static bool LoadValue(const void* wrapper, bool &value);

		static bool LoadValue(const void* wrapper, int &value);

		static bool LoadValue(const void* wrapper, unsigned int &value);

		static bool LoadValue(const void* wrapper, float &value);

		static bool LoadValue(const void* wrapper, std::string &value);

		static bool LoadArray(const void* wrapper, std::function<bool(const void*)> walker);

		static bool LoadArray(const void* wrapper, const std::string &name, std::function<bool(const void*)> walker);

		// uint, int, float, bool, string
		template<typename T>
		static bool LoadArray(const void* wrapper, const std::string &name, std::vector<T> &raw);

		// uint, int, float, bool, string
		template<typename T>
		static bool LoadArray(const void* wrapper, std::vector<T> &raw);

		static void SaveKey(void* wrapper, const std::string &key);

		static void SaveValue(void* wrapper, bool value);

		static void SaveValue(void* wrapper, int value);

		static void SaveValue(void* wrapper, unsigned int value);

		static void SaveValue(void* wrapper, float value);

		static void SaveValue(void* wrapper, const std::string &value);

		static void SaveValue(void* wrapper, const char *value);

		static void SaveValue(void* wrapper, const Color &color);

		static void SaveValue(void* wrapper, const Vector4 &vector);

		static void SaveValue(void* wrapper, const Quaternion &quaternion);

		static void SaveValue(void* wrapper, const Matrix4 &matrix);

		static void SaveValue(void* wrapper, const BoxBounds &aabb);

		static void SaveArray(void *wrapper, unsigned int count, std::function<void(unsigned int)> walker);

		template<class Container>
		static void SaveArray(void *wrapper, Container &raw, std::function<void(decltype(raw.begin()))> walker)
		{
			StartArray(wrapper);

			for (auto it = raw.begin(); it != raw.end(); ++it)
				walker(it);

			EndArray(wrapper);
		}

		// uint, int, float, bool, string
		template<typename T>
		static void SaveArray(void *wrapper, std::vector<T> &raw);

		static void StartObject(void* wrapper);

		static void EndObject(void* wrapper);

		static void StartArray(void* wrapper);

		static void EndArray(void* wrapper);

		static bool IsObject(const void* wrapper);

		static bool IsObject(const void* wrapper, const std::string &member);

		static bool IsArray(const void* wrapper);

		static bool IsArray(const void* wrapper, const std::string &member);
	};
}

#endif // _FURY_SERIALIZEABLE_H_