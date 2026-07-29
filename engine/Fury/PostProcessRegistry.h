#ifndef _FURY_POSTPROCESS_REGISTRY_H_
#define _FURY_POSTPROCESS_REGISTRY_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Fury/Macros.h"
#include "Fury/PostProcessEffect.h"

namespace fury
{
	// Process-global registry of postprocess effects, resolved by name.
	class FURY_API PostProcessRegistry
	{
	public:

		// Load and register every *.json directly under dir.
		// Returns the count loaded; broken files are logged and skipped.
		static unsigned int LoadFromDirectory(const std::string &dir);

		// Register an effect explicitly.
		static void Register(const PostProcessEffect::Ptr &effect);

		static PostProcessEffect::Ptr Get(const std::string &name);

		static std::vector<PostProcessEffect::Ptr> GetAll();

		// All registered effects in canonical chain order: (stage,
		// order, name). This is THE chain order — the pipeline and
		// the editor both present/consume effects in this sequence;
		// users toggle entries on/off but never reorder them.
		static std::vector<PostProcessEffect::Ptr> GetSortedAll();

		static void Clear();

	private:

		static std::unordered_map<std::string, PostProcessEffect::Ptr> &Map();
	};
}

#endif // _FURY_POSTPROCESS_REGISTRY_H_