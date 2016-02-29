#ifndef _FURY_FBXUTIL_H_
#define _FURY_FBXUTIL_H_

#include <fbxsdk.h>

#include "Singleton.h"
#include "Entity.h"

namespace fury
{
	class SceneNode;

	class Mesh;

	class Material;

	// assume int takes 16bits
	enum Options : unsigned int
	{
		UV 				= 0x0001, 
		NORMAL 			= 0x0002, 
		TANGENT 		= 0x0004, 
		DIFFUSE_MAP 	= 0x0008, 
		SPECULAR_MAP 	= 0x0010, 
		NORMAL_MAP 		= 0x0020, 
		OPTIMIZE_MESH 	= 0x0040, 
		DELETE_MESHDATA = 0x0080
	};

	class FURY_API FbxUtil : public Singleton<FbxUtil>
	{
	protected:

		float m_ScaleFactor = 1.0f;

		int m_Options = 0;

		std::string m_FbxFolder;

	public:

		typedef std::shared_ptr<FbxUtil> Ptr;
		
		// for models from blender, u may want to set scalefactor to 0.01.
		void LoadScene(const std::string &filePath, const std::shared_ptr<SceneNode> &rootNode, float scaleFactor = 0.01f, unsigned int options = 0xFFFF);

	protected:

		void LoadNode(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadMesh(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadMaterial(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadLight(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		std::shared_ptr<Mesh> CreateMesh(FbxNode *fbxNode);

		std::shared_ptr<Material> CreateMaterial(FbxSurfaceMaterial *fbxMaterial);
	};
}

#endif // _FURY_FBXUTIL_H_