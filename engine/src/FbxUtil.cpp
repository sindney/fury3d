#include <vector>
#include <unordered_map>
#include <stack>
#include <functional>

#include "AnimationClip.h"
#include "AnimationUtil.h"
#include "Angle.h"
#include "Debug.h"
#include "EntityUtil.h"
#include "FileUtil.h"
#include "Joint.h"
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
	void FbxUtil::LoadScene(const std::string &filePath, const std::shared_ptr<SceneNode> &rootNode, FbxImportOptions importOptions)
	{
		m_ImportOptions = importOptions;
		m_FbxManager = FbxManager::Create();

		auto ios = FbxIOSettings::Create(m_FbxManager, IOSROOT);
		if (m_ImportOptions.Flags & IMP_ANIM) ios->SetBoolProp(IMP_FBX_ANIMATION, true);
		m_FbxManager->SetIOSettings(ios);

		auto importer = FbxImporter::Create(m_FbxManager, "");

		size_t pos = filePath.find_last_of("\\/");
		m_FbxFolder = (std::string::npos == pos) ? "" : filePath.substr(0, pos + 1);
		LOGD << "FbxFolder: " << m_FbxFolder;

		if (importer->Initialize(filePath.c_str(), -1, m_FbxManager->GetIOSettings()))
		{
			m_FbxScene = FbxScene::Create(m_FbxManager, "");

			bool result = importer->Import(m_FbxScene);
			importer->Destroy();

			if (result)
			{
				auto &globalSettings = m_FbxScene->GetGlobalSettings();
				m_AmbientColor = globalSettings.GetAmbientColor();
				
				FbxAxisSystem sceneAxisSystem = m_FbxScene->GetGlobalSettings().GetAxisSystem();
				FbxAxisSystem furyAxisSystem = FbxAxisSystem(FbxAxisSystem::EUpVector::eYAxis, FbxAxisSystem::EFrontVector::eParityOdd, FbxAxisSystem::ECoordSystem::eRightHanded);
				if (sceneAxisSystem != furyAxisSystem)
				{
					furyAxisSystem.ConvertScene(m_FbxScene);
					LOGD << "Converting axis system.";
				}
				
				if (m_ImportOptions.Flags & FbxImportFlags::TRIANGULATE)
				{
					FbxGeometryConverter geomConverter(m_FbxManager);
					geomConverter.Triangulate(m_FbxScene, true);
					LOGD << "Scene Triangulated.";
				}

				if (m_ImportOptions.Flags & FbxImportFlags::IMP_ANIM)
				{
					// bake animation layers
					int animStackCount = m_FbxScene->GetSrcObjectCount<FbxAnimStack>();
					for (int i = 0; i < animStackCount; i++)
					{
						auto stack = m_FbxScene->GetSrcObject<FbxAnimStack>(i);
						auto take = m_FbxScene->GetTakeInfo(stack->GetName());
						auto start = take->mLocalTimeSpan.GetStart();
						auto stop = take->mLocalTimeSpan.GetStop();

						if (m_ImportOptions.Flags & FbxImportFlags::BAKE_LAYERS)
							stack->BakeLayers(m_FbxScene->GetAnimationEvaluator(), start, stop, FbxTime::eFrames24);
					}
					LOGD << "Found " << animStackCount << " animation stacks.";
				}

				auto fbxRootNode = m_FbxScene->GetRootNode();
				if (fbxRootNode)
				{
					for (int i = 0; i < fbxRootNode->GetChildCount(); i++)
						LoadNode(rootNode, fbxRootNode->GetChild(i));

					rootNode->Recompose();
				}

				// load animation data
				if (m_ImportOptions.Flags & FbxImportFlags::IMP_ANIM)
				{
					// find mesh - animation clip pair automatically
					// this only works when animStack's name is of this format MeshName|Walk
					if (m_ImportOptions.Flags & FbxImportFlags::AUTO_PAIR_CLIP)
					{
						auto &linkMap = m_ImportOptions.AnimLinkMap;
						int animStackCount = m_FbxScene->GetSrcObjectCount<FbxAnimStack>();
						for (int i = 0; i < animStackCount; i++)
						{
							auto stack = m_FbxScene->GetSrcObject<FbxAnimStack>(i);
							std::string stackName = stack->GetName();

							auto index = stackName.find_first_of('|');
							if (index == std::string::npos)
								continue;

							auto meshName = stackName.substr(0, index);
							if (auto matchMesh = EntityUtil::Instance()->Get<Mesh>(meshName))
							{
								auto it = linkMap.find(meshName);
								LOGD << "Found Clip " << stackName << " for " << meshName;
								if (it == linkMap.end())
								{
									auto stackNames = { stackName };
									linkMap.emplace(meshName, stackNames);
								}
								else
								{
									bool found = false;
									for (auto name : it->second)
										found = name == stackName;
									
									if (!found)
										it->second.push_back(stackName);
								}
							}
						}
					}

					LoadAnimations(m_ImportOptions.AnimLinkMap);
				}
			}
			else
			{
				ASSERT_MSG(false, "Error: " << importer->GetStatus().GetErrorString());
			}

			m_SkinnedMeshMap.clear();

			m_FbxScene->Destroy();
			m_FbxScene = nullptr;
		}
		else
		{
			ASSERT_MSG(false, "Error: " << importer->GetStatus().GetErrorString());
		}

		// Destroy the SDK manager and all the other objects it was handling.
		m_FbxManager->Destroy();
		m_FbxManager = nullptr;
	}

	void FbxUtil::LoadAnimations(const AnimLinkMap &animLinkMap)
	{
		auto GetAnimStackByName = [&](const std::string &name) -> FbxAnimStack*
		{
			if (auto takeInfo = m_FbxScene->GetTakeInfo(name.c_str()))
			{
				int stackCount = m_FbxScene->GetSrcObjectCount<FbxAnimStack>();
				for (int i = 0; i < stackCount; i++)
				{
					auto stack = m_FbxScene->GetSrcObject<FbxAnimStack>(i);
					if (stack->GetName() == name)
					{
						return stack;
					}
				}
			}

			return nullptr;
		};

		for (auto pair : animLinkMap)
		{
			auto it = m_SkinnedMeshMap.find(pair.first);
			if (it == m_SkinnedMeshMap.end())
			{
				LOGW << "Skinned Mesh " << pair.first << " not found!";
				continue;
			}

			std::vector<FbxAnimStack*> animStacks;
			for (unsigned int i = 0; i < pair.second.size(); i++)
			{
				auto name = pair.second[i];
				if (auto animStack = GetAnimStackByName(name))
				{
					animStacks.push_back(animStack);
				}
				else
				{
					LOGW << "Animation Stack " << name << " not found!";
				}
			}

			auto fbxMesh = it->second.first;
			auto mesh = it->second.second;

			int deformerCount = fbxMesh->GetDeformerCount(FbxDeformer::eSkin);
			if (deformerCount == 0)
				continue;

			if (deformerCount != 1)
			{
				LOGW << "Don't support multiple deformer on one geometry.";
				continue;
			}

			FbxSkin *fbxSkin = (FbxSkin*)fbxMesh->GetDeformer(0, FbxDeformer::eSkin);
			if (fbxSkin == nullptr)
				continue;

			int clusterCount = fbxSkin->GetClusterCount();
			if (clusterCount < 1)
				continue;

			auto Vector4ToKeyFrame = [&](unsigned int tick, const FbxVector4 &fbxVector) -> KeyFrame
			{
				return KeyFrame(tick, (float)fbxVector.mData[0], (float)fbxVector.mData[1], (float)fbxVector.mData[2]);
			};

			auto EulerToQuat = [&](const FbxDouble3 &fbxDouble) -> Quaternion
			{
				return Angle::EulerRadToQuat(Vector4((float)fbxDouble[1], (float)fbxDouble[0], (float)fbxDouble[2]) * Angle::DegToRad);
			};

			auto impPosAnim = m_ImportOptions.Flags & FbxImportFlags::IMP_POS_ANIM;
			auto impSclAnim = m_ImportOptions.Flags & FbxImportFlags::IMP_SCL_ANIM;

			for (auto animStack : animStacks)
			{
				auto clip = AnimationClip::Create(animStack->GetName(), 24);

				// set as current
				m_FbxScene->SetCurrentAnimationStack(animStack);

				LOGD << "Reading animStack " << animStack->GetName() << " for " << mesh->GetName();

				for (int i = 0; i < clusterCount; i++)
				{
					auto cluster = fbxSkin->GetCluster(i);
					auto link = cluster->GetLink();
					auto take = m_FbxScene->GetTakeInfo(animStack->GetName());
					auto start = take->mLocalTimeSpan.GetStart();
					auto stop = take->mLocalTimeSpan.GetStop();
					//auto layer = animStack->GetMember<FbxAnimLayer>();
					//auto curveRot = link->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_ROTATION);
					
					auto channel = clip->AddChannel(link->GetName());
					
					unsigned int tick = 0;
					for (FbxLongLong i = start.GetFrameCount(FbxTime::eFrames24); i <= stop.GetFrameCount(FbxTime::eFrames24); ++i)
					{
						FbxTime curTime;
						curTime.SetFrame(i, FbxTime::eFrames24);

						auto curRotation = link->EvaluateLocalRotation(curTime) * (double)Angle::DegToRad;
						channel->rotations.push_back(KeyFrame(tick, (float)curRotation[1], (float)curRotation[0], (float)curRotation[2]));

						if (impPosAnim)
						{
							auto curPosition = link->EvaluateLocalTranslation(curTime);
							channel->positions.push_back(Vector4ToKeyFrame(tick, curPosition));
						}

						if (impSclAnim)
						{
							auto curScale = link->EvaluateLocalScaling(curTime);
							channel->scalings.push_back(Vector4ToKeyFrame(tick, curScale));
						}

						tick++;
					}
				}

				if (m_ImportOptions.Flags & FbxImportFlags::OPTIMIZE_ANIM)
					AnimationUtil::Instance()->OptimizeAnimClip(clip, 0.5f);

				clip->CalculateDuration();
				EntityUtil::Instance()->Add(clip);
			}
		}
	}

	void FbxUtil::LoadNode(const SceneNode::Ptr &ntNode, FbxNode* fbxNode)
	{
		SceneNode::Ptr childNode = SceneNode::Create(fbxNode->GetName());
		ApplyFbxAMatrixToNode(childNode, fbxNode->EvaluateLocalTransform());

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
		Mesh::Ptr mesh = EntityUtil::Instance()->Get<Mesh>(fbxMesh->GetName());

		if (mesh == nullptr)
		{
			// if not, we read the mesh data.
			mesh = CreateMesh(ntNode, fbxNode);
			EntityUtil::Instance()->Add(mesh);
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

			// only supports phong / lambert material
			bool isPhong = fbxMaterial->GetClassId().Is(FbxSurfacePhong::ClassId);
			bool isLambert = fbxMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId);

			if (isPhong || isLambert)
			{
				Material::Ptr material = EntityUtil::Instance()->Get<Material>(fbxMaterial->GetName());

				if (material == nullptr)
				{
					material = isPhong ? CreatePhongMaterial((FbxSurfacePhong*)fbxMaterial) :
						CreateLambertMaterial((FbxSurfaceLambert*)fbxMaterial);
					EntityUtil::Instance()->Add(material);
				}

				if (auto ptr = ntNode->GetComponent<MeshRender>())
					ptr->SetMaterial(material, i);
			}
			else
			{
				LOGW << "Material type not supported!";
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
		light->SetIntensity((float)fbxLight->Intensity.Get() * m_ImportOptions.ScaleFactor);
		light->SetInnerAngle(Angle::DegToRad * (float)fbxLight->InnerAngle.Get());
		light->SetOutterAngle(Angle::DegToRad * (float)fbxLight->OuterAngle.Get());
		light->SetFalloff((float)fbxLight->FarAttenuationEnd.Get() * m_ImportOptions.ScaleFactor);
		light->SetRadius((float)fbxLight->DecayStart.Get() * m_ImportOptions.ScaleFactor);
		light->CalculateAABB();
		LOGD << fbxLight->GetName();

		ntNode->AddComponent(light);
	}

	std::shared_ptr<Material> FbxUtil::CreatePhongMaterial(FbxSurfacePhong *fbxPhong)
	{
		// a,d,s, Intensity(Factor), Shininess, Reflectivity, Transparency
		//GetImplementation(material, FBXSDK_IMP)

		Material::Ptr material = Material::Create(fbxPhong->GetName());

		// read uniforms
		material->SetUniform(Material::SHININESS, Uniform1f::Create({ (float)fbxPhong->Shininess }));
		material->SetUniform(Material::AMBIENT_FACTOR, Uniform1f::Create({ (float)fbxPhong->AmbientFactor }));
		material->SetUniform(Material::DIFFUSE_FACTOR, Uniform1f::Create({ (float)fbxPhong->DiffuseFactor }));
		material->SetUniform(Material::SPECULAR_FACTOR, Uniform1f::Create({ (float)fbxPhong->SpecularFactor }));
		material->SetUniform(Material::EMISSIVE_FACTOR, Uniform1f::Create({ (float)fbxPhong->EmissiveFactor }));

		auto GetUniform3f = [](FbxPropertyT<FbxDouble3> &data) -> UniformBase::Ptr
		{
			return Uniform3f::Create({ (float)data.Get()[0], (float)data.Get()[1], (float)data.Get()[2] });
		};

		// read colors
		material->SetUniform(Material::AMBIENT_COLOR, Uniform3f::Create({
			(float)m_AmbientColor.mRed * (float)fbxPhong->Ambient.Get()[0],
			(float)m_AmbientColor.mGreen * (float)fbxPhong->Ambient.Get()[1],
			(float)m_AmbientColor.mBlue * (float)fbxPhong->Ambient.Get()[2]
		}));
		material->SetUniform(Material::DIFFUSE_COLOR, GetUniform3f(fbxPhong->Diffuse));
		material->SetUniform(Material::SPECULAR_COLOR, GetUniform3f(fbxPhong->Specular));
		material->SetUniform(Material::EMISSIVE_COLOR, GetUniform3f(fbxPhong->Emissive));

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
		material->SetTexture(Material::DIFFUSE_TEXTURE, GetTexture(fbxPhong->Diffuse));
		if (m_ImportOptions.Flags & FbxImportFlags::SPECULAR_MAP)
			material->SetTexture(Material::SPECULAR_TEXTURE, GetTexture(fbxPhong->Specular));
		if (m_ImportOptions.Flags & FbxImportFlags::NORMAL_MAP)
			material->SetTexture(Material::NORMAL_TEXTURE, GetTexture(fbxPhong->NormalMap));

		LOGD << fbxPhong->GetName();

		return material;
	}

	std::shared_ptr<Material> FbxUtil::CreateLambertMaterial(FbxSurfaceLambert *fbxLambert)
	{
		Material::Ptr material = Material::Create(fbxLambert->GetName());

		// read uniforms
		material->SetUniform(Material::AMBIENT_FACTOR, Uniform1f::Create({ (float)fbxLambert->AmbientFactor }));
		material->SetUniform(Material::DIFFUSE_FACTOR, Uniform1f::Create({ (float)fbxLambert->DiffuseFactor }));
		material->SetUniform(Material::EMISSIVE_FACTOR, Uniform1f::Create({ (float)fbxLambert->EmissiveFactor }));

		auto GetUniform3f = [](FbxPropertyT<FbxDouble3> &data) -> UniformBase::Ptr
		{
			return Uniform3f::Create({ (float)data.Get()[0], (float)data.Get()[1], (float)data.Get()[2] });
		};

		// read colors
		material->SetUniform(Material::AMBIENT_COLOR, Uniform3f::Create({
			(float)m_AmbientColor.mRed * (float)fbxLambert->Ambient.Get()[0],
			(float)m_AmbientColor.mGreen * (float)fbxLambert->Ambient.Get()[1],
			(float)m_AmbientColor.mBlue * (float)fbxLambert->Ambient.Get()[2]
		}));
		material->SetUniform(Material::DIFFUSE_COLOR, GetUniform3f(fbxLambert->Diffuse));
		material->SetUniform(Material::EMISSIVE_COLOR, GetUniform3f(fbxLambert->Emissive));

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
		material->SetTexture(Material::DIFFUSE_TEXTURE, GetTexture(fbxLambert->Diffuse));
		if (m_ImportOptions.Flags & FbxImportFlags::NORMAL_MAP)
			material->SetTexture(Material::NORMAL_TEXTURE, GetTexture(fbxLambert->NormalMap));

		LOGD << fbxLambert->GetName();

		return material;
	}

	std::shared_ptr<Mesh> FbxUtil::CreateMesh(const SceneNode::Ptr &ntNode, FbxNode *fbxNode)
	{
		FbxMesh* fbxMesh = static_cast<FbxMesh*>(fbxNode->GetNodeAttribute());
		FbxLayerElementMaterial* layerMaterial = fbxMesh->GetLayer(0)->GetMaterials();

		Mesh::Ptr mesh = Mesh::Create(fbxMesh->GetName());

		// read physical data.

		int polygonCount = fbxMesh->GetPolygonCount();
		int indicesCount = polygonCount * 3;

		mesh->Positions.Data.reserve(indicesCount * 3);
		mesh->Indices.Data.reserve(indicesCount);

		bool hasSkeleton = fbxMesh->GetDeformerCount(FbxDeformer::eSkin) != 0;
		std::vector<std::pair<int, std::pair<std::vector<int>, std::vector<float>>>> ctrPointIndices;

		if (hasSkeleton)
		{
			ctrPointIndices.resize(fbxMesh->GetControlPointsCount());

			FbxSkin *fbxSkin = (FbxSkin*)fbxMesh->GetDeformer(0, FbxDeformer::eSkin);
			int clusterCount = fbxSkin->GetClusterCount();

			for (int i = 0; i < clusterCount; i++)
			{
				auto cluster = fbxSkin->GetCluster(i);
				auto link = cluster->GetLink();

				int ctrPtnIndicesCount = cluster->GetControlPointIndicesCount();
				int* ctrPtnIndices = cluster->GetControlPointIndices();
				double* ctrPtnWeights = cluster->GetControlPointWeights();

				for (int j = 0; j < ctrPtnIndicesCount; j++)
				{
					int ctrPtnIndex = ctrPtnIndices[j];
					float ctrPtnWeight = (float)ctrPtnWeights[j];
					auto &pair = ctrPointIndices[ctrPtnIndex];
					pair.first++;

					if (pair.first > 4)
					{
						LOGW << link->GetName() << "has more than 4 vertex bindings!";
						continue;
					}

					pair.second.first.push_back(i);
					pair.second.second.push_back(ctrPtnWeight);
				}
			}

			mesh->IDs.Data.resize(indicesCount * 4);
			mesh->Weights.Data.resize(indicesCount * 3);
		}

		if ((m_ImportOptions.Flags & FbxImportFlags::UV) && fbxMesh->GetElementUVCount() > 0)
			mesh->UVs.Data.reserve(indicesCount * 2);

		if ((m_ImportOptions.Flags & FbxImportFlags::NORMAL) && fbxMesh->GetElementNormalCount() > 0)
			mesh->Normals.Data.reserve(indicesCount * 3);

		if ((m_ImportOptions.Flags & FbxImportFlags::TANGENT) && fbxMesh->GetElementTangent() > 0)
			mesh->Tangents.Data.reserve(indicesCount * 3);

		int normalCounter = 0, uvCounter = 0, tangentCounter = 0;

		for (int i = 0; i < polygonCount; i++)
		{
			if (fbxMesh->GetPolygonSize(i) > 3)
				ASSERT_MSG(false, "Error: Triangulate " << mesh->GetName() << " first!");

			for (int j = 0; j < 3; j++)
			{
				int ctrPtnIndex = fbxMesh->GetPolygonVertex(i, j);

				auto position = fbxMesh->GetControlPointAt(ctrPtnIndex);
				mesh->Positions.Data.push_back((float)position.mData[0]);
				mesh->Positions.Data.push_back((float)position.mData[1]);
				mesh->Positions.Data.push_back((float)position.mData[2]);

				// uv
				if ((m_ImportOptions.Flags & FbxImportFlags::UV) && fbxMesh->GetElementUVCount() > 0)
				{
					int uvIndex = 0;
					FbxGeometryElementUV* vertexUV = fbxMesh->GetElementUV();

					if (vertexUV->GetMappingMode() == FbxGeometryElement::eByControlPoint)
					{
						if (vertexUV->GetReferenceMode() == FbxGeometryElement::eDirect)
							uvIndex = ctrPtnIndex;
						else if (vertexUV->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							uvIndex = vertexUV->GetIndexArray().GetAt(ctrPtnIndex);
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
				if ((m_ImportOptions.Flags & FbxImportFlags::NORMAL) && fbxMesh->GetElementNormalCount() > 0)
				{
					int normalIndex = 0;
					FbxGeometryElementNormal* vertexNormal = fbxMesh->GetElementNormal();

					if (vertexNormal->GetMappingMode() == FbxGeometryElement::eByControlPoint)
					{
						if (vertexNormal->GetReferenceMode() == FbxGeometryElement::eDirect)
							normalIndex = ctrPtnIndex;
						else if (vertexNormal->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							normalIndex = vertexNormal->GetIndexArray().GetAt(ctrPtnIndex);
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
				if ((m_ImportOptions.Flags & FbxImportFlags::TANGENT) && fbxMesh->GetElementNormalCount() > 0)
				{
					int tangentIndex = 0;
					FbxGeometryElementTangent* vertexTangent = fbxMesh->GetElementTangent();

					if (vertexTangent->GetMappingMode() == FbxGeometryElement::eByControlPoint)
					{
						if (vertexTangent->GetReferenceMode() == FbxGeometryElement::eDirect)
							tangentIndex = ctrPtnIndex;
						else if (vertexTangent->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
							tangentIndex = vertexTangent->GetIndexArray().GetAt(ctrPtnIndex);
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

				unsigned int vtxIndex = i * 3 + j;

				if (hasSkeleton)
				{
					auto &pair = ctrPointIndices[ctrPtnIndex];
					auto &jointIndices = pair.second.first;
					auto &jointWeights = pair.second.second;
					
					unsigned int index4 = vtxIndex * 4;
					unsigned int index3 = vtxIndex * 3;
					for (unsigned int k = 0; k < jointIndices.size(); k++)
					{
						mesh->IDs.Data[index4] = jointIndices[k];
						index4++;

						if (k > 2) continue;
						mesh->Weights.Data[index3] = jointWeights[k];
						index3++;
					}
				}

				mesh->Indices.Data.push_back(vtxIndex);
			}
		}

		LOGD << mesh->GetName() << " [vtx: " << mesh->Positions.Data.size() / 3 << " tris: " << mesh->Indices.Data.size() / 3 << "]";

		// TODO: 在读完skin信息后，读subMesh时，尝试分割骨骼到各subMesh，解除一个skinMesh只能绑35个骨骼的限制

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

		// load mesh skin info, if there's any.
		if (hasSkeleton)
			CreateSkeleton(ntNode, mesh, fbxMesh);

		mesh->CalculateAABB();

		// optimize mesh data, aka find unique vertices.
		if (m_ImportOptions.Flags & FbxImportFlags::OPTIMIZE_MESH)
			MeshUtil::Instance()->OptimizeMesh(mesh);

		return mesh;
	}

	void DisplayTree(Joint::Ptr root)
	{
		std::stack<Joint::Ptr> jointStack;
		jointStack.push(root);

		while (!jointStack.empty())
		{
			auto joint = jointStack.top();
			jointStack.pop();

			LOGD << joint->GetName();

			std::string desc = "Childs: ";
			auto sibling = joint->GetFirstChild();
			while (sibling != nullptr)
			{
				desc += sibling->GetName() + ", ";

				if (auto firstChild = sibling->GetFirstChild())
					jointStack.push(sibling);

				sibling = sibling->GetSibling();
			}

			LOGD << desc;
		}
	}

	bool FbxUtil::CreateSkeleton(const SceneNode::Ptr &ntNode, const Mesh::Ptr &mesh, FbxMesh *fbxMesh)
	{
		int deformerCount = fbxMesh->GetDeformerCount(FbxDeformer::eSkin);
		if (deformerCount != 1)
		{
			LOGW << "Don't support multiple deformer on one geometry.";
			return false;
		}

		LOGD << "Reading " << mesh->GetName() << "'s skeleton.";

		FbxAMatrix geomMatrix = GetGeometryMatrix(fbxMesh->GetNode());
		FbxSkin *fbxSkin = (FbxSkin*)fbxMesh->GetDeformer(0, FbxDeformer::eSkin);
		int clusterCount = fbxSkin->GetClusterCount();
		if (clusterCount < 1)
			return true;

		LOGD << "Found " << clusterCount << " joints.";

		std::unordered_map<std::string, std::shared_ptr<Joint>> &jointMap = mesh->m_JointMap;
		std::vector<Joint::Ptr> &joints = mesh->m_Joints;

		auto fbxNode = fbxMesh->GetNode();
		auto fbxParentNode = fbxNode->GetParent();

		// find root joint
		FbxNode* root = fbxSkin->GetCluster(0)->GetLink();
		while (root->GetParent() != nullptr)
		{
			if (root == fbxNode || root == fbxParentNode)
				break;

			if (root->GetNodeAttribute() && root->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
			{
				if (((FbxSkeleton*)root->GetNodeAttribute())->GetSkeletonType() == FbxSkeleton::eRoot)
					break;
				root = root->GetParent();
			}
			else
			{
				break;
			}
		}
		LOGD << "Found Root Joint: " << root->GetName();

		// apply root joint's local transform to mesh's node.
		// because root joint's transform is the real transform of this whole skinnedMeshNode.
		ApplyFbxAMatrixToNode(ntNode, root->EvaluateLocalTransform());

		// create joint tree
		{
			auto GetJointByName = [&](const std::string &name) -> Joint::Ptr
			{
				auto it = jointMap.find(name);
				if (it != jointMap.end())
					return it->second;
				else
					return jointMap.emplace(name, Joint::Create(name, mesh)).first->second;
			};

			std::stack<FbxNode*> nodeStack;
			nodeStack.push(root);

			while (!nodeStack.empty())
			{
				auto node = nodeStack.top();
				nodeStack.pop();

				auto joint = GetJointByName(node->GetName());
				if (node != root)
					joint->SetLocalMatrix(FbxMatrixToFuryMatrix(node->EvaluateLocalTransform()));

				Joint::Ptr prevJoint = nullptr;
				int childCount = node->GetChildCount();
				bool firstChild = true;

				for (int i = 0; i < childCount; i++)
				{
					auto curNode = node->GetChild(i);
					if (curNode->GetNodeAttribute() &&
						curNode->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
					{
						auto curJoint = GetJointByName(curNode->GetName());
						curJoint->SetParent(joint);

						if (firstChild)
						{
							firstChild = false;
							joint->SetFirstChild(curJoint);
						}
						else
						{
							prevJoint->SetSibling(curJoint);
						}

						prevJoint = curJoint;

						if (curNode->GetChildCount() > 0)
							nodeStack.push(curNode);
					}
					else
					{
						LOGW << curNode->GetName() << " is not a joint, skipping.";
					}
				}
			}
		}

		// load all joints
		for (int i = 0; i < clusterCount; i++)
		{
			auto cluster = fbxSkin->GetCluster(i);
			auto link = cluster->GetLink();
			auto joint = jointMap[link->GetName()];

			// get offset matrix
			// transforms vertices from model space to bone space.
			FbxAMatrix transformMatrix, transformLinkMatrix, offsetMatrix;
			cluster->GetTransformMatrix(transformMatrix);
			cluster->GetTransformLinkMatrix(transformLinkMatrix);

			offsetMatrix = transformLinkMatrix.Inverse() * transformMatrix * geomMatrix;

			joint->SetOffsetMatrix(FbxMatrixToFuryMatrix(offsetMatrix));
			joints.push_back(joint);
		}

		mesh->m_RootJoint = jointMap[root->GetName()];
		mesh->m_RootJoint->Update(Matrix4());

		//DisplayTree(jointMap[root->GetName()]);
		
		// assign mesh to joints
		m_SkinnedMeshMap.emplace(mesh->GetName(), std::make_pair(fbxMesh, mesh));

		return true;
	}
	
	FbxAMatrix FbxUtil::GetGeometryMatrix(FbxNode* fbxNode) const
	{
		return FbxAMatrix(fbxNode->GetGeometricTranslation(FbxNode::eSourcePivot),
			fbxNode->GetGeometricRotation(FbxNode::eSourcePivot),
			fbxNode->GetGeometricScaling(FbxNode::eSourcePivot));
	}

	Matrix4 FbxUtil::FbxMatrixToFuryMatrix(const FbxAMatrix &fbxMatrix)
	{
		Matrix4 furyMatrix;
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				furyMatrix.Raw[i * 4 + j] = (float)fbxMatrix.Get(i, j);

		return furyMatrix;
	}

	void FbxUtil::ApplyFbxAMatrixToNode(const std::shared_ptr<SceneNode> &ntNode, const FbxAMatrix &fbxMatrix)
	{
		FbxQuaternion fbxQ;
		fbxQ.ComposeSphericalXYZ(fbxMatrix.GetR());
		FbxDouble3 fbxT = fbxMatrix.GetT();
		FbxDouble3 fbxS = fbxMatrix.GetS();

		Vector4 furyT((float)fbxT.mData[0], (float)fbxT.mData[1], (float)fbxT.mData[2], 1.0f);
		Vector4 furyS((float)fbxS.mData[0], (float)fbxS.mData[1], (float)fbxS.mData[2], 1.0f);
		Quaternion furyR((float)fbxQ.mData[0], (float)fbxQ.mData[1], (float)fbxQ.mData[2], (float)fbxQ.mData[3]);

		furyT = furyT * m_ImportOptions.ScaleFactor;
		furyS = furyS * m_ImportOptions.ScaleFactor;

		ntNode->SetLocalPosition(furyT);
		ntNode->SetLocalRoattion(furyR);
		ntNode->SetLocalScale(furyS);
		ntNode->Recompose();
	}
}
