#ifndef _FURY_FILEUTIL_H_
#define _FURY_FILEUTIL_H_

#include <string>
#include <vector>

#include "Singleton.h"
#include "EnumUtil.h"

#undef LoadString
#undef LoadImage

namespace fury
{
	class Pipeline;

	class Serializable;

	class FURY_API FileUtil : public Singleton<FileUtil>
	{
	private:

		std::string m_AbsPath;

	public:

		std::string GetAbsPath();

		std::string GetAbsPath(const std::string &source, bool toForwardSlash = false);

		bool FileExist(const std::string &path) const;

		// image, text file io

		bool LoadString(const std::string &path, std::string &output);

		bool LoadImage(const std::string &path, std::vector<unsigned char> &output, int &width, int &height, int &channels);

		// serializable obj io

		bool LoadFromFile(const std::shared_ptr<Serializable> &source, const std::string &filePath);

		bool SaveToFile(const std::shared_ptr<Serializable> &source, const std::string &filePath);
	};
}

#endif // _FURY_FILEUTIL_H_