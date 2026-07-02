#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#if !defined(_WIN32)
#include <unistd.h>     // getcwd
#endif

#if defined(_WIN32)
#include <winsock.h>
#else
#include <arpa/inet.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP

#include "stb_image.h"

#include "lz4.h"

#include "Fury/EntityManager.h"
#include "Fury/FileUtil.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshUtil.h"
#include "Fury/Log.h"
#include "Fury/Uniform.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Serializable.h"
#include "Fury/Texture.h"

#undef far
#undef near
#undef max

namespace fury
{
	std::string FileUtil::m_AbsPath = "";

	std::string FileUtil::GetAbsPath()
	{
#if defined(__APPLE__)
		// CWD-based resolution. The legacy CFBundle path (kept commented
		// out below) returns the .app's Resources directory — convenient
		// for app-bundle distributions, awkward for the editor workflow
		// because it forces every asset to live inside the bundle. With
		// CWD resolution, `cd examples && ./bin/fury` finds Resource/
		// next to the current directory, so assets can sit alongside
		// the launcher script rather than being copied into bin/.
		if (m_AbsPath.size() == 0)
		{
			char cwd_path[1024];
			if (getcwd(cwd_path, sizeof(cwd_path)) != nullptr)
			{
				m_AbsPath = std::string(cwd_path) + '/';
			}
			else
			{
				FURYE << "getcwd failed; absolute path resolution may misbehave";
			}
		}

		// Legacy CFBundle resolution — restore by uncommenting if you
		// ever ship the engine inside a .app bundle.
		// if (m_AbsPath.size() == 0)
		// {
		// 	CFBundleRef mainBundle = CFBundleGetMainBundle();
		// 	CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
		// 	char path[512];
		// 	if (!CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8*)path, 512))
		// 	{
		// 		FURYE << "Absolute Path Not Found!";
		// 	}
		// 	CFRelease(resourcesURL);
		// 	m_AbsPath = std::string(path) + '/';
		// }
#endif
		return m_AbsPath;
	}

	std::string FileUtil::GetAbsPath(const std::string &source, bool toForwardSlash)
	{
		std::string clone = source;
		if (toForwardSlash)
			std::replace(clone.begin(), clone.end(), '\\', '/');

		return GetAbsPath() + clone;
	}

	bool FileUtil::FileExist(const std::string &path)
	{
		std::ifstream stream(path.c_str());
		if (stream.good())
		{
			stream.close();
			return true;
		}
		else
		{
			FURYE << "File " << path << " not exist!";
			stream.close();
			return false;
		}
	}

	// file io

	bool FileUtil::LoadString(const std::string &path, std::string &output)
	{
		std::ifstream stream(path, std::ios::in);
		if (stream)
		{
			stream.seekg(0, std::ios::end);
			size_t size = (size_t)stream.tellg();
			stream.seekg(0, std::ios::beg);

			output.resize(size);

			stream.read(&output[0], size);
			stream.close();

			return size == output.size();
		}
		else
		{
			FURYW << "Failed to load chars: " << path;
			return false;
		}
	}

	bool FileUtil::LoadImage(const std::string &path, std::vector<unsigned char> &output, int &width, int &height, int &channels)
	{
		if (!FileExist(path))
			return false;

		unsigned char* ptr = stbi_load(path.c_str(), &width, &height, &channels, 0);
		if (ptr && width && height)
		{
			output.resize(width * height * channels);
			memcpy(&output[0], ptr, output.size());

			stbi_image_free(ptr);

			return true;
		}
		else
		{
			FURYW << "Failed to load image: " << path;
			return false;
		}
	}

	namespace
	{
		// Sniff a sensible extension from the encoded image's magic bytes.
		// Mirrors the formats stb_image is built with (JPEG, PNG, BMP).
		std::string DetectImageExtension(const std::vector<unsigned char> &bytes)
		{
			if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xD8)
				return ".jpg";
			if (bytes.size() >= 4 && bytes[0] == 0x89 && bytes[1] == 0x50
				&& bytes[2] == 0x4E && bytes[3] == 0x47)
				return ".png";
			if (bytes.size() >= 2 && bytes[0] == 0x42 && bytes[1] == 0x4D)
				return ".bmp";
			return ".bin";
		}

		// Byte-for-byte equality between an existing file and an in-memory
		// buffer. Used to skip rewrites when the same scene is saved twice
		// to the same place. Returns false if the file is missing, the
		// length differs, or any byte differs.
		bool FileBytesEqual(const std::filesystem::path &path,
			const std::vector<unsigned char> &bytes)
		{
			std::error_code ec;
			if (!std::filesystem::exists(path, ec) || ec) return false;
			auto sz = std::filesystem::file_size(path, ec);
			if (ec || sz != bytes.size()) return false;
			std::ifstream in(path, std::ios::binary);
			if (!in) return false;
			std::vector<unsigned char> on_disk(bytes.size());
			in.read(reinterpret_cast<char *>(on_disk.data()), on_disk.size());
			if (!in) return false;
			return on_disk == bytes;
		}

		// Walk every Material in the scene's EntityManager; for each
		// memory-backed texture, write its encoded bytes to a sibling
		// file of the output scene and transition the texture to
		// file-backed (m_FilePath set, encoded bytes preserved on the
		// Texture in case the user saves to a different location later).
		// Returns false on any IO failure (with a FURYE message naming
		// the path); true on success or no-op.
		bool ExtractMemoryBackedTextures(
			const std::shared_ptr<Serializable> &source,
			const std::filesystem::path &output_dir,
			const std::string &output_stem)
		{
			auto scene = std::dynamic_pointer_cast<Scene>(source);
			if (!scene) return true;  // not a Scene; nothing to extract

			auto entities = scene->GetEntityManager();
			if (!entities) return true;

			std::unordered_set<std::string> used_filenames;
			bool ok = true;
			entities->ForEach<Material>([&](const std::shared_ptr<Material> &mat) -> bool {
				for (const auto &pair : mat->GetTextures())
				{
					const auto &tex = pair.second;
					if (!tex) continue;
					// Extract textures that carry encoded bytes (whether
					// pristine memory-backed or already file-backed from
					// a previous save in a different output directory).
					// File-backed textures with no encoded bytes are
					// passed through untouched.
					if (tex->GetEncodedBytes().empty()) continue;

					const auto &bytes = tex->GetEncodedBytes();
					std::string filename = tex->GetOriginalFilename();
					// Take just the basename — original-filename hints can
					// arrive as a path on some glTF tools.
					if (!filename.empty())
					{
						auto slash = filename.find_last_of("/\\");
						if (slash != std::string::npos) filename = filename.substr(slash + 1);
					}
					if (filename.empty())
					{
						filename = output_stem + "_" + tex->GetName()
							+ DetectImageExtension(bytes);
					}

					// Resolve filename collisions. Two textures asking for the
					// same filename (e.g. both fall back to a synthesized hint)
					// get a numeric suffix on the second/third/etc.
					std::string final_name = filename;
					if (used_filenames.count(final_name))
					{
						auto dot = filename.find_last_of('.');
						std::string stem = (dot == std::string::npos)
							? filename : filename.substr(0, dot);
						std::string ext = (dot == std::string::npos)
							? std::string{} : filename.substr(dot);
						int suffix = 2;
						do
						{
							final_name = stem + "_" + std::to_string(suffix++) + ext;
						} while (used_filenames.count(final_name));
					}
					used_filenames.insert(final_name);

					std::filesystem::path out_file = output_dir.empty()
						? std::filesystem::path(final_name)
						: output_dir / final_name;
					if (!FileBytesEqual(out_file, bytes))
					{
						std::error_code ec;
						if (!output_dir.empty())
						{
							std::filesystem::create_directories(output_dir, ec);
						}
						std::ofstream out(out_file, std::ios::binary);
						if (!out)
						{
							FURYE << "FileUtil::ExtractMemoryBackedTextures: failed to open '"
								<< out_file.string() << "' for write";
							ok = false;
							return false;
						}
						out.write(reinterpret_cast<const char *>(bytes.data()),
							static_cast<std::streamsize>(bytes.size()));
						if (!out)
						{
							FURYE << "FileUtil::ExtractMemoryBackedTextures: write failed for '"
								<< out_file.string() << "'";
							ok = false;
							return false;
						}
					}

					// Transition to file-backed. m_FilePath is the bare
					// filename (no directory) — load-time resolution joins
					// it against the active scene's working_dir.
					tex->SetFilePathAndSRGB(final_name, tex->IsSRGB());
				}
				return true;
			});
			return ok;
		}
	}

	bool FileUtil::LoadFile(const Serializable::Ptr &source, const std::string &filePath)
	{
		using namespace rapidjson;

		std::ifstream stream(filePath);
		if (stream)
		{
			Document dom;

			std::stringstream buffer;
			buffer << stream.rdbuf();
			stream.close();

			dom.Parse(buffer.str().c_str());

			if (dom.HasParseError())
			{
				FURYE << "Error parsing json file " << filePath << ": " << dom.GetParseError();
				return false;
			}

			if (!source->Load(&dom))
			{
				FURYE << "Serialization failed!";
				return false;
			}

			FURYD << filePath << " successfully deserialized!";
			return true;
		}
		else
		{
			FURYE << "Path " << filePath << " not found!";
			return false;
		}
	}

	bool FileUtil::SaveFile(const Serializable::Ptr &source, const std::string &filePath, int maxDecimalPlaces)
	{
		using namespace rapidjson;

		std::filesystem::path output_path(filePath);
		std::filesystem::path output_dir = output_path.parent_path();
		std::string output_stem = output_path.stem().string();
		if (!ExtractMemoryBackedTextures(source, output_dir, output_stem))
			return false;

		std::ofstream output(filePath);
		if (output)
		{
			StringBuffer sb;
			PrettyWriter<StringBuffer> writer(sb);
			writer.SetMaxDecimalPlaces(maxDecimalPlaces);

			source->Save(&writer);

			output.write(sb.GetString(), sb.GetSize());
			output.close();

			FURYD << filePath << " successfully serialized!";
			return true;
		}
		else
		{
			FURYE << "Path " << filePath << " not found!";
			return false;
		}
	}

	bool FileUtil::LoadCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath)
	{
		using namespace rapidjson;

		std::ifstream stream(filePath, std::ios_base::binary);
		if (stream)
		{
			Document dom;

			{
				uint32_t orgSize, compressSize, netOrgSize, netCompressSize;

				stream.read((char*)&netOrgSize, sizeof(uint32_t));
				orgSize = ntohl(netOrgSize);

				stream.read((char*)&netCompressSize, sizeof(uint32_t));
				compressSize = ntohl(netCompressSize);

				char *srcBuffer = new char[compressSize];
				stream.read(srcBuffer, compressSize);

				char* buffer = new char[orgSize];

				int size = LZ4_decompress_fast(srcBuffer, buffer, orgSize);
				if (size == 0)
				{
					FURYE << "Failed to decompress data!";
					return false;
				}

				dom.Parse(buffer, orgSize);

				delete[] buffer;
				delete[] srcBuffer;
			}

			if (dom.HasParseError())
			{
				FURYE << "Error parsing json file " << filePath << ": " << dom.GetParseError();
				return false;
			}

			if (!source->Load(&dom))
			{
				FURYE << "Deserialization failed!";
				return false;
			}

			FURYD << filePath << " successfully deserialized!";
			return true;
		}
		else
		{
			FURYE << "Path " << filePath << " not found!";
			return false;
		}
	}

	bool FileUtil::SaveCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces)
	{
		using namespace rapidjson;

		std::filesystem::path output_path(filePath);
		std::filesystem::path output_dir = output_path.parent_path();
		std::string output_stem = output_path.stem().string();
		if (!ExtractMemoryBackedTextures(source, output_dir, output_stem))
			return false;

		std::ofstream stream(filePath, std::ios_base::binary);
		if (stream)
		{
			StringBuffer sb;
			PrettyWriter<StringBuffer> writer(sb);
			writer.SetMaxDecimalPlaces(maxDecimalPlaces);

			source->Save(&writer);

			const char *src = sb.GetString();
			uint32_t srcSize = sb.GetSize();

			uint32_t bufferSize = LZ4_compressBound(srcSize);
			char* buffer = new char[bufferSize];

			uint32_t size = LZ4_compress_default(src, buffer, srcSize, bufferSize);
			if (size == 0)
			{
				FURYE << "Failed to compress json string!";
				return false;
			}
			else
			{
				FURYD << "Before: " << srcSize << " After: " << size;
			}

			uint32_t netSrcSize = htonl(srcSize);
			uint32_t netSize = htonl(size);

			stream.write((char*)&netSrcSize, sizeof(uint32_t));
			stream.write((char*)&netSize, sizeof(uint32_t));
			stream.write(buffer, size);
			stream.flush();
			stream.close();

			delete[] buffer;

			FURYD << filePath << " successfully serialized!";
			return true;
		}
		else
		{
			FURYE << "Path " << filePath << " not found!";
			return false;
		}
	}

	std::string FileUtil::SerializeToString(const std::shared_ptr<Serializable> &source, int maxDecimalPlaces)
	{
		using namespace rapidjson;
		StringBuffer sb;
		PrettyWriter<StringBuffer> writer(sb);
		writer.SetMaxDecimalPlaces(maxDecimalPlaces);
		source->Save(&writer);
		return std::string(sb.GetString(), sb.GetSize());
	}

	bool FileUtil::DeserializeFromString(const std::shared_ptr<Serializable> &target, const std::string &json)
	{
		using namespace rapidjson;
		Document dom;
		dom.Parse(json.c_str());
		if (dom.HasParseError())
		{
			FURYE << "FileUtil::DeserializeFromString: parse error " << dom.GetParseError();
			return false;
		}
		if (!target->Load(&dom))
		{
			FURYE << "FileUtil::DeserializeFromString: Load returned false";
			return false;
		}
		return true;
	}

}