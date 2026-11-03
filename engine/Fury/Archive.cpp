#include "Fury/Archive.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "Fury/BoxBounds.h"
#include "Fury/Color.h"
#include "Fury/EntityManager.h"
#include "Fury/Matrix4.h"
#include "Fury/Quaternion.h"
#include "Fury/Scene.h"
#include "Fury/Vector4.h"

namespace fury
{
	Archive::Archive(ArchiveMode mode) : m_Mode(mode)
	{
	}

	void Archive::SetError(const std::string& message)
	{
		SetError(GetCurrentPath(), message);
	}

	void Archive::SetError(const std::string& path, const std::string& message)
	{
		if (!m_Error.empty()) return;
		m_Error = message;
		m_ErrorPath = path;
	}

	void Archive::SetContext(const std::shared_ptr<Scene>& scene,
		const std::shared_ptr<EntityManager>& manager)
	{
		m_Scene = scene;
		m_Manager = manager;
		if (!m_Manager && m_Scene)
			m_Manager = m_Scene->GetEntityManager();
	}

	ArchiveFieldResult Archive::TransferResource(std::type_index type, ResourceIdentity identity,
		const char* name, std::string& value, ArchiveFieldPolicy policy, bool present)
	{
		if (IsCooking())
		{
			if (present && !value.empty()) CollectReference(type, identity, value);
			return ArchiveFieldResult::Ok;
		}
		return Transfer(name, value, policy, present);
	}

	struct JsonReadArchive::Impl
	{
		struct Context
		{
			const rapidjson::Value* Value;
			std::string Path;
			bool Array = false;
			std::size_t Index = 0;
		};

		std::vector<Context> Stack;

		const rapidjson::Value* Current() const
		{
			const Context& context = Stack.back();
			if (!context.Array) return context.Value;
			if (context.Index >= context.Value->Size()) return nullptr;
			return &(*context.Value)[context.Index];
		}

		std::string CurrentPath() const
		{
			const Context& context = Stack.back();
			if (!context.Array) return context.Path;
			return context.Path + "[" + std::to_string(context.Index) + "]";
		}

		const rapidjson::Value* Field(Archive& ar, const char* name,
			ArchiveFieldPolicy policy, bool /*present*/, std::string& path) const
		{
			path = CurrentPath();
			const rapidjson::Value* current = Current();
			if (name == nullptr) return current;
			if (!current || !current->IsObject())
			{
				ar.SetError(path, "expected an object");
				return nullptr;
			}
			path += "." + std::string(name);
			auto it = current->FindMember(name);
			if (it == current->MemberEnd())
			{
				if (policy == ArchiveFieldPolicy::Required)
					ar.SetError(path, "required field is missing");
				return nullptr;
			}
			return &it->value;
		}

		bool FloatArray(Archive& ar, const char* name, ArchiveFieldPolicy policy,
			bool present, std::vector<float>& values, std::string* fieldPath = nullptr) const
		{
			std::string path;
			const rapidjson::Value* value = Field(ar, name, policy, present, path);
			if (fieldPath) *fieldPath = path;
			if (!value) return false;
			if (!value->IsArray())
			{
				ar.SetError(path, "expected an array");
				return false;
			}
			values.clear();
			values.reserve(value->Size());
			for (const auto& item : value->GetArray())
			{
				if (!item.IsNumber())
				{
					ar.SetError(path, "expected an array of numbers");
					return false;
				}
				values.push_back(item.GetFloat());
			}
			return true;
		}
	};

	JsonReadArchive::JsonReadArchive(const rapidjson::Value* root) : Archive(ArchiveMode::Loading),
		m_Impl(std::make_unique<Impl>())
	{
		m_Impl->Stack.push_back({ root, "$", false, 0 });
	}

	JsonReadArchive::~JsonReadArchive() = default;

	const rapidjson::Value* JsonReadArchive::GetCurrentValue() const
	{
		return m_Impl->Current();
	}

	std::string JsonReadArchive::GetCurrentPath() const
	{
		return m_Impl->CurrentPath();
	}

	bool JsonReadArchive::EnterObject(const char* name, ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* value = m_Impl->Field(*this, name, policy, present, path);
		if (!value) return false;
		if (!value->IsObject())
		{
			SetError(path, "expected an object");
			return false;
		}
		m_Impl->Stack.push_back({ value, path, false, 0 });
		return true;
	}

	void JsonReadArchive::LeaveObject()
	{
		if (m_Impl->Stack.size() > 1) m_Impl->Stack.pop_back();
	}

	bool JsonReadArchive::EnterArray(const char* name, ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* value = m_Impl->Field(*this, name, policy, present, path);
		if (!value) return false;
		if (!value->IsArray())
		{
			SetError(path, "expected an array");
			return false;
		}
		m_Impl->Stack.push_back({ value, path, true, 0 });
		return true;
	}

	void JsonReadArchive::LeaveArray()
	{
		if (m_Impl->Stack.size() > 1) m_Impl->Stack.pop_back();
	}

	std::size_t JsonReadArchive::GetArraySize()
	{
		auto& context = m_Impl->Stack.back();
		if (!context.Array || !context.Value->IsArray())
		{
			SetError(context.Path, "expected an array");
			return 0;
		}
		return context.Value->Size();
	}

	void JsonReadArchive::SetArrayIndex(std::size_t index)
	{
		auto& context = m_Impl->Stack.back();
		if (!context.Array || index >= context.Value->Size())
		{
			SetError(context.Path, "array index is out of range");
			return;
		}
		context.Index = index;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, bool& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* raw = m_Impl->Field(*this, name, policy, present, path);
		if (!raw) return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (!raw->IsBool())
		{
			SetError(path, "expected a boolean");
			return ArchiveFieldResult::Ok;
		}
		value = raw->GetBool();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, int& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* raw = m_Impl->Field(*this, name, policy, present, path);
		if (!raw) return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (!raw->IsInt())
		{
			SetError(path, "expected an integer");
			return ArchiveFieldResult::Ok;
		}
		value = raw->GetInt();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, unsigned int& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* raw = m_Impl->Field(*this, name, policy, present, path);
		if (!raw) return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (!raw->IsUint())
		{
			SetError(path, "expected an unsigned integer");
			return ArchiveFieldResult::Ok;
		}
		value = raw->GetUint();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, std::int64_t& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* raw = m_Impl->Field(*this, name, policy, present, path);
		if (!raw) return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (!raw->IsInt64())
		{
			SetError(path, "expected a 64-bit integer");
			return ArchiveFieldResult::Ok;
		}
		value = raw->GetInt64();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, std::uint64_t& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* raw = m_Impl->Field(*this, name, policy, present, path);
		if (!raw) return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (!raw->IsUint64())
		{
			SetError(path, "expected an unsigned 64-bit integer");
			return ArchiveFieldResult::Ok;
		}
		value = raw->GetUint64();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, float& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* raw = m_Impl->Field(*this, name, policy, present, path);
		if (!raw) return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (!raw->IsNumber())
		{
			SetError(path, "expected a number");
			return ArchiveFieldResult::Ok;
		}
		value = raw->GetFloat();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, double& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* raw = m_Impl->Field(*this, name, policy, present, path);
		if (!raw) return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (!raw->IsNumber())
		{
			SetError(path, "expected a number");
			return ArchiveFieldResult::Ok;
		}
		value = raw->GetDouble();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, std::string& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::string path;
		const rapidjson::Value* raw = m_Impl->Field(*this, name, policy, present, path);
		if (!raw) return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (!raw->IsString())
		{
			SetError(path, "expected a string");
			return ArchiveFieldResult::Ok;
		}
		value.assign(raw->GetString(), raw->GetStringLength());
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, Color& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::vector<float> raw;
		std::string path;
		if (!m_Impl->FloatArray(*this, name, policy, present, raw, &path))
			return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (raw.size() != 4)
		{
			SetError(path, "expected four color components");
			return ArchiveFieldResult::Ok;
		}
		value = Color(raw[0], raw[1], raw[2], raw[3]);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, Vector4& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::vector<float> raw;
		std::string path;
		if (!m_Impl->FloatArray(*this, name, policy, present, raw, &path))
			return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (raw.size() != 3 && raw.size() != 4)
		{
			SetError(path, "expected three or four vector components");
			return ArchiveFieldResult::Ok;
		}
		value = Vector4(raw[0], raw[1], raw[2], raw.size() == 3 ? 1.0f : raw[3]);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, Quaternion& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::vector<float> raw;
		std::string path;
		if (!m_Impl->FloatArray(*this, name, policy, present, raw, &path))
			return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (raw.size() != 4)
		{
			SetError(path, "expected four quaternion components");
			return ArchiveFieldResult::Ok;
		}
		value = Quaternion(raw[0], raw[1], raw[2], raw[3]);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, Matrix4& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::vector<float> raw;
		std::string path;
		if (!m_Impl->FloatArray(*this, name, policy, present, raw, &path))
			return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (raw.size() != 16)
		{
			SetError(path, "expected sixteen matrix components");
			return ArchiveFieldResult::Ok;
		}
		value = Matrix4(raw.data());
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonReadArchive::Transfer(const char* name, BoxBounds& value,
		ArchiveFieldPolicy policy, bool present)
	{
		std::vector<float> raw;
		std::string path;
		if (!m_Impl->FloatArray(*this, name, policy, present, raw, &path))
			return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;
		if (raw.size() == 2)
		{
			value.SetInfinite(true);
			return ArchiveFieldResult::Ok;
		}
		if (raw.size() != 6)
		{
			SetError(path, "expected two or six bounds components");
			return ArchiveFieldResult::Ok;
		}
		value.SetInfinite(false);
		value.SetMinMax(Vector4(raw[0], raw[1], raw[2]), Vector4(raw[3], raw[4], raw[5]));
		return ArchiveFieldResult::Ok;
	}

	struct JsonWriteArchive::Impl
	{
		using Writer = JsonWriter;
		struct Context
		{
			std::string Path;
		};

		Writer* WriterPtr;
		std::vector<Context> Stack;

		std::string CurrentPath() const
		{
			return Stack.back().Path;
		}

		void Key(const char* name)
		{
			if (name) WriterPtr->Key(name);
		}
	};

	JsonWriteArchive::JsonWriteArchive(JsonWriter* writer) : Archive(ArchiveMode::Saving),
		m_Impl(std::make_unique<Impl>())
	{
		m_Impl->WriterPtr = writer;
		m_Impl->Stack.push_back({ "$" });
	}

	JsonWriteArchive::~JsonWriteArchive() = default;

	std::string JsonWriteArchive::GetCurrentPath() const
	{
		return m_Impl->CurrentPath();
	}

	bool JsonWriteArchive::EnterObject(const char* name, ArchiveFieldPolicy, bool present)
	{
		if (!present) return false;
		m_Impl->Key(name);
		m_Impl->WriterPtr->StartObject();
		m_Impl->Stack.push_back({ m_Impl->CurrentPath() + (name ? "." + std::string(name) : "") });
		return true;
	}

	void JsonWriteArchive::LeaveObject()
	{
		m_Impl->WriterPtr->EndObject();
		if (m_Impl->Stack.size() > 1) m_Impl->Stack.pop_back();
	}

	bool JsonWriteArchive::EnterArray(const char* name, ArchiveFieldPolicy, bool present)
	{
		if (!present) return false;
		m_Impl->Key(name);
		m_Impl->WriterPtr->StartArray();
		m_Impl->Stack.push_back({ m_Impl->CurrentPath() + (name ? "." + std::string(name) : "") });
		return true;
	}

	void JsonWriteArchive::LeaveArray()
	{
		m_Impl->WriterPtr->EndArray();
		if (m_Impl->Stack.size() > 1) m_Impl->Stack.pop_back();
	}

	std::size_t JsonWriteArchive::GetArraySize()
	{
		return 0;
	}

	void JsonWriteArchive::SetArrayIndex(std::size_t)
	{
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, bool& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->Bool(value);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, int& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->Int(value);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, unsigned int& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->Uint(value);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, std::int64_t& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->Int64(value);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, std::uint64_t& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->Uint64(value);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, float& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->Double(value);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, double& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->Double(value);
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, std::string& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->String(value.data(), static_cast<rapidjson::SizeType>(value.size()));
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, Color& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->StartArray();
		m_Impl->WriterPtr->Double(value.r);
		m_Impl->WriterPtr->Double(value.g);
		m_Impl->WriterPtr->Double(value.b);
		m_Impl->WriterPtr->Double(value.a);
		m_Impl->WriterPtr->EndArray();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, Vector4& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->StartArray();
		m_Impl->WriterPtr->Double(value.x);
		m_Impl->WriterPtr->Double(value.y);
		m_Impl->WriterPtr->Double(value.z);
		m_Impl->WriterPtr->Double(value.w);
		m_Impl->WriterPtr->EndArray();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, Quaternion& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->StartArray();
		m_Impl->WriterPtr->Double(value.x);
		m_Impl->WriterPtr->Double(value.y);
		m_Impl->WriterPtr->Double(value.z);
		m_Impl->WriterPtr->Double(value.w);
		m_Impl->WriterPtr->EndArray();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, Matrix4& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		m_Impl->WriterPtr->StartArray();
		for (float component : value.Raw) m_Impl->WriterPtr->Double(component);
		m_Impl->WriterPtr->EndArray();
		return ArchiveFieldResult::Ok;
	}

	ArchiveFieldResult JsonWriteArchive::Transfer(const char* name, BoxBounds& value,
		ArchiveFieldPolicy, bool present)
	{
		if (!present) return ArchiveFieldResult::Missing;
		m_Impl->Key(name);
		auto min = value.GetMin();
		auto max = value.GetMax();
		m_Impl->WriterPtr->StartArray();
		m_Impl->WriterPtr->Double(min.x);
		m_Impl->WriterPtr->Double(min.y);
		if (!value.GetInfinite())
		{
			m_Impl->WriterPtr->Double(min.z);
			m_Impl->WriterPtr->Double(max.x);
			m_Impl->WriterPtr->Double(max.y);
			m_Impl->WriterPtr->Double(max.z);
		}
		m_Impl->WriterPtr->EndArray();
		return ArchiveFieldResult::Ok;
	}

	CookArchive::CookArchive() : Archive(ArchiveMode::Cooking)
	{
	}

	bool CookArchive::EnterObject(const char*, ArchiveFieldPolicy, bool present)
	{
		return present;
	}

	void CookArchive::LeaveObject()
	{
	}

	bool CookArchive::EnterArray(const char*, ArchiveFieldPolicy, bool present)
	{
		return present;
	}

	void CookArchive::LeaveArray()
	{
	}

	std::size_t CookArchive::GetArraySize()
	{
		return 0;
	}

	void CookArchive::SetArrayIndex(std::size_t)
	{
	}

#define FURY_COOK_TRANSFER(Type) \
	ArchiveFieldResult CookArchive::Transfer(const char*, Type&, ArchiveFieldPolicy, bool) \
	{ \
		return ArchiveFieldResult::Ok; \
	}

	FURY_COOK_TRANSFER(bool)
	FURY_COOK_TRANSFER(int)
	FURY_COOK_TRANSFER(unsigned int)
	FURY_COOK_TRANSFER(std::int64_t)
	FURY_COOK_TRANSFER(std::uint64_t)
	FURY_COOK_TRANSFER(float)
	FURY_COOK_TRANSFER(double)
	FURY_COOK_TRANSFER(std::string)
	FURY_COOK_TRANSFER(Color)
	FURY_COOK_TRANSFER(Vector4)
	FURY_COOK_TRANSFER(Quaternion)
	FURY_COOK_TRANSFER(Matrix4)
	FURY_COOK_TRANSFER(BoxBounds)

#undef FURY_COOK_TRANSFER

	void CookArchive::CollectReference(std::type_index type, ResourceIdentity identity,
		const std::string& value)
	{
		m_References.push_back({ type, identity, value });
	}

	bool CookArchive::GetTextureSRGB(const std::string& path, bool fallback) const
	{
		auto it = m_SrgbHints.find(path);
		return it == m_SrgbHints.end() ? fallback : it->second;
	}
}
