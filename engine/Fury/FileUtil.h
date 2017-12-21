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
	class GLTFAccessor;

	class GLTFDom;

	class Mesh;

	class Pipeline;

	class Scene;

	class Serializable;

	enum GLTFImportFlags : unsigned int
	{
		OPTMZ_MESH = 0x0001, 
		GEN_NORMAL = 0x0002, 
		GEN_TANGENT = 0x0004, 
	};

	class FURY_API FileUtil final
	{
	private:

		static std::string m_AbsPath;

		static unsigned int* m_UIntBuffer;
		static int m_UIntBufferLength;

		static unsigned short* m_UShortBuffer;
		static int m_UShortBufferLength;

		static unsigned char* m_UCharBuffer;
		static int m_UCharBufferLength;

		static float* m_FloatBuffer;
		static int m_FloatBufferLength;

		static short* m_ShortBuffer;
		static int m_ShortBufferLength;

		static char* m_CharBuffer;
		static int m_CharBufferLength;

	public:

		static std::string GetAbsPath();

		static std::string GetAbsPath(const std::string &source, bool toForwardSlash = false);

		static bool FileExist(const std::string &path);

		// image, text file io

		static bool LoadString(const std::string &path, std::string &output);

		static bool LoadImage(const std::string &path, std::vector<unsigned char> &output, int &width, int &height, int &channels);

		// serializable obj io

		static bool LoadFile(const std::shared_ptr<Serializable> &source, const std::string &filePath);

		static bool SaveFile(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces = 5);

		static bool LoadCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath);

		static bool SaveCompressedFile(const std::shared_ptr<Serializable> &source, const std::string &filePath, int maxDecimalPlaces = 5);

		static bool LoadGLTFFile(const std::shared_ptr<Scene> &scene, const std::string &jsonPath, const unsigned int options = 0);

	private:

		static bool PrivateLoadGLTFFile(const std::shared_ptr<GLTFDom> &gltfDom, std::vector<std::shared_ptr<std::ifstream>> &buffers, 
			const std::shared_ptr<Scene> &scene, const std::string &workingDir, const unsigned int options);

		template<typename T>
		static int AccessBuffer(const std::shared_ptr<GLTFDom> &gltfDom, std::vector<std::shared_ptr<std::ifstream>> &buffers,
			const std::shared_ptr<GLTFAccessor> &accessor, std::vector<T> &output);

		template<typename T>
		static T* ResizeDataBuffer(T* buffer, int &oldLength, int newLength);

		static void DeleteDataBuffers();
	};
}

#endif // _FURY_FILEUTIL_H_