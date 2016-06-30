#ifndef _FURY_FBXPARSER_H_
#define _FURY_FBXPARSER_H_

#include <unordered_map>

#include <fbxsdk.h>

#include "Singleton.h"
#include "Entity.h"
#include "Matrix4.h"

namespace fury
{
	class SceneNode;

	class Mesh;

	class Material;

	// assume int takes 16bits
	enum FbxImportFlags : unsigned int
	{
		UV 				= 0x0001, 
		NORMAL 			= 0x0002, 
		TANGENT 		= 0x0004, 
		SPECULAR_MAP 	= 0x0008, 
		NORMAL_MAP 		= 0x0010, 
		OPTIMIZE_MESH 	= 0x0020, 
		TRIANGULATE		= 0x0040, 
		IMP_ANIM		= 0x0080, 
		IMP_POS_ANIM	= 0x0100, 
		IMP_SCL_ANIM	= 0x0200, 
		OPTIMIZE_ANIM	= 0x0400, 
		BAKE_LAYERS		= 0x0800, 
		AUTO_PAIR_CLIP	= 0x1000
	};

	struct FbxImportOptions
	{
	public:

		unsigned int Flags = FbxImportFlags::UV | FbxImportFlags::NORMAL | FbxImportFlags::IMP_ANIM | 
			FbxImportFlags::IMP_POS_ANIM | FbxImportFlags::AUTO_PAIR_CLIP | FbxImportFlags::OPTIMIZE_ANIM | 
			FbxImportFlags::OPTIMIZE_MESH;

		float ScaleFactor = 1.0f;

		// 1 - 0
		float AnimCompressLevel = 0.5f;

		std::unordered_map<std::string, std::vector<std::string>> AnimLinkMap;
	};

	class FURY_API FbxParser : public Singleton<FbxParser>
	{
	protected:

		FbxScene* m_FbxScene = nullptr;

		FbxManager* m_FbxManager = nullptr;

		FbxImportOptions m_ImportOptions;

		FbxColor m_AmbientColor;

		std::string m_FbxFolder;

		std::unordered_map<std::string, std::pair<FbxMesh*, std::shared_ptr<Mesh>>> m_SkinnedMeshMap;

	public:

		typedef std::shared_ptr<FbxParser> Ptr;

		typedef std::unordered_map<std::string, std::vector<std::string>> AnimLinkMap;
		
		void LoadScene(const std::string &filePath, const std::shared_ptr<SceneNode> &rootNode, FbxImportOptions importOptions);

	protected:

		void LoadAnimations(const AnimLinkMap &animLinkMap);

		void LoadNode(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadMesh(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadMaterial(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		void LoadLight(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		std::shared_ptr<Material> CreatePhongMaterial(FbxSurfacePhong *fbxPhong);

		std::shared_ptr<Material> CreateLambertMaterial(FbxSurfaceLambert *fbxLambert);

		std::shared_ptr<Mesh> CreateMesh(const std::shared_ptr<SceneNode> &ntNode, FbxNode *fbxNode);

		bool CreateSkeleton(const std::shared_ptr<SceneNode> &ntNode, const std::shared_ptr<Mesh> &mesh, FbxMesh *fbxMesh);

		FbxAMatrix GetGeometryMatrix(FbxNode* fbxNode);

		Matrix4 FbxMatrixToFuryMatrix(const FbxAMatrix &fbxMatrix);

		void ApplyFbxAMatrixToNode(const std::shared_ptr<SceneNode> &ntNode, const FbxAMatrix &fbxMatrix, bool scale = true);
	};
}

#endif // _FURY_FBXPARSER_H_