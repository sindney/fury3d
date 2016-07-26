#include <fstream>
#include <sstream>
#include <algorithm>

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

#include "Fury/Log.h"
#include "Fury/FileUtil.h"
#include "Fury/Serializable.h"

#undef far
#undef near

namespace fury
{
	using namespace std;

	std::string FileUtil::m_AbsPath = "";

	std::string FileUtil::GetAbsPath()
	{
#if defined(__APPLE__)
		if (m_AbsPath.size() == 0)
		{
			CFBundleRef mainBundle = CFBundleGetMainBundle();
			CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
			char path[512];
			if (!CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8*)path, 512))
			{
				FURYE << "Absolute Path Not Found!";
			}
			CFRelease(resourcesURL);
			m_AbsPath = std::string(path) + '/';
		}
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
		ifstream stream(path, ios::in);
		if (stream)
		{
			stream.seekg(0, ios::end);
			size_t size = (size_t)stream.tellg();
			stream.seekg(0, ios::beg);

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

	bool FileUtil::SaveCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces)
	{
		using namespace rapidjson;

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
}