#ifndef _FURY_SERIALIZEABLE_H_
#define _FURY_SERIALIZEABLE_H_

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include "Macros.h"

namespace fury
{
	class Color;

	class FURY_API Serializable
	{
	public:

		typedef std::shared_ptr<Serializable> Ptr;

		// wrapper's type should be rapidjson::Value
		virtual bool Load(const void* wrapper, bool object = true) = 0;

		// wrapper's type should be rapidjson::Writer<rapidjson::StringBuffer>
		virtual bool Save(void* wrapper, bool object = true) = 0;

	protected:

		static bool LoadMemberValue(const void* wrapper, const std::string &name, bool &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, int &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, float &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, std::string &value);

		static bool LoadMemberValue(const void* wrapper, const std::string &name, Color &color);

		static bool LoadValue(const void* wrapper, bool &value);

		static bool LoadValue(const void* wrapper, int &value);

		static bool LoadValue(const void* wrapper, float &value);

		static bool LoadValue(const void* wrapper, std::string &value);

		static bool LoadArray(const void* wrapper, const std::string &name, std::function<bool(const void*)> walker);

		static void SaveKey(void* wrapper, const std::string &key);

		static void SaveValue(void* wrapper, bool value);

		static void SaveValue(void* wrapper, int value);

		static void SaveValue(void* wrapper, float value);

		static void SaveValue(void* wrapper, const std::string &value);

		static void SaveValue(void* wrapper, const Color &color);

		static void SaveArray(void *wrapper, unsigned int count, std::function<void(unsigned int)> walker);

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