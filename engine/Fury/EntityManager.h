#ifndef _FURY_ENTITY_MANAGER_H_
#define _FURY_ENTITY_MANAGER_H_

#include "Macros.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>

#include "Fury/Log.h"

namespace fury {
class FURY_API EntityManager final {
public:
	typedef std::unordered_map<std::type_index, std::unordered_map<size_t, std::shared_ptr<void>>> TypeMap;

	// file path -> canonical entity, per type.
	typedef std::unordered_map<std::type_index, std::unordered_map<std::string, std::shared_ptr<void>>> PathTypeMap;

	typedef std::shared_ptr<EntityManager> Ptr;

	static Ptr Create() {
		return std::make_shared<EntityManager>();
	}

	// class ObjectType must have function 'size_t GetHashCode()'.
	// Entities with a non-empty GetPath() are keyed by path: the first
	// Add for a path wins, later same-path Adds return false silently
	// (the uuid entry still registers so saved uuid references resolve).
	// Pathless entities use the first-registered-wins name index with a
	// one-shot duplicate warning.
	template<class ObjectType>
	bool Add(std::shared_ptr<ObjectType> pointer) {
		std::type_index key0 = typeid(ObjectType);
		size_t key1 = pointer->GetHashCode();

		auto& uuid_map = m_EntityMap[key0];
		if (uuid_map.find(key1) != uuid_map.end())
			return false;
		uuid_map.emplace(key1, std::static_pointer_cast<void>(pointer));

		const std::string path = pointer->GetPath();
		if (!path.empty()) {
			auto& paths = m_PathMap[key0];
			if (paths.find(path) != paths.end())
				return false;
			paths.emplace(path, std::static_pointer_cast<void>(pointer));
			return true;
		}

		IndexName(key0, key1, pointer->GetName());
		return true;
	}

	// First-registered-wins name index + loud duplicates. Only called
	// for pathless entities; path-keyed Add is silent by contract.
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
				  << "' (" << type.name() << "') -- Get(name) keeps the first registered";
		}
	}

	template<class ObjectType>
	std::shared_ptr<ObjectType> Remove(size_t hashcode) {
		std::type_index key0 = typeid(ObjectType);

		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end())
			return nullptr;

		auto it1 = it0->second.find(hashcode);
		if (it1 == it0->second.end())
			return nullptr;

		auto ptr = it1->second;
		it0->second.erase(it1);

		const std::string path = std::static_pointer_cast<ObjectType>(ptr)->GetPath();
		if (!path.empty()) {
			auto itP = m_PathMap.find(key0);
			if (itP != m_PathMap.end()) {
				auto it = itP->second.find(path);
				if (it != itP->second.end() && it->second == ptr)
					itP->second.erase(it);
			}
		}

		return std::static_pointer_cast<ObjectType>(ptr);
	}

	// Remove by path for path-keyed entries (drops the canonical entry
	// plus every uuid-keyed duplicate filed under that path), by name
	// scan for pathless ones.
	template<class ObjectType>
	std::shared_ptr<ObjectType> Remove(const std::string& name) {
		std::type_index key0 = typeid(ObjectType);

		auto itP = m_PathMap.find(key0);
		if (itP != m_PathMap.end()) {
			auto it = itP->second.find(name);
			if (it != itP->second.end()) {
				auto ptr = it->second;
				itP->second.erase(it);
				auto it0 = m_EntityMap.find(key0);
				if (it0 != m_EntityMap.end()) {
					for (auto i = it0->second.begin(); i != it0->second.end();) {
						if (std::static_pointer_cast<ObjectType>(i->second)->GetPath() == name)
							i = it0->second.erase(i);
						else
							++i;
					}
				}
				return std::static_pointer_cast<ObjectType>(ptr);
			}
		}

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

	// Move the path-keyed entry oldPath -> newPath and update the
	// entity's own path. Returns false (entity untouched) when oldPath
	// is absent or newPath is already taken.
	template<class ObjectType>
	bool Repath(const std::string& oldPath, const std::string& newPath) {
		if (oldPath == newPath || newPath.empty())
			return false;
		std::type_index key0 = typeid(ObjectType);
		auto itP = m_PathMap.find(key0);
		if (itP == m_PathMap.end())
			return false;
		auto it = itP->second.find(oldPath);
		if (it == itP->second.end())
			return false;
		if (itP->second.find(newPath) != itP->second.end())
			return false;
		auto ptr = it->second;
		itP->second.erase(it);
		std::static_pointer_cast<ObjectType>(ptr)->SetPath(newPath);
		itP->second.emplace(newPath, ptr);
		return true;
	}

	template<class ObjectType>
	void RemoveAll() {
		auto it0 = m_EntityMap.find(typeid(ObjectType));
		if (it0 != m_EntityMap.end())
			it0->second.clear();
		auto itP = m_PathMap.find(typeid(ObjectType));
		if (itP != m_PathMap.end())
			itP->second.clear();
		auto itN = m_NameIndex.find(typeid(ObjectType));
		if (itN != m_NameIndex.end())
			itN->second.clear();
	}

	void RemoveAll() {
		m_EntityMap.clear();
		m_PathMap.clear();
		m_NameIndex.clear();
	}

	// Number of distinct entities: unique paths plus pathless entries.
	template<class ObjectType>
	size_t Count() {
		size_t n = 0;
		auto itP = m_PathMap.find(typeid(ObjectType));
		if (itP != m_PathMap.end())
			n += itP->second.size();
		auto it0 = m_EntityMap.find(typeid(ObjectType));
		if (it0 != m_EntityMap.end())
			for (const auto& kv : it0->second)
				if (std::static_pointer_cast<ObjectType>(kv.second)->GetPath().empty())
					++n;
		return n;
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

	// Lookup by path (the canonical asset identity) or by name for
	// pathless entities. Probe order: path map, name index, then a
	// self-healing scan that re-indexes pathless hits. The scan also
	// matches basenames, so legacy callers passing a texture's display
	// name still resolve.
	template<class ObjectType>
	std::shared_ptr<ObjectType> Get(const std::string& name) {
		std::type_index key0 = typeid(ObjectType);

		auto itP = m_PathMap.find(key0);
		if (itP != m_PathMap.end()) {
			auto it = itP->second.find(name);
			if (it != itP->second.end())
				return std::static_pointer_cast<ObjectType>(it->second);
		}

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
			idx.erase(itN); // stale entry -- fall through to scan
		}

		for (const auto& kv : it0->second) {
			auto ptr = std::static_pointer_cast<ObjectType>(kv.second);
			if (ptr->GetName() == name) {
				if (ptr->GetPath().empty())
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

	// Walks the path-keyed map (canonical entries, structurally
	// deduplicated) then the pathless uuid-keyed entries.
	// return false in your functor to break the loop.
	template<class ObjectType>
	void ForEach(const std::function<bool(const std::shared_ptr<ObjectType>&)>& func) {
		std::type_index key0 = typeid(ObjectType);

		auto itP = m_PathMap.find(key0);
		if (itP != m_PathMap.end()) {
			for (const auto& kv : itP->second) {
				auto ptr = std::static_pointer_cast<ObjectType>(kv.second);
				if (!func(ptr))
					return;
			}
		}

		auto it0 = m_EntityMap.find(key0);
		if (it0 == m_EntityMap.end())
			return;

		for (const auto& kv : it0->second) {
			auto ptr = std::static_pointer_cast<ObjectType>(kv.second);
			if (ptr->GetPath().empty() && !func(ptr))
				return;
		}
	}

private:
	TypeMap m_EntityMap;

	PathTypeMap m_PathMap;

	// name -> UUID-hashcode of the first pathless entity registered
	// under that name, per type. Maintained by Add/Get (self-healing).
	std::unordered_map<std::type_index, std::unordered_map<std::string, size_t>> m_NameIndex;

	// (type, name) pairs already warned about -- one-shot spam guard.
	std::set<std::pair<std::type_index, std::string>> m_DupWarned;
};
} // namespace fury

#endif // _FURY_ENTITY_MANAGER_H_
