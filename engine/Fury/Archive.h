#ifndef _FURY_ARCHIVE_H_
#define _FURY_ARCHIVE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

#include <rapidjson/fwd.h>

#include "Fury/Macros.h"

namespace fury
{
	class Archive;
	class Scene;
	class Pipeline;
	class EntityManager;
	class Color;
	class Vector4;
	class Quaternion;
	class Matrix4;
	class BoxBounds;

	enum class ArchiveMode
	{
		Loading,
		Saving,
		Cooking
	};

	enum class ArchiveFieldPolicy
	{
		Required,
		Optional,
		Conditional
	};

	enum class ArchiveFieldResult
	{
		Ok,
		Missing
	};

	enum class ResourceIdentity
	{
		Name,
		Path
	};

	using JsonWriter = rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF8<char>,
		rapidjson::UTF8<char>, rapidjson::CrtAllocator, 0>;

	struct CookReference
	{
		std::type_index Type;
		ResourceIdentity Identity;
		std::string Value;
	};

	template<typename T>
	struct RequiredField
	{
		const char* Name;
		T& Value;
	};

	template<typename T>
	struct OptionalField
	{
		const char* Name;
		T& Value;
	};

	template<typename T>
	struct ConditionalField
	{
		const char* Name;
		T& Value;
		bool Present;
	};

	template<typename T>
	struct ResourceReferenceField
	{
		const char* Name;
		std::string& Value;
		ResourceIdentity Identity;
		ArchiveFieldPolicy Policy;
		bool Present;
	};

	template<typename T>
	RequiredField<T> Required(const char* name, T& value)
	{
		return { name, value };
	}

	template<typename T>
	OptionalField<T> Optional(const char* name, T& value)
	{
		return { name, value };
	}

	template<typename T>
	ConditionalField<T> Conditional(const char* name, T& value, bool present)
	{
		return { name, value, present };
	}

	template<typename T>
	ResourceReferenceField<T> NameReference(const char* name, std::string& value)
	{
		return { name, value, ResourceIdentity::Name, ArchiveFieldPolicy::Required, true };
	}

	template<typename T>
	ResourceReferenceField<T> PathReference(const char* name, std::string& value)
	{
		return { name, value, ResourceIdentity::Path, ArchiveFieldPolicy::Required, true };
	}

	template<typename T>
	ResourceReferenceField<T> OptionalNameReference(const char* name, std::string& value)
	{
		return { name, value, ResourceIdentity::Name, ArchiveFieldPolicy::Optional, true };
	}

	template<typename T>
	ResourceReferenceField<T> OptionalPathReference(const char* name, std::string& value)
	{
		return { name, value, ResourceIdentity::Path, ArchiveFieldPolicy::Optional, true };
	}

	namespace detail
	{
		template<typename T, typename = void>
		struct HasSerialize : std::false_type {};

		template<typename T>
		struct HasSerialize<T, std::void_t<decltype(std::declval<T&>().Serialize(std::declval<Archive&>()))>>
			: std::true_type {};
	}

	class FURY_API Archive
	{
	public:
		explicit Archive(ArchiveMode mode);
		virtual ~Archive() = default;

		bool IsLoading() const { return m_Mode == ArchiveMode::Loading; }
		bool IsSaving() const { return m_Mode == ArchiveMode::Saving; }
		bool IsCooking() const { return m_Mode == ArchiveMode::Cooking; }
		ArchiveMode GetMode() const { return m_Mode; }

		bool IsValid() const { return m_Error.empty(); }
		const std::string& GetError() const { return m_Error; }
		const std::string& GetErrorPath() const { return m_ErrorPath; }
		void SetError(const std::string& message);
		void SetError(const std::string& path, const std::string& message);

		void SetVersion(int version) { m_Version = version; }
		int GetVersion() const { return m_Version; }

		void SetContext(const std::shared_ptr<Scene>& scene,
			const std::shared_ptr<EntityManager>& manager = nullptr);
		std::shared_ptr<Scene> GetScene() const { return m_Scene; }
		std::shared_ptr<EntityManager> GetEntityManager() const { return m_Manager; }
		void SetPipelineContext(const std::shared_ptr<Pipeline>& pipeline) { m_Pipeline = pipeline; }
		std::shared_ptr<Pipeline> GetPipeline() const { return m_Pipeline; }

		virtual std::string GetCurrentPath() const { return "$"; }

		virtual bool EnterObject(const char* name = nullptr,
			ArchiveFieldPolicy policy = ArchiveFieldPolicy::Required, bool present = true) = 0;
		virtual void LeaveObject() = 0;
		virtual bool EnterArray(const char* name = nullptr,
			ArchiveFieldPolicy policy = ArchiveFieldPolicy::Required, bool present = true) = 0;
		virtual void LeaveArray() = 0;
		virtual std::size_t GetArraySize() = 0;
		virtual void SetArrayIndex(std::size_t index) = 0;

		virtual ArchiveFieldResult Transfer(const char* name, bool& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, int& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, unsigned int& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, std::int64_t& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, std::uint64_t& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, float& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, double& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, std::string& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, Color& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, Vector4& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, Quaternion& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, Matrix4& value,
			ArchiveFieldPolicy policy, bool present) = 0;
		virtual ArchiveFieldResult Transfer(const char* name, BoxBounds& value,
			ArchiveFieldPolicy policy, bool present) = 0;

		// One array element: types carrying their own Serialize(Archive&)
		// write an object, everything else transfers as a plain value.
		template<typename T>
		void SerializeElement(T& value)
		{
			if constexpr (detail::HasSerialize<T>::value)
			{
				if (EnterObject())
				{
					value.Serialize(*this);
					LeaveObject();
				}
			}
			else
			{
				Transfer(nullptr, value, ArchiveFieldPolicy::Required, true);
			}
		}

		template<typename T>
		ArchiveFieldResult Transfer(const char* name, std::vector<T>& values,
			ArchiveFieldPolicy policy, bool present)
		{
			if (IsLoading())
				values.clear();
			if (!EnterArray(name, policy, present))
				return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;

			if (IsLoading())
				values.resize(GetArraySize());
			for (std::size_t i = 0; i < values.size(); ++i)
			{
				if (IsLoading())
					SetArrayIndex(i);
				SerializeElement(values[i]);
			}
			LeaveArray();
			return ArchiveFieldResult::Ok;
		}

		// std::vector<bool> hands out proxies, which cannot bind to bool&.
		ArchiveFieldResult Transfer(const char* name, std::vector<bool>& values,
			ArchiveFieldPolicy policy, bool present)
		{
			if (IsLoading())
				values.clear();
			if (!EnterArray(name, policy, present))
				return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;

			if (IsLoading())
				values.resize(GetArraySize());
			for (std::size_t i = 0; i < values.size(); ++i)
			{
				if (IsLoading())
					SetArrayIndex(i);
				bool value = values[i];
				Transfer(nullptr, value, ArchiveFieldPolicy::Required, true);
				if (IsLoading())
					values[i] = value;
			}
			LeaveArray();
			return ArchiveFieldResult::Ok;
		}

		// Elements behind a shared_ptr are created on load, so the pointee
		// has to be default constructible.
		template<typename T>
		ArchiveFieldResult Transfer(const char* name, std::vector<std::shared_ptr<T>>& values,
			ArchiveFieldPolicy policy, bool present)
		{
			if (IsLoading())
				values.clear();
			if (!EnterArray(name, policy, present))
				return IsValid() ? ArchiveFieldResult::Missing : ArchiveFieldResult::Ok;

			if (IsLoading())
				values.resize(GetArraySize());
			for (std::size_t i = 0; i < values.size(); ++i)
			{
				if (IsLoading())
					SetArrayIndex(i);
				if (!values[i])
					values[i] = std::make_shared<T>();
				if (values[i] && EnterObject())
				{
					values[i]->Serialize(*this);
					LeaveObject();
				}
			}
			LeaveArray();
			return ArchiveFieldResult::Ok;
		}

		template<typename T>
		ArchiveFieldResult TransferNamed(const char* name, T& value,
			ArchiveFieldPolicy policy, bool present)
		{
			if (!IsValid())
				return ArchiveFieldResult::Ok;
			return Transfer(name, value, policy, present);
		}

		ArchiveFieldResult TransferResource(std::type_index type, ResourceIdentity identity,
			const char* name, std::string& value, ArchiveFieldPolicy policy, bool present);
		virtual void SetTextureSRGB(const std::string&, bool) {}

	protected:
		virtual void CollectReference(std::type_index, ResourceIdentity, const std::string&) {}

	private:
		ArchiveMode m_Mode;
		int m_Version = 1;
		std::string m_Error;
		std::string m_ErrorPath;
		std::shared_ptr<Scene> m_Scene;
		std::shared_ptr<EntityManager> m_Manager;
		std::shared_ptr<Pipeline> m_Pipeline;
	};

	template<typename T>
	Archive& operator<<(Archive& ar, const RequiredField<T>& field)
	{
		ar.TransferNamed(field.Name, field.Value, ArchiveFieldPolicy::Required, true);
		return ar;
	}

	template<typename T>
	Archive& operator<<(Archive& ar, const OptionalField<T>& field)
	{
		ar.TransferNamed(field.Name, field.Value, ArchiveFieldPolicy::Optional, true);
		return ar;
	}

	template<typename T>
	Archive& operator<<(Archive& ar, const ConditionalField<T>& field)
	{
		ar.TransferNamed(field.Name, field.Value, ArchiveFieldPolicy::Conditional, field.Present);
		return ar;
	}

	template<typename T>
	Archive& operator<<(Archive& ar, const ResourceReferenceField<T>& field)
	{
		ar.TransferResource(typeid(T), field.Identity, field.Name, field.Value,
			field.Policy, field.Present);
		return ar;
	}

	class FURY_API JsonReadArchive final : public Archive
	{
	public:
		explicit JsonReadArchive(const rapidjson::Value* root);
		~JsonReadArchive() override;

		const rapidjson::Value* GetCurrentValue() const;
		std::string GetCurrentPath() const override;

		bool EnterObject(const char* name = nullptr,
			ArchiveFieldPolicy policy = ArchiveFieldPolicy::Required, bool present = true) override;
		void LeaveObject() override;
		bool EnterArray(const char* name = nullptr,
			ArchiveFieldPolicy policy = ArchiveFieldPolicy::Required, bool present = true) override;
		void LeaveArray() override;
		std::size_t GetArraySize() override;
		void SetArrayIndex(std::size_t index) override;

		ArchiveFieldResult Transfer(const char* name, bool& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, int& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, unsigned int& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::int64_t& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::uint64_t& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, float& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, double& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::string& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Color& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Vector4& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Quaternion& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Matrix4& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, BoxBounds& value,
			ArchiveFieldPolicy policy, bool present) override;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

	class FURY_API JsonWriteArchive final : public Archive
	{
	public:
		explicit JsonWriteArchive(JsonWriter* writer);
		~JsonWriteArchive() override;

		std::string GetCurrentPath() const override;

		bool EnterObject(const char* name = nullptr,
			ArchiveFieldPolicy policy = ArchiveFieldPolicy::Required, bool present = true) override;
		void LeaveObject() override;
		bool EnterArray(const char* name = nullptr,
			ArchiveFieldPolicy policy = ArchiveFieldPolicy::Required, bool present = true) override;
		void LeaveArray() override;
		std::size_t GetArraySize() override;
		void SetArrayIndex(std::size_t index) override;

		ArchiveFieldResult Transfer(const char* name, bool& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, int& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, unsigned int& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::int64_t& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::uint64_t& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, float& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, double& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::string& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Color& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Vector4& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Quaternion& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Matrix4& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, BoxBounds& value,
			ArchiveFieldPolicy policy, bool present) override;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

	class FURY_API CookArchive final : public Archive
	{
	public:
		CookArchive();

		bool EnterObject(const char* name = nullptr,
			ArchiveFieldPolicy policy = ArchiveFieldPolicy::Required, bool present = true) override;
		void LeaveObject() override;
		bool EnterArray(const char* name = nullptr,
			ArchiveFieldPolicy policy = ArchiveFieldPolicy::Required, bool present = true) override;
		void LeaveArray() override;
		std::size_t GetArraySize() override;
		void SetArrayIndex(std::size_t index) override;

		ArchiveFieldResult Transfer(const char* name, bool& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, int& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, unsigned int& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::int64_t& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::uint64_t& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, float& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, double& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, std::string& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Color& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Vector4& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Quaternion& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, Matrix4& value,
			ArchiveFieldPolicy policy, bool present) override;
		ArchiveFieldResult Transfer(const char* name, BoxBounds& value,
			ArchiveFieldPolicy policy, bool present) override;

		const std::vector<CookReference>& GetReferences() const { return m_References; }
		void SetTextureSRGB(const std::string& path, bool srgb) override { m_SrgbHints[path] = srgb; }
		bool GetTextureSRGB(const std::string& path, bool fallback) const;

	protected:
		void CollectReference(std::type_index type, ResourceIdentity identity,
			const std::string& value) override;

	private:
		std::vector<CookReference> m_References;
		std::map<std::string, bool> m_SrgbHints;
	};
}

#endif // _FURY_ARCHIVE_H_
