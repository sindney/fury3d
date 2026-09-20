#ifndef _FURY_DDC_STORE_H_
#define _FURY_DDC_STORE_H_

#include <string>

#include "Macros.h"

namespace fury
{
	// Derived Data Cache: content-addressed store of cooked artifacts.
	// Keys are SHA-1 hex strings over (type tag | cooker version | cook
	// target | format | usage flags | source content hash); entries live
	// at <root>/<hex[0:2]>/<hex[2:4]>/<hex>.ktx2 so folders stay small.
	class FURY_API DdcStore final
	{
	public:

		// Root resolution order: explicit path, FURY_DDC env, then
		// <executable dir>/DDC.
		static std::string ResolveRoot(const std::string& explicitPath = "");

		explicit DdcStore(std::string root);

		// Builds the texture cook key. settingsString already contains
		// target/format/usage/tool-version (caller-assembled).
		static std::string MakeKey(const std::string& typeTag, const std::string& settingsString,
			const std::string& sourceSha1Hex);

		// <root>/ab/cd/<key>.ktx2
		std::string PathForKey(const std::string& hexKey, const std::string& ext = ".ktx2") const;

		bool Has(const std::string& hexKey) const;

		// Copies srcFile under the key (creating tier dirs). Returns the
		// stored path, or "" on failure.
		std::string Store(const std::string& hexKey, const std::string& srcFile);

		// Stored path when present, "" otherwise.
		std::string Lookup(const std::string& hexKey) const;

		const std::string& Root() const { return m_Root; }

	private:

		std::string m_Root;
	};
}

#endif // _FURY_DDC_STORE_H_
