#include <fstream>
#include <sstream>
#include <algorithm>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP

#include <stb_image.h>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/PrettyWriter.h>
#include <rapidjson/stringbuffer.h>

#include "Log.h"
#include "FileUtil.h"
#include "Serializable.h"

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

	bool FileUtil::LoadFromFile(const Serializable::Ptr &source, const std::string &filePath)
	{
		using namespace rapidjson;

		std::ifstream stream(filePath);
		if (stream)
		{
			std::stringstream buffer;
			buffer << stream.rdbuf();

			Document dom;
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

	bool FileUtil::SaveToFile(const Serializable::Ptr &source, const std::string &filePath, int maxDecimalPlaces)
	{
		using namespace rapidjson;

		std::ofstream output(filePath);
		if (output)
		{
			StringBuffer sb;
			PrettyWriter<StringBuffer> writer(sb);
			writer.SetMaxDecimalPlaces(maxDecimalPlaces);

			source->Save(&writer);

			output << sb.GetString();
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
}