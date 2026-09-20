#ifndef _FURY_COOK_PIPELINE_H_
#define _FURY_COOK_PIPELINE_H_

#include <memory>
#include <string>
#include <vector>

#include "Macros.h"

namespace fury
{
	class Scene;

	// Options for CookSceneAssets. Empty strings select the documented
	// defaults in each field's comment.
	struct FURY_API CookOptions
	{
		// "legacy" | "modern"; "" -> host default (legacy on macOS, modern elsewhere).
		std::string target = "";

		// "" -> DdcStore::ResolveRoot (FURY_DDC env, then <exe dir>/DDC).
		std::string ddcRoot = "";

		// "" -> FURY_KTX_CLI env, then <exe dir>/ktx.
		std::string ktxPath = "";

		// "" -> <scenePath-without-ext>.cookmanifest.json.
		std::string manifestOut = "";

		// "" -> <scenePath-without-ext>.cook.json when that file exists.
		std::string overridesPath = "";

		bool verbose = false;
	};

	struct FURY_API CookResult
	{
		int texturesTotal = 0, texturesCooked = 0, texturesDdcHits = 0;

		// Referenced paths missing on disk; the caller exits non-zero.
		std::vector<std::string> unresolved;

		std::string manifestPath;

		// Non-empty on hard failure (tool missing, ktx failed, ...).
		std::string error;
	};

	// ktx CLI invocations performed by the last CookSceneAssets call
	// (3 per cooked texture); stays 0 on a full DDC hit. Test hook.
	extern FURY_API int g_CookKtxInvocations;

	// Enumerates the scene's referenced assets, block-compresses its
	// textures through the vendored ktx CLI into the DDC, and writes a
	// cook manifest JSON. Headless: no GL required.
	FURY_API CookResult CookSceneAssets(const std::shared_ptr<Scene>& scene,
		const std::string& scenePath, const CookOptions& opts);
}

#endif // _FURY_COOK_PIPELINE_H_
