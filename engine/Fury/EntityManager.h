#ifndef _FURY_ENTITY_MANAGER_H_
#define _FURY_ENTITY_MANAGER_H_

#include "Macros.h"

#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>

namespace fury {
class FURY_API EntityManager final {
public:
	typedef std::unordered_map<std::type_index, std::unordered_map<size_t, std::shared_ptr<void>>> TypeMap;

	typedef std::shared_ptr<EntityManager> Ptr;

	static Ptr Create() {
		return std::make_shared<EntityManager>();
	}

	// class ObjectType must have function 'size_t GetHashCode()'.
	template<class ObjectType>
	bool Add(std::shared_ptr<ObjectType> pointer) {
		std::type_index key0 = typeid(ObjectType);
		size_t key1 = pointer->GetHashCode();

		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end()) {
			m_EntityMap[key0].emplace(key1, std::static_pointer_cast<void>(pointer));
			return true;
		}

		auto it1 = it0->second.find(key1);
		if (it1 == it0->second.end()) {
			m_EntityMap[key0].emplace(key1, std::static_pointer_cast<void>(pointer));
			return true;
		}

		return false;
	}

	template<class ObjectType>
	std::shared_ptr<ObjectType> Remove(size_t hashcode) {
		std::type_index key0 = typeid(ObjectType);
		size_t key1 = hashcode;

		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end())
			return nullptr;

		auto it1 = it0->second.find(key1);
		if (it1 == it0->second.end())
			return nullptr;

		auto ptr = it1->second;
		it0->second.erase(it1);

		return std::static_pointer_cast<ObjectType>(ptr);
	}

	// Name-based remove — iterates the map (like Get<T>(name)).
	template<class ObjectType>
	std::shared_ptr<ObjectType> Remove(const std::string& name) {
		std::type_index key0 = typeid(ObjectType);
		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end())
			return nullptr;
		for (auto it1 = it0->second.begin(); it1 != it0->second.end(); ++it1) {
			auto ptr = std::static_pointer_cast<ObjectType>(it1->second);
			if (ptr->GetName() == name) {
				auto out = ptr;
				it0->second.erase(it1);
				return out;
			}
		}
		return nullptr;
	}

	template<class ObjectType>
	void RemoveAll() {
		auto it0 = m_EntityMap.find(typeid(ObjectType));
		if (it0 != m_EntityMap.end())
			it0->second.clear();
	}

	void RemoveAll() {
		m_EntityMap.clear();
	}

	template<class ObjectType>
	size_t Count() {
		auto it0 = m_EntityMap.find(typeid(ObjectType));
		if (it0 != m_EntityMap.end())
			return it0->second.size();
		else
			return 0;
	}

	template<class ObjectType>
	std::shared_ptr<ObjectType> Get(size_t hashcode) {
		std::type_index key0 = typeid(ObjectType);
		size_t key1 = hashcode;

		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end())
			return nullptr;

		auto it1 = it0->second.find(key1);
		if (it1 == it0->second.end())
			return nullptr;

		return std::static_pointer_cast<ObjectType>(it1->second);
	}

	// Fast O(1) lookup by UUID (hashes the UUID and looks up in
	// the map). Use this for EntityManager-registered assets.
	template<class ObjectType>
	std::shared_ptr<ObjectType> GetByUUID(const std::string& uuid) {
		return Get<ObjectType>(std::hash<std::string>()(uuid));
	}

	// Name-based lookup — iterates the map because entities are
	// now keyed by UUID hash, not name hash. O(n) but n is small
	// (typically dozens of assets per type). Returns the first
	// match; if multiple entities share a name, the first one
	// encountered is returned.
	template<class ObjectType>
	std::shared_ptr<ObjectType> Get(const std::string& name) {
		std::type_index key0 = typeid(ObjectType);
		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end())
			return nullptr;
		for (const auto& kv : it0->second) {
			auto ptr = std::static_pointer_cast<ObjectType>(kv.second);
			if (ptr->GetName() == name)
				return ptr;
		}
		return nullptr;
	}

	template<class ObjectType>
	std::unordered_map<size_t, std::shared_ptr<void>>::iterator Begin() {
		std::type_index key0 = typeid(ObjectType);

		auto it0 = m_EntityMap.find(key0);
		if (it0 != m_EntityMap.end())
			return it0->second.begin();

		return std::unordered_map<size_t, std::shared_ptr<void>>::iterator();
	}

	template<class ObjectType>
	std::unordered_map<size_t, std::shared_ptr<void>>::iterator End() {
		std::type_index key0 = typeid(ObjectType);

		auto it0 = m_EntityMap.find(key0);
		if (it0 != m_EntityMap.end())
			return it0->second.end();

		return std::unordered_map<size_t, std::shared_ptr<void>>::iterator();
	}

	// return false in your functor to break the loop.
	template<class ObjectType>
	void ForEach(const std::function<bool(const std::shared_ptr<ObjectType>&)>& func) {
		std::type_index key0 = typeid(ObjectType);

		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end())
			return;

		for (auto it1 = it0->second.begin(); it1 != it0->second.end(); ++it1) {
			auto ptr = std::static_pointer_cast<ObjectType>(it1->second);
			if (!func(ptr))
				break;
		}
	}

private:
	TypeMap m_EntityMap;
};
} // namespace fury

#endif // _FURY_ENTITY_MANAGER_H_