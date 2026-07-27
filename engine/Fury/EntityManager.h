#ifndef _FURY_ENTITY_MANAGER_H_
#define _FURY_ENTITY_MANAGER_H_

#include "Macros.h"

#include <functional>
#include <memory>
#include <set>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "Fury/Log.h"

namespace fury {
class FURY_API EntityManager final {
public:
	typedef std::unordered_map<std::type_index, std::unordered_map<size_t, std::shared_ptr<void>>> TypeMap;

	typedef std::shared_ptr<EntityManager> Ptr;

	static Ptr Create() {
		return std::make_shared<EntityManager>();
	}

	// class ObjectType must have function 'size_t GetHashCode()'.
	// Get(name) returns the FIRST entity registered under a name;
	// later same-name entities still register but log a one-shot warning.
	template<class ObjectType>
	bool Add(std::shared_ptr<ObjectType> pointer) {
		std::type_index key0 = typeid(ObjectType);
		size_t key1 = pointer->GetHashCode();

		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end()) {
			m_EntityMap[key0].emplace(key1, std::static_pointer_cast<void>(pointer));
			IndexName(key0, key1, pointer->GetName());
			return true;
		}

		auto it1 = it0->second.find(key1);
		if (it1 == it0->second.end()) {
			it0->second.emplace(key1, std::static_pointer_cast<void>(pointer));
			IndexName(key0, key1, pointer->GetName());
			return true;
		}

		return false;
	}

	// First-registered-wins name index + loud duplicates.
	void IndexName(std::type_index type, size_t hashcode, const std::string& name) {
		if (name.empty()) return; // unnamed entities stay scan-only

		auto& idx = m_NameIndex[type];
		auto it = idx.find(name);
		if (it == idx.end()) {
			idx.emplace(name, hashcode);
			return;
		}
		if (it->second != hashcode &&
			m_DupWarned.insert(std::make_pair(type, name)).second) {
			FURYW << "EntityManager: duplicate entity name '" << name
				  << "' (" << type.name() << "') — Get(name) keeps the first registered";
		}
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
		auto itN = m_NameIndex.find(typeid(ObjectType));
		if (itN != m_NameIndex.end())
			itN->second.clear();
	}

	void RemoveAll() {
		m_EntityMap.clear();
		m_NameIndex.clear();
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

	// Name-based lookup. O(1) via the first-registered-wins name
	// index; falls back to a scan when the index entry is stale
	// (entity removed or renamed) and re-indexes what it finds, so
	// the index heals itself instead of drifting.
	template<class ObjectType>
	std::shared_ptr<ObjectType> Get(const std::string& name) {
		std::type_index key0 = typeid(ObjectType);
		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end())
			return nullptr;

		auto& idx = m_NameIndex[key0];
		auto itN = idx.find(name);
		if (itN != idx.end()) {
			auto itE = it0->second.find(itN->second);
			if (itE != it0->second.end()) {
				auto ptr = std::static_pointer_cast<ObjectType>(itE->second);
				if (ptr->GetName() == name)
					return ptr;
			}
			idx.erase(itN); // stale entry — fall through to scan
		}

		for (const auto& kv : it0->second) {
			auto ptr = std::static_pointer_cast<ObjectType>(kv.second);
			if (ptr->GetName() == name) {
				idx.emplace(name, kv.first);
				return ptr;
			}
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

	// name -> UUID-hashcode of the first entity registered under
	// that name, per type. Maintained by Add/Get (self-healing).
	std::unordered_map<std::type_index, std::unordered_map<std::string, size_t>> m_NameIndex;

	// (type, name) pairs already warned about — one-shot spam guard.
	std::set<std::pair<std::type_index, std::string>> m_DupWarned;
};
} // namespace fury

#endif // _FURY_ENTITY_MANAGER_H_