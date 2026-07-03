#ifndef _FURY_ENTITY_UTIL_H_
#define _FURY_ENTITY_UTIL_H_

#include <functional>
#include <string>

namespace fury
{
	// Returns the first non-colliding name for `base` using the
	// suffix scheme `base`, `base (1)`, `base (2)`, ..., `base (N)`.
	//
	// For duplicate-style requests, the caller passes a base already
	// suffixed with " (copy)"; the helper then yields " (copy)",
	// " (copy 2)", " (copy 3)", ... by treating the numeric suffix
	// as a continuation counter.
	//
	// The helper scans suffix integers starting at 1 (for the (N)
	// form) or 2 (for the (copy N) form) up to a hard cap of 100000
	// and falls back to "base (?)" if exhausted.
	//
	// This helper lives outside the editor (no WITH_EDITOR gating) so
	// engine-side code (e.g. future Scene::Merge dedup) can use it.
	inline std::string UniqueName(const std::string& base,
		const std::function<bool(const std::string&)>& exists)
	{
		if (!exists(base))
			return base;

		static const std::string CopySuffix = " (copy)";
		const bool copyForm = base.size() >= CopySuffix.size() &&
			base.compare(base.size() - CopySuffix.size(),
				CopySuffix.size(), CopySuffix) == 0;

		if (copyForm)
		{
			// "Cube (copy)" -> prefix "Cube", candidates
			// "Cube (copy 2)", "Cube (copy 3)", ... starting at 2.
			const std::string prefix = base.substr(
				0, base.size() - CopySuffix.size());
			for (int i = 2; i <= 100000; ++i)
			{
				std::string candidate =
					prefix + " (copy " + std::to_string(i) + ")";
				if (!exists(candidate))
					return candidate;
			}
		}
		else
		{
			for (int i = 1; i <= 100000; ++i)
			{
				std::string candidate =
					base + " (" + std::to_string(i) + ")";
				if (!exists(candidate))
					return candidate;
			}
		}

		return base + " (?)";
	}
}

#endif // _FURY_ENTITY_UTIL_H_
