#ifndef _FURY_FILEUTIL_H_
#define _FURY_FILEUTIL_H_

#include <fstream>
#include <string>
#include <vector>
#include <memory>

#include "Fury/EnumUtil.h"

#undef LoadString
#undef LoadImage

namespace fury
{
	class Mesh;

	class Pipeline;

	class Scene;

	class Serializable;

	class FURY_API FileUtil final
	{
	private:

		static std::string m_AbsPath;

		// RapidJSON's default -- full float round-trip precision.
		static constexpr int kMaxDecimalPlaces = 324;

	public:

		static std::string GetAbsPath();

		static std::string GetAbsPath(const std::string &source, bool toForwardSlash = false);

		static bool FileExist(const std::string &path);

		// image, text file io

		static bool LoadString(const std::string &path, std::string &output);

		static bool LoadImage(const std::string &path, std::vector<unsigned char> &output, int &width, int &height, int &channels);

		// serializable obj io

		static bool LoadFile(const std::shared_ptr<Serializable> &source, const std::string &filePath);

		static bool SaveFile(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces = kMaxDecimalPlaces);

		static bool LoadCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath);

		static bool SaveCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces = kMaxDecimalPlaces);

		// Pick the underlying serializer by lowercased extension: .json
		// -> SaveFile, .bin -> SaveCompressedFile. Returns false otherwise.
		static bool SaveByExtension(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces = kMaxDecimalPlaces);

		// Mirror of SaveByExtension: pick LoadFile (.json) or
		// LoadCompressedFile (.bin) by extension. Returns false on
		// unsupported extension.
		static bool LoadByExtension(const std::shared_ptr<Serializable> &source, const std::string &filePath);

		// In-memory serialization. Used by CloneTree to round-trip a
		// node subtree through the engine's existing Save/Load -- the
		// single source of truth for "what's in a SceneNode". This
		// avoids per-component Clone() maintenance: every component
		// already knows how to serialize itself, and Load resolves
		// weak_ptrs to entities in the active scene's EntityManager.
		static std::string SerializeToString(const std::shared_ptr<Serializable> &source, int maxDecimalPlaces = kMaxDecimalPlaces);

		static bool DeserializeFromString(const std::shared_ptr<Serializable> &target, const std::string &json);
	};
}

#endif // _FURY_FILEUTIL_H_
