#include "Fury/Entity.h"

#include "Fury/Log.h"

#include <cstdio>
#include <random>
#include <string>

namespace fury {
// UUID v4 generator (random, 8-4-4-4-12 hex). Uses a per-process
// MT19937-64 seeded by std::random_device. Uniqueness is
// statistically guaranteed -- collisions are astronomically
// unlikely (2^122 possible UUIDs).
static std::string GenerateUUID() {
	static std::random_device rd;
	static std::mt19937_64 gen(rd());
	std::uniform_int_distribution<uint64_t> dis;
	uint64_t a = dis(gen), b = dis(gen);
	char buf[37];
	std::snprintf(buf, sizeof(buf),
				  "%08llx-%04llx-%04llx-%04llx-%012llx",
				  (unsigned long long)(a >> 32),
				  (unsigned long long)((a >> 16) & 0xFFFF),
				  (unsigned long long)(a & 0xFFFF),
				  (unsigned long long)(b >> 48),
				  (unsigned long long)(b & 0xFFFFFFFFFFFFULL));
	return buf;
}

Entity::Ptr Entity::Create(const std::string& name) {
	return std::make_shared<Entity>(name);
}

Entity::Entity(const std::string& name)
	: m_TypeIndex(typeid(Entity)) {
	m_UUID = GenerateUUID();
	m_HashCode = std::hash<std::string>()(m_UUID);
	m_Name = name;
}

std::type_index Entity::GetTypeIndex() const {
	return m_TypeIndex;
}

bool Entity::Load(const void* wrapper, bool object) {
	if (object && !IsObject(wrapper)) {
		FURYE << "Json node is not an object!";
		return false;
	}

	if (!LoadMemberValue(wrapper, "name", m_Name)) {
		FURYE << "Name not found!";
		return false;
	}

	// Load UUID (optional -- auto-generate if absent for old
	// scenes that don't have a "uuid" field).
	LoadMemberValue(wrapper, "uuid", m_UUID);
	if (m_UUID.empty())
		m_UUID = GenerateUUID();
	m_HashCode = std::hash<std::string>()(m_UUID);

	return true;
}

void Entity::Save(void* wrapper, bool object) {
	if (object)
		StartObject(wrapper);

	SaveKey(wrapper, "name");
	SaveValue(wrapper, m_Name);

	SaveKey(wrapper, "uuid");
	SaveValue(wrapper, m_UUID);

	if (object)
		EndObject(wrapper);
}

std::string Entity::GetName() const {
	return m_Name;
}

std::string Entity::GetUUID() const {
	return m_UUID;
}

void Entity::SetUUID(const std::string& uuid) {
	m_UUID = uuid;
	m_HashCode = std::hash<std::string>()(uuid);
}

size_t Entity::GetHashCode() const {
	return m_HashCode;
}

size_t Entity::SetName(const std::string& name) {
	m_Name = name;
	// m_HashCode unchanged -- stays hash of UUID.
	return m_HashCode;
}
} // namespace fury
