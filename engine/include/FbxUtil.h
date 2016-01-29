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

	class FURY_API FbxUtil : public Singleton<FbxUtil>
	{
	protected:

		float m_ScaleFactor = 1.0f;

	public:

		typedef std::shared_ptr<FbxUtil> Ptr;
		
		// for models from blender, u may want to set scalefactor to 0.01.
		void LoadScene(const std::string &filePath, const std::shared_ptr<SceneNode> &rootNode, float scaleFactor = 0.01f);

	protected:

		void LoadNode(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadMesh(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadMaterial(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadLight(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		std::shared_ptr<Mesh> CreateMesh(FbxMesh *fbxMesh);

		std::shared_ptr<Material> CreateMaterial(FbxSurfaceMaterial *fbxMaterial);
	};
}

#endif // _FURY_FBXUTIL_H_