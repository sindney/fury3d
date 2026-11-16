#include "Fury/SceneCompare.h"

#include <rapidjson/document.h>

#include <cmath>
#include <cstdio>
#include <functional>

#include "Fury/AnimationClip.h"
#include "Fury/EntityManager.h"
#include "Fury/FileUtil.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/ParticleSystem.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Texture.h"

namespace fury
{
	// ------------------------------------------------------------------
	// CanonNode
	// ------------------------------------------------------------------

	CanonNode CanonNode::Obj()
	{
		CanonNode n;
		n.kind = Kind::Object;
		return n;
	}

	CanonNode CanonNode::Arr()
	{
		CanonNode n;
		n.kind = Kind::Array;
		return n;
	}

	CanonNode CanonNode::Str(const std::string &value)
	{
		CanonNode n;
		n.kind = Kind::String;
		n.scalar = value;
		n.raw = value;
		return n;
	}

	static std::string RawReal(double value)
	{
		if (std::isnan(value)) return "nan";
		if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.9g", value);
		return buf;
	}

	CanonNode CanonNode::Int(long long value)
	{
		CanonNode n;
		n.kind = Kind::Integer;
		n.integer = value;
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%lld", value);
		n.scalar = std::string("i") + buf;
		n.raw = buf;
		return n;
	}

	CanonNode CanonNode::UInt(unsigned long long value)
	{
		CanonNode n;
		n.kind = Kind::Integer;
		n.integer = static_cast<long long>(value);
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%llu", value);
		n.scalar = std::string("i") + buf;
		n.raw = buf;
		return n;
	}

	CanonNode CanonNode::Real(double value)
	{
		CanonNode n;
		n.kind = Kind::Real;
		n.real = value;
		n.raw = RawReal(value);
		if (std::isnan(value))
			n.scalar = "fnan";
		else if (std::isinf(value))
			n.scalar = value > 0 ? "finf" : "f-ninf";
		else
			n.scalar = "f" + n.raw;
		return n;
	}

	CanonNode CanonNode::Bool(bool value)
	{
		CanonNode n;
		n.kind = Kind::Boolean;
		n.boolean = value;
		n.scalar = value ? "true" : "false";
		n.raw = n.scalar;
		return n;
	}

	CanonNode CanonNode::Nil()
	{
		return CanonNode();
	}

	// ------------------------------------------------------------------
	// Document to canonical tree
	// ------------------------------------------------------------------

	namespace
	{
		CanonNode FromJson(const rapidjson::Value &value)
		{
			if (value.IsObject())
			{
				CanonNode node = CanonNode::Obj();
				for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
				{
					if (!it->name.IsString()) continue;
					node.object[it->name.GetString()] = FromJson(it->value);
				}
				return node;
			}
			if (value.IsArray())
			{
				CanonNode node = CanonNode::Arr();
				node.array.reserve(value.Size());
				for (const auto &item : value.GetArray())
					node.array.push_back(FromJson(item));
				return node;
			}
			if (value.IsString()) return CanonNode::Str(value.GetString());
			if (value.IsBool()) return CanonNode::Bool(value.GetBool());
			if (value.IsNull()) return CanonNode::Nil();
			if (value.IsInt64()) return CanonNode::Int(value.GetInt64());
			if (value.IsUint64()) return CanonNode::UInt(value.GetUint64());
			if (value.IsDouble()) return CanonNode::Real(value.GetDouble());
			return CanonNode::Nil();
		}
	}

	bool SceneCompare::Parse(const std::string &json, CanonNode &out)
	{
		rapidjson::Document doc;
		doc.template Parse<rapidjson::kParseFullPrecisionFlag>(json.c_str(), json.size());
		if (doc.HasParseError())
		{
			FURYE << "SceneCompare::Parse: json parse error " << doc.GetParseError();
			return false;
		}
		out = FromJson(doc);
		return true;
	}

	// ------------------------------------------------------------------
	// Diff
	// ------------------------------------------------------------------

	namespace
	{
		bool ScalarEqual(const CanonNode &a, const CanonNode &b)
		{
			if (a.kind != b.kind) return false;
			return a.scalar == b.scalar;
		}

		// Abs/relative float tolerance; NaN matches NaN.
		bool RealsEqual(double a, double b)
		{
			if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
			if (a == b) return true;
			const double tol = SceneCompare::kFloatTolerance;
			return std::fabs(a - b) <= tol * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
		}

		std::string PathJoin(const std::string &base, const std::string &key)
		{
			return base.empty() ? key : base + "/" + key;
		}

		std::string PathIndex(const std::string &base, size_t index)
		{
			return base + "[" + std::to_string(index) + "]";
		}

		void DiffInto(const CanonNode &a, const CanonNode &b, const std::string &path,
			SceneDiff &out)
		{
			if (a.kind == CanonNode::Kind::Object && b.kind == CanonNode::Kind::Object)
			{
				auto ia = a.object.begin();
				auto ib = b.object.begin();
				while (ia != a.object.end() || ib != b.object.end())
				{
					if (ia != a.object.end() && ib != b.object.end() && ia->first == ib->first)
					{
						DiffInto(ia->second, ib->second, PathJoin(path, ia->first), out);
						++ia;
						++ib;
					}
					else if (ib == b.object.end() || (ia != a.object.end() && ia->first < ib->first))
					{
						DiffInto(ia->second, CanonNode::Nil(), PathJoin(path, ia->first), out);
						++ia;
					}
					else
					{
						DiffInto(CanonNode::Nil(), ib->second, PathJoin(path, ib->first), out);
						++ib;
					}
				}
				return;
			}

			if (a.kind == CanonNode::Kind::Array && b.kind == CanonNode::Kind::Array)
			{
				const size_t n = std::max(a.array.size(), b.array.size());
				for (size_t i = 0; i < n; ++i)
				{
					const CanonNode *pa = i < a.array.size() ? &a.array[i] : nullptr;
					const CanonNode *pb = i < b.array.size() ? &b.array[i] : nullptr;
					const std::string p = PathIndex(path, i);
					if (pa && pb) DiffInto(*pa, *pb, p, out);
					else if (pa) DiffInto(*pa, CanonNode::Nil(), p, out);
					else DiffInto(CanonNode::Nil(), *pb, p, out);
				}
				return;
			}

			if (a.kind == CanonNode::Kind::Real && b.kind == CanonNode::Kind::Real)
			{
				if (RealsEqual(a.real, b.real))
					return;
			}
			else if (ScalarEqual(a, b))
			{
				return;
			}

			SceneDiffEntry entry;
			entry.path = path.empty() ? "<root>" : path;
			entry.first = a.IsNull() ? "<missing>" : a.scalar;
			entry.second = b.IsNull() ? "<missing>" : b.scalar;
			entry.firstRaw = a.raw.empty() ? entry.first : a.raw;
			entry.secondRaw = b.raw.empty() ? entry.second : b.raw;
			out.entries.push_back(std::move(entry));
			out.equal = false;
		}
	}

	SceneDiff SceneCompare::Diff(const CanonNode &first, const CanonNode &second)
	{
		SceneDiff out;
		DiffInto(first, second, "", out);
		return out;
	}

	SceneDiff SceneCompare::DiffJson(const std::string &first, const std::string &second)
	{
		CanonNode a;
		CanonNode b;
		if (!Parse(first, a) || !Parse(second, b))
		{
			SceneDiff out;
			out.equal = false;
			SceneDiffEntry entry;
			entry.path = "<root>";
			entry.first = "unparsable document";
			entry.second = "unparsable document";
			out.entries.push_back(entry);
			return out;
		}
		return Diff(a, b);
	}

	SceneDiff SceneCompare::DiffFiles(const std::string &firstPath, const std::string &secondPath)
	{
		std::string first;
		std::string second;
		if (!FileUtil::ReadDocumentByExtension(firstPath, first) ||
			!FileUtil::ReadDocumentByExtension(secondPath, second))
		{
			SceneDiff out;
			out.equal = false;
			SceneDiffEntry entry;
			entry.path = "<root>";
			entry.first = "unreadable document";
			entry.second = "unreadable document";
			out.entries.push_back(entry);
			return out;
		}
		return DiffJson(first, second);
	}

	// ------------------------------------------------------------------
	// Counts
	// ------------------------------------------------------------------

	SceneCounts SceneCompare::Counts(const std::shared_ptr<Scene> &scene)
	{
		SceneCounts out;
		if (!scene) return out;
		std::function<void(const SceneNode::Ptr &)> walk =
			[&](const SceneNode::Ptr &node)
			{
				if (!node) return;
				++out.nodes;
				if (node->GetComponent<ParticleRenderer>()) ++out.particle_renderers;
				if (auto render = node->GetComponent<MeshRender>())
				{
					if (auto mesh = render->GetMesh())
					{
						for (unsigned int s = 0; s < mesh->GetSubMeshCount(); ++s)
						{
							++out.submeshes;
							auto sub = mesh->GetSubMeshAt(s);
							if (sub) out.triangles += static_cast<long long>(sub->Indices.Data.size() / 3);
						}
						out.vertices += static_cast<long long>(mesh->Positions.Data.size() / 3);
					}
				}
				for (unsigned int i = 0; i < node->GetChildCount(); ++i)
					walk(node->GetChildAt(i));
			};
		walk(scene->GetRootNode());
		auto em = scene->GetEntityManager();
		em->ForEach<Mesh>([&](const Mesh::Ptr &m) -> bool
		{
			if (m->IsSkinnedMesh()) ++out.meshes_skinned;
			else ++out.meshes_static;
			out.joints += m->GetJointCount();
			return true;
		});
		em->ForEach<Material>([&](const Material::Ptr &) -> bool { ++out.materials; return true; });
		em->ForEach<Texture>([&](const Texture::Ptr &) -> bool { ++out.textures; return true; });
		em->ForEach<AnimationClip>([&](const AnimationClip::Ptr &) -> bool { ++out.animations; return true; });
		em->ForEach<ParticleSystem>([&](const ParticleSystem::Ptr &) -> bool { ++out.particle_systems; return true; });
		out.meshes_total = out.meshes_static + out.meshes_skinned;
		return out;
	}
}
