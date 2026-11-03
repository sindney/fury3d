#ifndef _FURY_SCENEFINGERPRINT_H_
#define _FURY_SCENEFINGERPRINT_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Macros.h"

namespace fury
{
	class Scene;

	// Canonical persistent-state fingerprint of a runtime Scene.
	//
	// Reads live objects through public getters; never routes through
	// Serializable::Save/Load or archive output. Unordered containers are
	// canonicalized by stable path/name key; semantic order (children, LOD
	// chains, joints, postprocess chains, material slots) is preserved.
	// IDs, paths, enums, and counts are exact; floats compare under a
	// documented abs/relative tolerance (kFloatTolerance). Transient state
	// (GPU objects, playback cursors, editor-only subtrees) is excluded.
	class FURY_API FpNode
	{
	public:
		typedef std::map<std::string, FpNode> ObjectMap;
		typedef std::vector<FpNode> ArrayList;

		enum class Kind { Object, Array, String, Integer, Real, Boolean, Null };

		Kind kind = Kind::Null;
		ObjectMap object;
		ArrayList array;
		std::string scalar;   // canonical token; for Real it carries the raw value
		std::string raw;      // human-readable raw value (diff reporting)
		long long integer = 0;
		double real = 0.0;
		bool boolean = false;

		static FpNode Obj();
		static FpNode Arr();
		static FpNode Str(const std::string &value);
		static FpNode Int(long long value);
		static FpNode UInt(unsigned long long value);
		static FpNode Real(double value);
		static FpNode Bool(bool value);
		static FpNode Nil();

		// Object helpers (keys sorted by std::map, so insertion order is
		// irrelevant to the canonical form).
		FpNode &Set(const std::string &key, const FpNode &value);
		FpNode &Set(const std::string &key, const std::string &value) { return Set(key, Str(value)); }
		FpNode &Set(const std::string &key, const char *value) { return Set(key, Str(std::string(value))); }
		FpNode &Set(const std::string &key, long long value) { return Set(key, Int(value)); }
		FpNode &Set(const std::string &key, int value) { return Set(key, Int(value)); }
		FpNode &Set(const std::string &key, unsigned int value) { return Set(key, UInt(value)); }
		FpNode &Set(const std::string &key, size_t value) { return Set(key, UInt(value)); }
		FpNode &Set(const std::string &key, bool value) { return Set(key, Bool(value)); }
		FpNode &Set(const std::string &key, float value) { return Set(key, Real(value)); }
		FpNode &Set(const std::string &key, double value) { return Set(key, Real(value)); }

		bool IsNull() const { return kind == Kind::Null; }

		// Find a child of an object node; nullptr when absent/not an object.
		const FpNode *Find(const std::string &key) const;
	};

	struct FURY_API FingerprintDiffEntry
	{
		std::string path;    // structured field path, e.g. nodes[2]/components/Light/intensity
		std::string first;   // canonical token in the first fingerprint ("<missing>" when absent)
		std::string second;  // canonical token in the second fingerprint ("<missing>" when absent)
		std::string firstRaw;
		std::string secondRaw;
	};

	struct FURY_API FingerprintDiff
	{
		bool equal = true;
		std::vector<FingerprintDiffEntry> entries;
	};

	// Structural counts mirroring `fury info`; AABB is not fingerprinted.
	struct FURY_API SceneFingerprintCounts
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

	class FURY_API SceneFingerprint
	{
	public:
		// Documented float tolerance: abs/relative 1e-4. Two floats a, b
		// compare equal when |a-b| <= kFloatTolerance * max(1, |a|, |b|).
		// Quaternions are sign-canonicalized before comparison.
		static constexpr double kFloatTolerance = 1e-4;

		// Canonical fingerprint tree of a scene's persistent runtime state.
		// Editor-only subtrees are skipped.
		static FpNode Build(const std::shared_ptr<Scene> &scene);

		// Deterministic single-line text form; Parse reads it back.
		static std::string Canonicalize(const FpNode &node);

		// Inverse of Canonicalize; false on malformed input.
		static bool Parse(const std::string &text, FpNode &out);

		// Structured field-path diff; Real leaves use the tolerance above.
		static FingerprintDiff Diff(const FpNode &first, const FpNode &second);

		// Convenience: Parse both canonical strings, then Diff.
		static FingerprintDiff DiffStrings(const std::string &first, const std::string &second);

		static SceneFingerprintCounts Counts(const std::shared_ptr<Scene> &scene);
	};
}

#endif // _FURY_SCENEFINGERPRINT_H_
