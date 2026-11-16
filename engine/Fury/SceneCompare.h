#ifndef _FURY_SCENECOMPARE_H_
#define _FURY_SCENECOMPARE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Macros.h"

namespace fury
{
	class Scene;

	// Canonical tree of a saved scene document.
	//
	// Object members are keyed by name, so the order members appear in the
	// document does not affect the tree. Array elements keep their order,
	// because array order carries meaning. Reals carry their raw value and
	// compare under a documented tolerance; every other kind compares by
	// an exact token.
	class FURY_API CanonNode
	{
	public:
		typedef std::map<std::string, CanonNode> ObjectMap;
		typedef std::vector<CanonNode> ArrayList;

		enum class Kind { Object, Array, String, Integer, Real, Boolean, Null };

		Kind kind = Kind::Null;
		ObjectMap object;
		ArrayList array;
		std::string scalar;   // comparison token
		std::string raw;      // human-readable value (diff reporting)
		long long integer = 0;
		double real = 0.0;
		bool boolean = false;

		static CanonNode Obj();
		static CanonNode Arr();
		static CanonNode Str(const std::string &value);
		static CanonNode Int(long long value);
		static CanonNode UInt(unsigned long long value);
		static CanonNode Real(double value);
		static CanonNode Bool(bool value);
		static CanonNode Nil();

		bool IsNull() const { return kind == Kind::Null; }
	};

	struct FURY_API SceneDiffEntry
	{
		std::string path;    // document path, e.g. nodes/childs[2]/components/Light/intensity
		std::string first;   // value in the first document ("<missing>" when absent)
		std::string second;  // value in the second document ("<missing>" when absent)
		std::string firstRaw;
		std::string secondRaw;
	};

	struct FURY_API SceneDiff
	{
		bool equal = true;
		std::vector<SceneDiffEntry> entries;
	};

	// Structural counts mirroring `fury info`; AABB is not counted.
	struct FURY_API SceneCounts
	{
		long long nodes = 0;
		long long meshes_static = 0;
		long long meshes_skinned = 0;
		long long meshes_total = 0;
		long long submeshes = 0;
		long long vertices = 0;
		long long triangles = 0;
		long long materials = 0;
		long long textures = 0;
		long long animations = 0;
		long long joints = 0;
		long long particle_systems = 0;
		long long particle_renderers = 0;
	};

	// Comparison of saved scene documents.
	class FURY_API SceneCompare
	{
	public:
		// Documented float tolerance: abs/relative 1e-4. Two reals a, b
		// compare equal when |a-b| <= kFloatTolerance * max(1, |a|, |b|).
		static constexpr double kFloatTolerance = 1e-4;

		// Parse a saved scene document into the canonical tree.
		static bool Parse(const std::string &json, CanonNode &out);

		// Field-path diff of two canonical trees.
		static SceneDiff Diff(const CanonNode &first, const CanonNode &second);

		// Diff two saved scene documents given as JSON text.
		static SceneDiff DiffJson(const std::string &first, const std::string &second);

		// Diff two saved scene documents given as file paths. A .bin path
		// is decompressed first, so a compressed document and a plain one
		// compare directly.
		static SceneDiff DiffFiles(const std::string &firstPath, const std::string &secondPath);

		// Structural counts of a live scene.
		static SceneCounts Counts(const std::shared_ptr<Scene> &scene);
	};
}

#endif // _FURY_SCENECOMPARE_H_
