#include <vector>
#include <unordered_map>

#include "Angle.h"
#include "Debug.h"
#include "EntityUtil.h"
#include "FileUtil.h"
#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRender.h"
#include "MeshUtil.h"
#include "SceneNode.h"
#include "FbxUtil.h"
#include "Texture.h"
#include "Uniform.h"

namespace fury
{
	void FbxUtil::LoadScene(const std::string &filePath, const std::shared_ptr<SceneNode> &rootNode, float scaleFactor, unsigned int options)
	{
		FbxManager* sdkManager = FbxManager::Create();

		FbxIOSettings* ios = FbxIOSettings::Create(sdkManager, IOSROOT);
		sdkManager->SetIOSettings(ios);

		FbxImporter* importer = FbxImporter::Create(sdkManager, "");
		m_ScaleFactor = scaleFactor;
		m_Options = options;

		size_t pos = filePath.find_last_of("\\/");
		m_FbxFolder = (std::string::npos == pos) ? "" : filePath.substr(0, pos + 1);
		LOGD << "FbxFolder: " << m_FbxFolder;

		if (importer->Initialize(filePath.c_str(), -1, sdkManager->GetIOSettings()))
		{
			FbxScene* lScene = FbxScene::Create(sdkManager, "");

			importer->Import(lScene);

			importer->Destroy();

			FbxNode* lRootNode = lScene->GetRootNode();

			if (lRootNode)
			{
				for (int i = 0; i < lRootNode->GetChildCount(); i++)
					LoadNode(rootNode, lRootNode->GetChild(i));

				rootNode->Recompose();
			}
		}
		else
			ASSERT_MSG(false, "Error: " << importer->GetStatus().GetErrorString());

		// Destroy the SDK manager and all the other objects it was handling.
		sdkManager->Destroy();
	}

	void FbxUtil::LoadNode(const SceneNode::Ptr &ntNode, FbxNode* fbxNode)
	{
		SceneNode::Ptr childNode = SceneNode::Create(fbxNode->GetName());

		// copy transforms.
		FbxQuaternion fbxQ;
		fbxQ.ComposeSphericalXYZ(fbxNode->LclRotation.Get());
		FbxDouble3 fbxT = fbxNode->LclTranslation.Get();
		FbxDouble3 fbxS = fbxNode->LclScaling.Get();

		Vector4 furyT((float)fbxT.mData[0] * m_ScaleFactor, (float)fbxT.mData[1] * m_ScaleFactor, (float)fbxT.mData[2] * m_ScaleFactor, 1.0f);
		Vector4 furyS((float)fbxS.mData[0] * m_ScaleFactor, (float)fbxS.mData[1] * m_ScaleFactor, (float)fbxS.mData[2] * m_ScaleFactor, 1.0f);
		Quaternion furyR((float)fbxQ.mData[0], (float)fbxQ.mData[1], (float)fbxQ.mData[2], (float)fbxQ.mData[3]);

		childNode->SetLocalPosition(furyT);
		childNode->SetLocalRoattion(furyR);
		childNode->SetLocalScale(furyS);

		// add to scene graph
		ntNode->AddChild(childNode);

		// read Components
		FbxNodeAttribute* fbxNodeAttr = fbxNode->GetNodeAttribute();
		if (fbxNodeAttr != NULL)
		{
			FbxNodeAttribute::EType fbxNodeAttrType = fbxNodeAttr->GetAttributeType();
			if (fbxNodeAttrType == FbxNodeAttribute::eMesh)
			{
				LoadMesh(childNode, fbxNode);
			}
			else if (fbxNodeAttrType == FbxNodeAttribute::eLight)
			{
				LoadLight(childNode, fbxNode);
			}
		}

		// read child nodes.
		for (int i = 0; i < fbxNode->GetChildCount(); i++)
			LoadNode(ntNode, fbxNode->GetChild(i));
	}

	void FbxUtil::LoadMesh(const SceneNode::Ptr &ntNode, FbxNode* fbxNode)
	{
		FbxMesh* fbxMesh = static_cast<FbxMesh*>(fbxNode->GetNodeAttribute());

		// first, we test if ther's already a mesh asset exits with this name.
		Mesh::Ptr mesh = EntityUtil::Instance()->FindEntity<Mesh>(fbxMesh->GetName());
		
		if (mesh == nullptr)
		{
			// if not, we read the mesh data.
			mesh = CreateMesh(fbxNode);
			EntityUtil::Instance()->AddEntity(mesh);
		}

		// attach mesh component to node.
		ntNode->AddComponent(MeshRender::Create(nullptr, mesh));

		LoadMaterial(ntNode, fbxNode);
	}

	void FbxUtil::LoadMaterial(const SceneNode::Ptr &ntNode, FbxNode *fbxNode)
	{
		int materialCount = fbxNode->GetSrcObjectCount<FbxSurfaceMaterial>();
		for (int i = 0; i < materialCount; i++)
		{
			FbxSurfaceMaterial *fbxMaterial = fbxNode->GetSrcObject<FbxSurfaceMaterial>(i);

			// only supports phong material
			if (fbxMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
			{
				Material::Ptr material = EntityUtil::Instance()->FindEntity<Material>(fbxMaterial->GetName());

				if (material == nullptr)
				{
					material = CreateMaterial(fbxMaterial);
					EntityUtil::Instance()->AddEntity(material);
				}

				if (auto ptr = ntNode->GetComponent<MeshRender>())
					ptr->SetMaterial(material, i);
			}
			else
			{
				LOGW << "Material Type not supported!";
			}
		}
	}

	void FbxUtil::LoadLight(const SceneNode::Ptr &ntNode, FbxNode *fbxNode)
	{
		FbxLight *fbxLight = static_cast<FbxLight*>(fbxNode->GetNodeAttribute());

		LightType type;

		switch (fbxLight->LightType.Get())
		{
		case FbxLight::EType::eDirectional:
			type = LightType::DIRECTIONAL;
			break;
		case FbxLight::EType::ePoint:
			type = LightType::POINT;
			break;
		case FbxLight::EType::eSpot:
			type = LightType::SPOT;
			break;
		default:
			LOGW << "Unsupported light type!";
			return;
		}

		Light::Ptr light = Light::Create();

		FbxDouble3 color = fbxLight->Color.Get();
		light->SetType(type);
		light->SetColor(Color((float)color.mData[0], (float)color.mData[1], (float)color.mData[2]));
		light->SetIntensity((float)fbxLight->Intensity.Get() * m_ScaleFactor);
		light->SetInnerAngle(Angle::DegToRad * (float)fbxLight->InnerAngle.Get());
		light->SetOutterAngle(Angle::DegToRad * (float)fbxLight->OuterAngle.Get());
		light->SetFalloff((float)fbxLight->FarAttenuationEnd.Get() * m_ScaleFactor);
		light->SetRadius((float)fbxLight->DecayStart.Get() * m_ScaleFactor);
		light->CalculateAABB();
		LOGD << fbxLight->GetName();

		ntNode->AddComponent(light);
	}

	std::shared_ptr<Mesh> FbxUtil::CreateMesh(FbxNode *fbxNode)
	{
		FbxMesh* fbxMesh = static_cast<FbxMesh*>(fbxNode->GetNodeAttribute());
		FbxLayerElementMaterial* layerMaterial = fbxMesh->GetLayer(0)->GetMaterials();

		Mesh::Ptr mesh = Mesh::Create(fbxMesh->GetName());

		// read physical data.

		int polygonCount = fbxMesh->GetPolygonCount();
		int indicesCount = polygonCount * 3;

		mesh->Positions.Data.reserve(indicesCount * 3);
		mesh->Indices.Data.reserve(indicesCount);

		if ((m_Options & Options::UV) && fbxMesh->GetElementUVCount() > 0)
			mesh->UVs.Data.reserve(indicesCount * 2);

		if ((m_Options & Options::NORMAL) && fbxMesh->GetElementNormalCount() > 0)
			mesh->Normals.Data.reserve(indicesCount * 3);

		if ((m_Options & Options::TANGENT) && fbxMesh->GetElementTangent() > 0)
			mesh->Tangents.Data.reserve(indicesCount * 3);

		int normalCounter = 0, uvCounter = 0, tangentCounter = 0;

		for (int i = 0; i < polygonCount; i++)
		{
			for (int j = 0; j < 3; j++)
			{
				int ctrPtrIndex = fbxMesh->GetPolygonVertex(i, j);

				auto position = fbxMesh->GetControlPointAt(ctrPtrIndex);
				mesh->Positions.Data.push_back((float)position.mData[0]);
				mesh->Positions.Data.push_back((float)position.mData[1]);
				mesh->Positions.Data.push_back((float)position.mData[2]);

				// uv
				if ((m_Options & Options::UV) && fbxMesh->GetElementUVCount() > 0)
				{
					int uvIndex = 0;
					FbxGeometryElementUV* vertexUV = fbxMesh->GetElementUV();

					if (vertexUV->GetMappingMode() == FbxGeometryElement::eByControlPoint)
					{
						if (vertexUV->GetReferenceMode() == FbxGeometryElement::eDirect)
							uvIndex = ctrPtrIndex;
						else if (vertexUV->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							uvIndex = vertexUV->GetIndexArray().GetAt(ctrPtrIndex);
						else
							ASSERT_MSG(false, "Error: Invalid Reference Mode!");
					}
					else if (vertexUV->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
					{
						if (vertexUV->GetReferenceMode() == FbxGeometryElement::eDirect)
							uvIndex = uvCounter;
						else if (vertexUV->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							uvIndex = vertexUV->GetIndexArray().GetAt(uvCounter);
						else
							ASSERT_MSG(false, "Error: Invalid Reference Mode!");

						uvCounter++;
					}

					auto uv = vertexUV->GetDirectArray().GetAt(uvIndex);
					mesh->UVs.Data.push_back((float)uv.mData[0]);
					mesh->UVs.Data.push_back(1.0f - (float)uv.mData[1]);
				}

				// normal
				if ((m_Options & Options::NORMAL) && fbxMesh->GetElementNormalCount() > 0)
				{
					int normalIndex = 0;
					FbxGeometryElementNormal* vertexNormal = fbxMesh->GetElementNormal();

					if (vertexNormal->GetMappingMode() == FbxGeometryElement::eByControlPoint)
					{
						if (vertexNormal->GetReferenceMode() == FbxGeometryElement::eDirect)
							normalIndex = ctrPtrIndex;
						else if (vertexNormal->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							normalIndex = vertexNormal->GetIndexArray().GetAt(ctrPtrIndex);
						else
							ASSERT_MSG(false, "Error: Invalid Reference Mode!");
					}
					else if (vertexNormal->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
					{
						if (vertexNormal->GetReferenceMode() == FbxGeometryElement::eDirect)
							normalIndex = normalCounter;
						else if (vertexNormal->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							normalIndex = vertexNormal->GetIndexArray().GetAt(normalCounter);
						else
							ASSERT_MSG(false, "Error: Invalid Reference Mode!");

						normalCounter++;
					}

					auto normal = vertexNormal->GetDirectArray().GetAt(normalIndex);
					mesh->Normals.Data.push_back((float)normal.mData[0]);
					mesh->Normals.Data.push_back((float)normal.mData[1]);
					mesh->Normals.Data.push_back((float)normal.mData[2]);
				}

				// tangent
				if ((m_Options & Options::TANGENT) && fbxMesh->GetElementNormalCount() > 0)
				{
					int tangentIndex = 0;
					FbxGeometryElementTangent* vertexTangent = fbxMesh->GetElementTangent();

					if (vertexTangent->GetMappingMode() == FbxGeometryElement::eByControlPoint)
					{
						if (vertexTangent->GetReferenceMode() == FbxGeometryElement::eDirect)
							tangentIndex = ctrPtrIndex;
						else if (vertexTangent->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							tangentIndex = vertexTangent->GetIndexArray().GetAt(ctrPtrIndex);
						else
							ASSERT_MSG(false, "Error: Invalid Reference Mode!");
					}
					else if (vertexTangent->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
					{
						if (vertexTangent->GetReferenceMode() == FbxGeometryElement::eDirect)
							tangentIndex = tangentCounter;
						else if (vertexTangent->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							tangentIndex = vertexTangent->GetIndexArray().GetAt(tangentCounter);
						else
							ASSERT_MSG(false, "Error: Invalid Reference Mode!");

						tangentCounter++;
					}

					auto tangent = vertexTangent->GetDirectArray().GetAt(tangentIndex);
					mesh->Tangents.Data.push_back((float)tangent.mData[0]);
					mesh->Tangents.Data.push_back((float)tangent.mData[1]);
					mesh->Tangents.Data.push_back((float)tangent.mData[2]);
				}

				mesh->Indices.Data.push_back(i * 3 + j);
			}
		}

		LOGD << mesh->GetName() << " [vtx: " << mesh->Positions.Data.size() / 3 << " tris: " << mesh->Indices.Data.size() / 3 << "]";

		// read subMeshes if theres any
		unsigned int materialCount = fbxNode->GetSrcObjectCount<FbxSurfaceMaterial>();
		if (materialCount > 0 && layerMaterial->GetMappingMode() == FbxLayerElement::eByPolygon)
		{
			if (layerMaterial->GetReferenceMode() == FbxLayerElement::eIndex ||
				layerMaterial->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
			{
				unsigned int materialIndexCount = layerMaterial->GetIndexArray().GetCount();
				std::unordered_map<unsigned int, SubMesh::Ptr> subMeshes;

				for (unsigned int i = 0; i < materialIndexCount; i++)
				{
					unsigned int matID = layerMaterial->GetIndexArray().GetAt(i);
					if (matID == -1 || matID >= materialCount) 
						matID = 0;

					auto it = subMeshes.find(matID);
					if (it == subMeshes.end())
						it = subMeshes.emplace(std::make_pair(matID, SubMesh::Create())).first;

					auto subMesh = it->second;
					unsigned int index = i * 3;

					subMesh->Indices.Data.push_back(index);
					subMesh->Indices.Data.push_back(index + 1);
					subMesh->Indices.Data.push_back(index + 2);
				}

				for (unsigned int i = 0; i < subMeshes.size(); i++)
					mesh->AddSubMesh(subMeshes[i]);

				LOGD << "Loaded " << subMeshes.size() << " subMeshes.";
			}
			else
			{
				LOGW << "Material referenceMode not supported!";
			}
		}

		mesh->CalculateAABB();

		// optimize mesh data, aka find unique vertices.
		if (m_Options & Options::OPTIMIZE_MESH)
			MeshUtil::Instance()->OptimizeMesh(mesh);

		// delete mesh data after opengl buffer creation.
		// this will save some memory space, if you won't change mesh's vertex data.
		if (m_Options & Options::DELETE_MESHDATA)
		{
			mesh->UpdateBuffer();
		}

		return mesh;
	}

	std::shared_ptr<Material> FbxUtil::CreateMaterial(FbxSurfaceMaterial *fbxMaterial)
	{
		// a,d,s, Intensity(Factor), Shininess, Reflectivity, Transparency
		//GetImplementation(material, FBXSDK_IMP)
		
		FbxSurfacePhong *fbxPhong = static_cast<FbxSurfacePhong*>(fbxMaterial);
		Material::Ptr material = Material::Create(fbxPhong->GetName());

		// read uniforms
		material->SetUniform(Material::SHININESS, Uniform1f::Create({ (float)fbxPhong->Shininess }));
		material->SetUniform(Material::AMBIENT_FACTOR, Uniform1f::Create({ (float)fbxPhong->AmbientFactor }));
		material->SetUniform(Material::DIFFUSE_FACTOR, Uniform1f::Create({ (float)fbxPhong->DiffuseFactor }));
		material->SetUniform(Material::SPECULAR_FACTOR, Uniform1f::Create({ (float)fbxPhong->SpecularFactor }));

		auto GetUniform3f = [](FbxPropertyT<FbxDouble3> &data) -> UniformBase::Ptr
		{
			return Uniform3f::Create({ (float)data.Get()[0], (float)data.Get()[1], (float)data.Get()[2] });
		};

		// read colors
		material->SetUniform(Material::AMBIENT_COLOR, GetUniform3f(fbxPhong->Ambient));
		material->SetUniform(Material::DIFFUSE_COLOR, GetUniform3f(fbxPhong->Diffuse));
		material->SetUniform(Material::SPECULAR_COLOR, GetUniform3f(fbxPhong->Specular));

		material->SetUniform(Material::MATERIAL_ID, Uniform1ui::Create({ material->GetID() }));

		auto GetTexture = [&](FbxPropertyT<FbxDouble3> prop) -> Texture::Ptr
		{
			if (prop.IsValid() && prop.GetSrcObjectCount<FbxTexture>() > 0)
			{
				FbxTexture *fbxTexture = prop.GetSrcObject<FbxTexture>(0);

				FbxFileTexture *fileTexture = FbxCast<FbxFileTexture>(fbxTexture);
				if (fileTexture)
				{
					bool mipMap = (bool)fileTexture->UseMipMap;
					std::string filePath = m_FbxFolder + fileTexture->GetRelativeFileName();

					auto texture = Texture::Create(fileTexture->GetName());
					texture->SetFilterMode(mipMap ? FilterMode::LINEAR_MIPMAP_LINEAR : FilterMode::LINEAR);
					texture->CreateFromImage(filePath, mipMap);

					return texture;
				}
				else
				{
					LOGW << "FbxProceduralTexture not supported!";
				}
			}

			return nullptr;
		};

		// read tetxures.
		if(m_Options & Options::DIFFUSE_MAP)
			material->SetTexture(Material::DIFFUSE_TEXTURE, GetTexture(fbxPhong->Diffuse));
		if(m_Options & Options::SPECULAR_MAP)
			material->SetTexture(Material::SPECULAR_TEXTURE, GetTexture(fbxPhong->Specular));
		if(m_Options & Options::NORMAL_MAP)
			material->SetTexture(Material::NORMAL_TEXTURE, GetTexture(fbxPhong->NormalMap));

		LOGD << fbxMaterial->GetName();

		return material;
	}
}