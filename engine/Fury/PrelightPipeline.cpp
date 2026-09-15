#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

// Debug-build picker trace machinery: rejection strings cost a
// snprintf per gate, so they exist only in FURY_BUILD_DEBUG builds.
#ifdef FURY_BUILD_DEBUG
#define FURY_SHADOW_REJECTF(...) do { \
	char rejBuf[256]; \
	std::snprintf(rejBuf, sizeof(rejBuf), __VA_ARGS__); \
	rejected.push_back(rejBuf); } while (0)
#else
#define FURY_SHADOW_REJECTF(...)
#endif

#include "Fury/BuoyancyComponent.h"
#include "Fury/Camera.h"
#include "Fury/Engine.h"
#include "Fury/Log.h"
#include "Fury/EnumUtil.h"
#include "Fury/FramePacket.h"
#include "Fury/Frustum.h"
#include "Fury/GLLoader.h"
#include "Fury/Gui.h"
#include "Fury/Joint.h"
#include "Fury/InputUtil.h"
#include "Fury/InstancedMeshRender.h"
#include "Fury/InstancedMeshStreamer.h"
#include "Fury/Light.h"
#include "Fury/MathUtil.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/MeshUtil.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/ParticleSystem.h"
#include "Fury/Pass.h"
#include "Fury/Pipeline.h"
#include "Fury/PostProcessEffect.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/Profiler.h"
#include "Fury/PrelightPipeline.h"
#include "Fury/RenderSettings.h"
#include "Fury/RenderThread.h"
#include "Fury/RenderTarget.h"
#include "Fury/SkyAtmosphere.h"
#include "Fury/OceanComponent.h"
#include "Fury/OcTree.h"
#include "Fury/PhysicsWorld.h"
#include "Fury/OceanWaves.h"
#include "Fury/RenderQuery.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/SphereBounds.h"
#include "Fury/Texture.h"

namespace fury
{
namespace
{
	// FURY_DRAWCMD_CACHE=0 disables the draw-command cache (debug A/B).
	const bool kDrawCmdCache = std::getenv("FURY_DRAWCMD_CACHE") == nullptr
		|| std::getenv("FURY_DRAWCMD_CACHE")[0] != '0';

	// Shared shader-variant resolution for DrawUnit / the draw-command
	// cache (identical rules, identical fallback order).
	std::shared_ptr<Shader> ResolveUnitShader(const std::shared_ptr<Pass> &pass,
		const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh,
		bool billboard, bool shadowBit)
	{
		auto shader = material->GetShaderForPass(pass->GetRenderIndex());
		if (shader != nullptr)
			return shader;

		// MASK materials request the ALPHA_TEST shader variant so
		// the discard branch compiles only where it's needed.
		unsigned int textureFlags = material->GetTextureFlags();
		if (material->GetAlphaMode() == AlphaMode::MASK)
			textureFlags |= (unsigned int)ShaderTexture::ALPHA_TEST;
		// Shadow-receive variant when this draw's light casts
		// (transparent additive loop) -- the shadow samplers/compares
		// compile only into the *_shadow_shader variants.
		if (shadowBit)
			textureFlags |= (unsigned int)ShaderTexture::SHADOW;
		// Vegetation variant bits from the material flags / LOD tier.
		if (material->GetTwoSided())
			textureFlags |= (unsigned int)ShaderTexture::TWO_SIDED;
		if (material->GetWindEnabled())
			textureFlags |= (unsigned int)ShaderTexture::WIND;
		if (billboard)
			textureFlags |= (unsigned int)ShaderTexture::BILLBOARD |
				(unsigned int)ShaderTexture::TWO_SIDED |
				(unsigned int)ShaderTexture::ALPHA_TEST;
		ShaderType shaderType = mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH;
		shader = pass->GetShader(shaderType, textureFlags);

		// Fall back in steps: first without the shadow bit, then
		// without wind (billboard/alpha combos stay intact), then
		// without the vegetation bits, then without alpha-test.
		// BILLBOARD is never dropped -- a billboard tier without its
		// shader is skipped.
		if (shader == nullptr && (textureFlags & (unsigned int)ShaderTexture::SHADOW))
			shader = pass->GetShader(shaderType,
				textureFlags & ~(unsigned int)ShaderTexture::SHADOW);
		if (shader == nullptr)
			shader = pass->GetShader(shaderType,
				textureFlags & ~(unsigned int)ShaderTexture::SHADOW & ~(unsigned int)ShaderTexture::WIND);
		if (shader == nullptr)
			shader = pass->GetShader(shaderType,
				textureFlags & ~(unsigned int)ShaderTexture::SHADOW &
					~(unsigned int)ShaderTexture::TWO_SIDED & ~(unsigned int)ShaderTexture::WIND);
		if (shader == nullptr && !billboard)
			shader = pass->GetShader(shaderType,
				material->GetTextureFlags());
		return shader;
	}
}

	PrelightPipeline::Ptr PrelightPipeline::Create(const std::string &name)
	{
		return std::make_shared<PrelightPipeline>(name);
	}

	PrelightPipeline::PrelightPipeline(const std::string &name)
		: Pipeline(name)
	{
		m_TypeIndex = typeid(PrelightPipeline);
		SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, true);
	}

	bool PrelightPipeline::Load(const void* wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "Json node is not an object!";
			return false;
		}

		if (!Pipeline::Load(wrapper, false))
			return false;

		bool boolValue = IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP);
		if (LoadMemberValue(wrapper, "cascaded_shadow_map", boolValue))
			SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, boolValue);
		else
			SetSwitch(PipelineSwitch::CASCADED_SHADOW_MAP, true);

		return true;
	}

	void PrelightPipeline::Save(void* wrapper, bool object)
	{
		if (object)
			StartObject(wrapper);

		Pipeline::Save(wrapper, false);

		SaveKey(wrapper, "cascaded_shadow_map");
		SaveValue(wrapper, IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP));

		if (object)
			EndObject(wrapper);
	}

	void PrelightPipeline::Execute(const std::shared_ptr<SceneManager> &sceneManager)
	{
		// Game thread: gather + stage. Execution (this same code path) runs
		// at the loop tail -- on the render thread when threaded, inline
		// on this thread when not.
		ASSERT_MSG(Pipeline::Active.get() == this, "Pipeline::Execute on a non-active pipeline");
		auto &rt = RenderThread::Get();
		FramePacket *packet = rt.AcquirePacket();
		GatherFrame(sceneManager, *packet);
		rt.StagePacket(packet);
	}

	PacketUnit PrelightPipeline::ResolveUnit(const RenderUnit &unit)
	{
		PacketUnit out;
		auto &node = unit.node;
		out.nodeKey = static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(node.get()));
		out.worldMatrix = node->GetWorldMatrix();
		out.worldBounds = node->GetWorldAABB();
		out.worldPos = node->GetWorldPosition();

		// LOD selection lives at gather: the pass path draws what the
		// packet resolved (previously UpdateActiveLod ran per draw).
		auto render = node->GetComponent<MeshRender>();
		auto mesh = unit.mesh;
		auto material = unit.material;
		if (render)
		{
			render->UpdateActiveLod(m_CurrentCamera);
			if (auto active = render->GetActiveMesh())
				mesh = active;
			out.lodIndex = static_cast<int>(render->GetActiveLod());

			// Billboard terminal tier: quad submesh 0 with the mesh's
			// billboard material, and only for the first unit.
			auto baseMesh = render->GetMesh();
			if (baseMesh && baseMesh->IsLodBillboard(render->GetActiveLod()))
			{
				out.billboard = true;
				if (auto bbMat = baseMesh->GetBillboardMaterial())
					material = bbMat;
			}
		}
		out.subMesh = out.billboard ? 0 : unit.subMesh;
		out.mesh = mesh;
		out.material = material;

		// Skinned palette copy: the draw binds these instead of reading
		// live Joint objects on the GL thread.
		if (mesh && mesh->IsSkinnedMesh())
		{
			unsigned int jointCount = mesh->GetJointCount();
			out.skinPalette.reserve(jointCount);
			for (unsigned int i = 0; i < jointCount; ++i)
			{
				auto joint = mesh->GetJointAt(i);
				out.skinPalette.push_back(joint ? joint->GetFinalMatrix() : Matrix4());
			}
		}
		return out;
	}

	void PrelightPipeline::GatherLightShadowPlan(const std::shared_ptr<SceneManager> &sceneManager,
		const std::shared_ptr<SceneNode> &lightNode, PacketLight &out, FramePacket &packet)
	{
		auto light = lightNode->GetComponent<Light>();
		auto camComp = m_CurrentCamera->GetComponent<Camera>();
		if (!light || !light->GetCastShadows() || !camComp)
			return;

		Matrix4 lightMatrix;
		lightMatrix.Rotate(MathUtil::AxisRadToQuat(Vector4::XAxis, MathUtil::DegToRad * 90.0f));
		lightMatrix = lightMatrix * lightNode->GetInvertWorldMatrix();
		out.lightMatrix = lightMatrix;

		// Resolves one caster node list into packet casters (instanced
		// components become indirections into packet.instanced).
		auto resolveCasters = [&](fury::SceneManager::SceneNodes &nodes,
			std::vector<PacketCaster> &outList)
		{
			for (auto &caster : nodes)
			{
				PacketCaster pc;
				pc.worldBounds = caster->GetWorldAABB();
				pc.worldMatrix = caster->GetWorldMatrix();
				if (auto imr = caster->GetComponent<InstancedMeshRender>())
				{
					// Ensure the instanced packet exists (casters can fall
					// outside the camera frustum, so they may be missing
					// from the query's instancedNodes).
					int found = -1;
					std::uint64_t key = static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(caster.get()));
					for (size_t k = 0; k < packet.instanced.size(); ++k)
					{
						if (packet.instanced[k].nodeKey == key)
						{
							found = static_cast<int>(k);
							break;
						}
					}
					if (found < 0)
					{
						if (auto caches = imr->SnapshotRenderCaches())
						{
							PacketInstanced pk;
							pk.nodeKey = key;
							pk.mesh = imr->GetMesh();
							for (unsigned int mi = 0; mi < imr->GetMaterialCount(); ++mi)
								pk.materials.push_back(imr->GetMaterial(mi));
							pk.castShadows = imr->GetCastShadows();
							pk.cullDistance = imr->GetCullDistance();
							pk.hierarchical = imr->GetHierarchical();
							pk.shadowLodTier = imr->GetShadowLodTier();
							pk.aggregateAABB = caster->GetWorldAABB();
							pk.caches = caches;
							packet.instanced.push_back(std::move(pk));
							found = static_cast<int>(packet.instanced.size()) - 1;
						}
					}
					if (found >= 0)
						pc.instancedIndex = found;
					else
						continue;
					outList.push_back(std::move(pc));
					continue;
				}

				auto render = caster->GetComponent<MeshRender>();
				if (!render)
					continue;
				pc.mesh = PickShadowLodMesh(render->GetMesh());
				if (!pc.mesh)
					continue;
				unsigned int subCount = pc.mesh->GetSubMeshCount();
				if (subCount > 0)
				{
					for (unsigned int sm = 0; sm < subCount; ++sm)
						pc.materials.push_back(render->GetMaterial(sm));
				}
				else
				{
					pc.materials.push_back(render->GetMaterial());
				}
				pc.skinned = pc.mesh->IsSkinnedMesh();
				if (pc.skinned)
				{
					unsigned int jointCount = pc.mesh->GetJointCount();
					pc.skinPalette.reserve(jointCount);
					for (unsigned int i = 0; i < jointCount; ++i)
					{
						auto joint = pc.mesh->GetJointAt(i);
						pc.skinPalette.push_back(joint ? joint->GetFinalMatrix() : Matrix4());
					}
				}
				outList.push_back(std::move(pc));
			}
		};

		const bool useCascaded = IsSwitchOn(PipelineSwitch::CASCADED_SHADOW_MAP);
		if (out.type == LightType::DIRECTIONAL && useCascaded)
		{
			// 4 split frusta from the packet's splits
			std::array<Frustum, 4> frustums;
			float curNear = camComp->GetNear();
			for (int i = 0; i < 4; i++)
			{
				frustums[i] = camComp->GetFrustum(curNear, out.csmSplits[i]);
				curNear = out.csmSplits[i];
			}

			fury::SceneManager::SceneNodes casterAll;
			sceneManager->GetVisibleShadowCasters(camComp->GetFrustum(), casterAll);

			for (int i = 0; i < 4; i++)
			{
				fury::SceneManager::SceneNodes splitNodes;
				FilterNodes(frustums[i], casterAll, splitNodes);
				out.csmProj[i] = GetCropMatrix(lightMatrix, frustums[i], splitNodes);
				resolveCasters(splitNodes, out.csmCasters[i]);
			}

			// camera aabb widens split 0 with more possible casters
			if (packet.camera.hasShadowBounds)
			{
				fury::SceneManager::SceneNodes extra;
				sceneManager->GetVisibleShadowCasters(packet.camera.shadowBounds, extra, false);
				std::vector<PacketCaster> extraCasters;
				resolveCasters(extra, extraCasters);
				auto &dst = out.csmCasters[0];
				dst.insert(dst.end(), extraCasters.begin(), extraCasters.end());
			}
		}
		else if (out.type == LightType::DIRECTIONAL)
		{
			auto camFrustum = camComp->GetFrustum(camComp->GetNear(), camComp->GetShadowFar());
			fury::SceneManager::SceneNodes casterNodes;
			sceneManager->GetVisibleShadowCasters(camFrustum, casterNodes, false);
			if (packet.camera.hasShadowBounds)
				sceneManager->GetVisibleShadowCasters(packet.camera.shadowBounds, casterNodes, false);
			out.singleProj = GetCropMatrix(lightMatrix, camFrustum, casterNodes);
			resolveCasters(casterNodes, out.casters);
		}
		else if (out.type == LightType::POINT)
		{
			SphereBounds lightSphere(out.worldPos, out.effectiveRadius);
			fury::SceneManager::SceneNodes casterNodes;
			sceneManager->GetVisibleShadowCasters(lightSphere, casterNodes);
			resolveCasters(casterNodes, out.casters);

			// per-face view matrices (right, left, top, bottom, back, front)
			static const Vector4 kDirs[6] = {
				Vector4(1.0f, 0.0f, 0.0f), Vector4(-1.0f, 0.0f, 0.0f),
				Vector4(0.0f, 1.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f),
				Vector4(0.0f, 0.0f, 1.0f), Vector4(0.0f, 0.0f, -1.0f)
			};
			static const Vector4 kUps[6] = {
				Vector4(0.0f, -1.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f),
				Vector4(0.0f, 0.0f, 1.0f), Vector4(0.0f, 0.0f, -1.0f),
				Vector4(0.0f, -1.0f, 0.0f), Vector4(0.0f, -1.0f, 0.0f)
			};
			for (int i = 0; i < 6; i++)
				out.cubeViews[i].LookAt(out.worldPos, out.worldPos + kDirs[i], kUps[i]);
		}
		else if (out.type == LightType::SPOT)
		{
			Frustum frustum;
			frustum.Setup(out.outterAngle, 1.0f, 1.0f, out.effectiveRadius);
			frustum.Transform(lightMatrix.Inverse());
			out.singleProj.PerspectiveFov(out.outterAngle, 1.0f, 1.0f, out.effectiveRadius);

			fury::SceneManager::SceneNodes casterNodes;
			sceneManager->GetVisibleRenderables(frustum, casterNodes);
			resolveCasters(casterNodes, out.casters);
		}
	}

	void PrelightPipeline::GatherFrame(const std::shared_ptr<SceneManager> &sceneManager, FramePacket &packet)
	{
		FURY_ZONE_NAMED("GatherFrame");
		ASSERT_MSG(m_CurrentCamera != nullptr, "PrelightPipeline.m_CurrentCamera not found!");

		packet.Reset();
		packet.pipeline = Pipeline::Active;
		packet.frameIndex = RenderThread::Get().CurrentFrameIndex();
		packet.camera = BuildPacketCamera(m_CurrentCamera);

		// pre
		SortPassByIndex();
		// Pass list snapshot for the render thread (avoids racing m_SortedPasses).
		packet.sortedPasses = m_SortedPasses;

		// Seed HDR / CSM / chain from the scene's renderSettings (pure CPU
		// state on the pipeline; the packet snapshots the results below).
		if (Scene::Active && Scene::Active->GetRenderSettings())
		{
			auto &rs = *Scene::Active->GetRenderSettings();
			ApplyRenderSettings(rs);
			packet.windParams = rs.GetWindParams();
			packet.csmMapSize = rs.GetCsmMapSize();
			rs.ComputeCsmSplits(packet.camera.nearClip, packet.camera.farClip, packet.csmSplits.data());
		}
		else
		{
			for (int i = 0; i < 4; i++)
				packet.csmSplits[i] = packet.camera.nearClip +
					(packet.camera.farClip - packet.camera.nearClip) * (i + 1) / 4.0f;
		}
		packet.hdrMode = m_HDRMode;
		packet.chain = m_ActiveChain;
		packet.chainOverrides = m_ActiveChainOverrides;
		packet.switches = std::bitset<32>(static_cast<unsigned long>(m_Switches.to_ulong()));
		packet.engineTime = Engine::GetTime();
		InputUtil::Instance()->GetWindowSize(packet.windowW, packet.windowH);
		packet.renderTarget = m_RenderTarget;

		// find visible nodes
		auto query = RenderQuery::Create();
		{
			FURY_ZONE_NAMED("Culling");
			sceneManager->GetRenderQuery(packet.camera.frustum, query);
			query->Sort(packet.camera.worldPos);
		}

		// resolve opaque/transparent units (LOD + billboard + palettes)
		{
			FURY_ZONE_NAMED("ResolveUnits");
			packet.opaqueUnits.reserve(query->opaqueUnits.size());
			for (const auto &unit : query->opaqueUnits)
				packet.opaqueUnits.push_back(ResolveUnit(unit));
			packet.transparentUnits.reserve(query->transparentUnits.size());
			for (const auto &unit : query->transparentUnits)
				packet.transparentUnits.push_back(ResolveUnit(unit));
		}

		// lights + shadow plans
		{
			FURY_ZONE_NAMED("GatherLights");
			for (const auto &node : query->lightNodes)
			{
				auto light = node->GetComponent<Light>();
				if (!light)
					continue;
				PacketLight pl;
				pl.nodeKey = static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(node.get()));
				pl.name = node->GetName();
				pl.type = light->GetType();
				pl.color = light->GetColor();
				pl.intensity = light->GetIntensity();
				pl.innerAngle = light->GetInnerAngle();
				pl.outterAngle = light->GetOutterAngle();
				pl.falloff = light->GetFalloff();
				pl.radius = light->GetRadius();
				pl.effectiveRadius = light->GetEffectiveRadius();
				pl.castShadows = light->GetCastShadows();
				pl.worldMatrix = node->GetWorldMatrix();
				pl.invertWorldMatrix = node->GetInvertWorldMatrix();
				pl.worldPos = node->GetWorldPosition();
				pl.worldDir = pl.worldMatrix.Multiply(Vector4(0, -1, 0, 0)).Normalized();
				pl.volumeMesh = light->GetMesh();
				pl.csmMapSize = packet.csmMapSize;
				pl.csmSplits = packet.csmSplits;
				if (pl.castShadows)
					GatherLightShadowPlan(sceneManager, node, pl, packet);
				packet.lights.push_back(std::move(pl));
			}
		}

		// instanced components (camera-frustum list; shadow-only casters
		// are appended by GatherLightShadowPlan above)
		{
			FURY_ZONE_NAMED("GatherInstanced");
			for (const auto &node : query->instancedNodes)
			{
				auto imr = node->GetComponent<InstancedMeshRender>();
				if (!imr || !imr->GetRenderable())
					continue;
				std::uint64_t key = static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(node.get()));
				bool exists = false;
				for (auto &existing : packet.instanced)
					if (existing.nodeKey == key) { exists = true; break; }
				if (exists)
					continue;
				auto caches = imr->SnapshotRenderCaches();
				if (!caches)
					continue;
				PacketInstanced pk;
				pk.nodeKey = key;
				pk.mesh = imr->GetMesh();
				for (unsigned int mi = 0; mi < imr->GetMaterialCount(); ++mi)
					pk.materials.push_back(imr->GetMaterial(mi));
				pk.castShadows = imr->GetCastShadows();
				pk.cullDistance = imr->GetCullDistance();
				pk.hierarchical = imr->GetHierarchical();
				pk.shadowLodTier = imr->GetShadowLodTier();
				pk.aggregateAABB = node->GetWorldAABB();
				pk.caches = caches;
				packet.instanced.push_back(std::move(pk));
			}
		}

		// particles: bake billboards into the parity buffer + resolve
		{
			FURY_ZONE_NAMED("GatherParticles");
			for (const auto &node : query->particleNodes)
			{
				auto pr = node->GetComponent<ParticleRenderer>();
				if (!pr)
					continue;
				PacketParticles pp;
				if (pr->GatherPacket(pp, packet.camera, packet.frameIndex) > 0)
					packet.particles.push_back(std::move(pp));
			}
		}

		// oceans: camera-follow snap on the game thread, piece snapshot out
		for (const auto &node : query->oceanNodes)
		{
			auto ocean = node->GetComponent<OceanComponent>();
			if (!ocean)
				continue;
			ocean->UpdateCameraFollow(packet.camera.worldPos);
			PacketOcean po;
			po.nodeKey = static_cast<std::uint64_t>(reinterpret_cast<uintptr_t>(node.get()));
			po.nodePos = node->GetWorldPosition();
			po.waterLevel = ocean->GetWaterLevel();
			po.waveTime = ocean->GetWaveTime();
			po.roughness = ocean->GetRoughness();
			po.normalStrength = ocean->GetNormalStrength();
			po.foamAmount = ocean->GetFoamAmount();
			po.shoreFoamDepthCm = ocean->GetShoreFoamDepthCm();
			po.windSpeed = ocean->GetWindSpeed();
			po.skirtRadiusCm = ocean->GetSkirtRadiusCm();
			po.absorb = ocean->GetAbsorbColor();
			po.scatter = ocean->GetScatterColor();
			po.fadeRanges = ocean->GetFadeRanges();
			po.debugView = ocean->GetDebugView();
			po.finite = ocean->GetMode() == OceanComponent::Mode::Finite;
			po.waves = ocean->GetWaves();
			if (po.finite)
			{
				po.finiteMesh = ocean->GetFiniteMesh();
			}
			else
			{
				for (const auto &piece : ocean->GetRingPieces())
				{
					PacketOceanPiece pp;
					pp.mesh = piece.MeshPtr;
					pp.origin = piece.Origin;
					pp.yOffset = piece.YOffset;
					pp.isSkirt = piece.IsSkirt;
					po.pieces.push_back(std::move(pp));
				}
			}
			packet.oceans.push_back(std::move(po));
		}

		// sky
		if (auto sky = SkyAtmosphere::GetActive())
		{
			packet.sky.valid = true;
			packet.sky.enabled = sky->GetEnabled();
			packet.sky.owner = sky;
			packet.sky.params = sky->GatherSkyParams(packet.camera.worldPos.y);
			sky->SnapshotRenderTextures(packet.sky);
		}

		// debug snapshots (only when a debug switch is on)
		if (IsSwitchOn({ PipelineSwitch::CUSTOM_BOUNDS, PipelineSwitch::LIGHT_BOUNDS,
			PipelineSwitch::MESH_BOUNDS, PipelineSwitch::OCTREE_BOUNDS,
			PipelineSwitch::EDITOR_GRID }, true))
		{
			if (IsSwitchOn(PipelineSwitch::MESH_BOUNDS))
				for (const auto &node : query->renderableNodes)
					packet.debug.meshBounds.push_back(node->GetWorldAABB());
			if (IsSwitchOn(PipelineSwitch::CUSTOM_BOUNDS))
			{
				packet.debug.customBoxes = m_DebugBoxBounds;
				packet.debug.customFrusta = m_DebugFrustum;
			}
			if (IsSwitchOn(PipelineSwitch::OCTREE_BOUNDS) && Scene::Active)
			{
				if (auto tree = std::dynamic_pointer_cast<OcTree>(Scene::Active->GetSceneManager()))
					tree->CollectDebugBounds(packet.debug.octreeBounds);
			}
		}
		if (IsSwitchOn(PipelineSwitch::BUOYANCY_DEBUG) &&
			PhysicsWorld::Exists() &&
			!PhysicsWorld::Instance()->GetBuoyancies().empty())
		{
			bool simulating = PhysicsWorld::Instance()->IsSimulationEnabled();
			for (const auto &weak : PhysicsWorld::Instance()->GetBuoyancies())
			{
				auto buoyancy = weak.lock();
				if (!buoyancy)
					continue;
				auto node = buoyancy->GetOwner();
				if (!node)
					continue;
				Matrix4 world = node->GetWorldMatrix();
				const auto &points = buoyancy->GetFloatPoints();
				for (unsigned int i = 0; i < points.size(); i++)
				{
					PacketDebug::BuoyMark mark;
					mark.center = world.Multiply(points[i].Offset);
					mark.radius = points[i].Radius;
					mark.color = Color(0.2f, 0.8f, 0.9f, 1.0f);
					if (simulating)
					{
						float s = buoyancy->GetLastSubmersion(i);
						mark.color = Color(0.2f + 0.75f * s, 0.9f - 0.65f * s, 0.2f, 1.0f);
					}
					packet.debug.buoyMarks.push_back(mark);
				}
			}
		}
	}

	void PrelightPipeline::ExecutePacket(FramePacket &packet)
	{
		FURY_ZONE_NAMED("ExecutePacket");
		ASSERT_MSG(packet.camera.valid, "FramePacket.camera not valid!");

		// pre
		m_CurrentShader = nullptr;
		m_CurrentMateral = nullptr;
		m_CurrentMesh = nullptr;

		m_CacheHits = 0;
		m_CacheRebuilds = 0;

		packet.shadowResults.clear();
		packet.shadowResults.resize(packet.lights.size());
		packet.frameShadowTemps.clear();

		// Per-instance frustum culling + HISM LOD bucketing on packet data
		// (game-thread gather snapshot; runs on the GL thread now).
		if (!packet.instanced.empty())
		{
			FURY_ZONE_NAMED("InstancedCulling");
			for (auto &pk : packet.instanced)
				BuildInstancedBatches(pk, packet.camera.frustum, packet.camera, pk.batches);
		}

		// draw passes

		unsigned int passCount = static_cast<int>(packet.sortedPasses.size());

		// The chain replaces the LAST quad pass (the screen write)
		// but keeps intermediate quad passes (e.g. pass_combine)
		// running so their outputs stay readable.
		const bool chainReplacesFinal =
			!packet.chain.empty() &&
			(!packet.hdrMode || HasHDRComposite());

		for (unsigned int i = 0; i < passCount; i++)
		{
			auto passName = packet.sortedPasses[i];
			auto pass = m_EntityManager->Get<Pass>(passName);

			FURY_ZONE_DYNAMIC(passName.c_str());
			FURY_GPU_ZONE_DYNAMIC(passName.c_str());

			auto drawMode = pass->GetDrawMode();

			m_CurrentShader = pass->GetFirstShader();

			// Skip only the last quad pass (the screen write);
			// earlier quad passes must still produce their outputs
			// for the chain to read.
			bool isLastQuadPass = (chainReplacesFinal &&
				drawMode == DrawMode::QUAD &&
				i == passCount - 1);
			if (isLastQuadPass)
				continue;

			// enable gamma correction on last pass (legacy path;
			// the chain encodes itself)
			bool isFinalLegacyPass = !chainReplacesFinal && (i == passCount - 1);
			if (isFinalLegacyPass)
				glEnable(GL_FRAMEBUFFER_SRGB);

			if (drawMode == DrawMode::OPAQUE)
			{
				pass->Bind();
				for (const auto &unit : packet.opaqueUnits)
					DrawUnit(pass, unit, packet);
				DrawInstancedUnits(pass, packet);
			}
			else if (drawMode == DrawMode::TRANSPARENT)
			{
				pass->Bind();

				// Back-to-front (gather-side sort) alpha-blended base:
				// ambient + emissive + albedo*alpha compositing.
				for (const auto &unit : packet.transparentUnits)
					DrawUnit(pass, unit, packet);

				// CPU-driven billboard particles. Drawn after
				// transparent mesh units so they participate in
				// back-to-front sort order. Particles are emissive
				// in v1 (no light sampling) -- they go between the
				// base pass and the additive light pass, NOT into
				// the additive loop (which would double-bright
				// the smoke/fire).
				if (!packet.particles.empty())
				{
					// Picks THE dominant shadow source per emitter (one
					// per particle draw; mesh transparents evaluate every
					// light in the additive loop instead). Highest score
					// wins; dir uses CSM when the switch is on, else
					// dir-single.
					auto pickShadowSourceFor =
						[&](const Vector4 &emitterPos,
							ParticleShadowInfo &info)
					{
						// Score = light arriving at the emitter: raw
						// intensity for the directional (NO distance term --
						// intensity/dist2 reads the sun node's parked
						// position; a near-origin dim sun outscored every
						// local light and the spot path never ran),
						// intensity x linear falloff for point/spot.
						struct Candidate {
							int lightIndex;
							LightType type;
							float score;
						};
						std::vector<Candidate> cands;
#ifdef FURY_BUILD_DEBUG
						std::vector<std::string> rejected;
#endif
						for (int li = 0; li < (int)packet.lights.size(); ++li)
						{
							const auto &pl = packet.lights[li];
							if (!pl.castShadows) { FURY_SHADOW_REJECTF("noCast: %s", pl.name.c_str()); continue; }
							const auto &sr = packet.shadowResults[li];
							if (!sr.texture) { FURY_SHADOW_REJECTF("noShadowTex: %s", pl.name.c_str()); continue; }
							info.anyCaster = true;
							auto wp = pl.worldPos;
							float dx = wp.x - emitterPos.x;
							float dy = wp.y - emitterPos.y;
							float dz = wp.z - emitterPos.z;
							float dist2 = dx * dx + dy * dy + dz * dz;

							// Influence gate: only lights that REACH
							// this emitter may be ranked -- Particle.glsl
							// reads an out-of-range source as fully lit.
							if (pl.type == LightType::POINT)
							{
								float r = pl.effectiveRadius;
								if (dist2 > r * r) { FURY_SHADOW_REJECTF("radius P: %s", pl.name.c_str()); continue; }
							}
							else if (pl.type == LightType::SPOT)
							{
								float r = pl.effectiveRadius;
								if (dist2 > r * r)
								{
									FURY_SHADOW_REJECTF("radius S r=%.2f: %s",
										r, pl.name.c_str());
									continue;
								}
								// Emitter must lie within the outer cone;
								// dx is emitter->light so the inside test
								// flips sign: cosTheta < -cosOuter.
								float dist = std::sqrt(dist2) + 1e-4f;
								const Vector4 &lightFwd = pl.worldDir;
								float cosTheta = (dx * lightFwd.x + dy * lightFwd.y + dz * lightFwd.z) / dist;
								float cosOuter = std::cos(pl.outterAngle * 0.5f);
								if (cosTheta >= -cosOuter)
								{
									FURY_SHADOW_REJECTF(
										"cone S cosT=%.4f cosOuter=%.4f outAngle=%.2f fwd=(%.3f,%.3f,%.3f): %s",
										cosTheta, cosOuter, pl.outterAngle,
										lightFwd.x, lightFwd.y, lightFwd.z,
										pl.name.c_str());
									continue;
								}
							}

							float score;
							if (pl.type == LightType::DIRECTIONAL)
								score = pl.intensity;
							else
								score = pl.intensity
									* std::max(0.0f, 1.0f - std::sqrt(dist2)
										/ pl.effectiveRadius);
							cands.push_back({li, pl.type, score});
						}
						std::sort(cands.begin(), cands.end(),
							[](const Candidate &a, const Candidate &b)
							{ return a.score > b.score; });

						auto populateFrom = [&](int lightIndex, LightType t)
						{
							const auto &pl = packet.lights[lightIndex];
							const auto &sr = packet.shadowResults[lightIndex];
							info.texture = sr.texture;
							// Cached matrices map camera-view -> shadow
							// UV (deferred convention); particles feed
							// world pos, so append the camera's
							// invert-world to chain world->view->shadow UV.
							const Matrix4 &viewFromWorld = packet.camera.invertWorldMatrix;
							if (t == LightType::POINT)
							{
								info.type = 1;
								info.lightPos = Vector4(pl.worldPos.x, pl.worldPos.y, pl.worldPos.z, 0);
								info.lightRadius = pl.effectiveRadius;
							}
							else if (t == LightType::SPOT)
							{
								info.type = 4;
								info.matrix = sr.single * viewFromWorld;
								// Cone-test inputs: Particle.glsl is
								// emissive, so the cone falloff lives in
								// the shadow factor.
								info.lightPos = Vector4(pl.worldPos.x, pl.worldPos.y, pl.worldPos.z, 0);
								info.lightDir = pl.worldDir;
								info.coneHalfInner = pl.innerAngle * 0.5f;
								info.coneHalfOuter = pl.outterAngle * 0.5f;
							}
							else if (t == LightType::DIRECTIONAL)
							{
								if (packet.switches.test((size_t)PipelineSwitch::CASCADED_SHADOW_MAP)
									&& !sr.csm.empty())
								{
									info.type = 3;
									for (int c = 0; c < 4; c++)
										info.csmMatrices[c] = sr.csm[c] * viewFromWorld;
									info.shadowFar = sr.shadowFar;
								}
								else
								{
									info.type = 2;
									info.matrix = sr.single * viewFromWorld;
								}
							}
						};

						int pickedIndex = -1;
						LightType pickedType = LightType::POINT;
						if (!cands.empty())
						{
							pickedIndex = cands.front().lightIndex;
							pickedType = cands.front().type;
							populateFrom(pickedIndex, pickedType);
						}

#ifdef FURY_BUILD_DEBUG
						// FURY_SHADOW_DEBUG=1 dumps the pick + rejection
						// reasons (one block per emitter per frame --
						// headless debugging only).
						static const bool kShadowDbg =
							std::getenv("FURY_SHADOW_DEBUG") != nullptr;
						if (kShadowDbg)
						{
							std::fprintf(stderr,
								"[shadowpick] emitter(%.0f,%.0f,%.0f) -> %s type=%d\n",
								emitterPos.x, emitterPos.y, emitterPos.z,
								pickedIndex >= 0 ? packet.lights[pickedIndex].name.c_str() : "(none)",
								info.type);
							if (pickedIndex >= 0 && pickedType == LightType::SPOT)
							{
								const auto &l = packet.lights[pickedIndex];
								std::fprintf(stderr,
									"    spot inner=%.4f outter=%.4f rad (%.1f/%.1f deg)\n",
									l.innerAngle, l.outterAngle,
									l.innerAngle * 57.2958f, l.outterAngle * 57.2958f);
							}
							for (const auto &r : rejected)
								std::fprintf(stderr, "    reject %s\n", r.c_str());
						}
#endif
					};

					GLint prevSrc, prevDst;
					glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrc);
					glGetIntegerv(GL_BLEND_DST_RGB, &prevDst);
					for (const auto &pp : packet.particles)
					{
						// Mirror the renderer's blend mode onto GL
						// state. Particle renderers don't bind the
						// pipeline's Pass blend -- each emitter has
						// its own ALPHA/ADDITIVE choice.
						if (pp.blendMode == ParticleBlend::ADDITIVE)
							glBlendFunc(GL_ONE, GL_ONE);
						else
							glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						// Depth-tested, NOT depth-written (the
						// transparent pass convention).
						glEnable(GL_DEPTH_TEST);
						glDepthMask(GL_FALSE);
						// Per-renderer shadow selection -- pick the
						// dominant casting light for THIS emitter.
						ParticleShadowInfo shadowInfo;
						pickShadowSourceFor(pp.worldPos, shadowInfo);
						ParticleRenderer::DrawPacket(pp, packet.camera, &shadowInfo);
						glDepthMask(GL_TRUE);
					}
					glBlendFunc(prevSrc, prevDst);
				}

				// One additive (ONE, ONE) draw per light so
				// transparents pick up direct lighting without light arrays. The forward shader premultiplies diffuse by alpha and leaves specular full-strength (glass highlights); per-light occlusion between transparents is ignored -- documented approximation.
				if (!packet.lights.empty())
				{
					// Additive over the pass's declared alpha blend: restore exactly when the loop exits so the deviation doesn't leak.
					GLint prevSrc, prevDst;
					glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrc);
					glGetIntegerv(GL_BLEND_DST_RGB, &prevDst);
					glBlendFunc(GL_ONE, GL_ONE);
					for (int li = 0; li < (int)packet.lights.size(); ++li)
					{
						for (const auto &unit : packet.transparentUnits)
							DrawUnit(pass, unit, packet, li);
					}
					glBlendFunc(prevSrc, prevDst);
				}
			}
			else if (drawMode == DrawMode::QUAD)
			{
				pass->Bind();
				DrawQuad(pass, packet);
			}
			else if (drawMode == DrawMode::SKY)
			{
				// DrawSky owns the bind: LUT updates render into their
				// own FBOs first, then pass_sky binds (no clear -- it
				// would wipe hdr_composite).
				DrawSky(pass, packet);
			}
			else if (drawMode == DrawMode::OCEAN)
			{
				// Same owned-bind pattern as DrawSky (needs a pre-pass
				// depth copy for shore foam before the pass binds).
				DrawOcean(pass, packet);
			}
			else if (drawMode == DrawMode::LIGHT)
			{
				pass->Bind(true);

				for (int li = 0; li < (int)packet.lights.size(); ++li)
				{
					const auto &pl = packet.lights[li];
					if (pl.type == LightType::DIRECTIONAL)
						DrawDirLight(pass, packet, li);
					else if (pl.type == LightType::POINT)
						DrawPointLight(pass, packet, li);
					else
						DrawSpotLight(pass, packet, li);
				}
			}

			pass->UnBind();

			if (isFinalLegacyPass)
				glDisable(GL_FRAMEBUFFER_SRGB);

			if (m_CurrentShader != nullptr)
				m_CurrentShader->UnBind();

			m_CurrentShader = nullptr;
			m_CurrentMateral = nullptr;
			m_CurrentMesh = nullptr;
		}

		// Run the postprocess chain if one is active. The chain's
		// final write hits the default framebuffer (or the editor's
		// RenderTarget if set) with sRGB encode baked into the
		// shader's u_gamma_correct uniform (since the FBOs we bind
		// are non-sRGB).
		if (chainReplacesFinal)
			RunPostProcessChain(packet);

		// draw debug
		if ((packet.switches.test((size_t)PipelineSwitch::CUSTOM_BOUNDS) ||
			packet.switches.test((size_t)PipelineSwitch::LIGHT_BOUNDS) ||
			packet.switches.test((size_t)PipelineSwitch::MESH_BOUNDS) ||
			packet.switches.test((size_t)PipelineSwitch::OCTREE_BOUNDS) ||
			packet.switches.test((size_t)PipelineSwitch::EDITOR_GRID)) ||
			!packet.debug.buoyMarks.empty())
		{
			// When an offscreen RenderTarget is set, the final composite
			// pass rendered into it (see Pass::Bind). The last pass's
			// UnBind rebound framebuffer 0, so re-bind the RT here so the
			// debug overlays composite over the scene inside the viewport
			// image. Restore framebuffer 0 afterward so the caller (and
			// the GUI pass) draw to the default framebuffer.
			GLint prev_fbo = 0;
			GLint prev_vp[4] = { 0, 0, 0, 0 };
			if (packet.renderTarget != nullptr && packet.renderTarget->IsAllocated())
			{
				glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
				glGetIntegerv(GL_VIEWPORT, prev_vp);
				glBindFramebuffer(GL_FRAMEBUFFER, packet.renderTarget->GetFBO());
				glViewport(0, 0, packet.renderTarget->GetWidth(), packet.renderTarget->GetHeight());
			}

			DrawDebug(packet);

			if (packet.renderTarget != nullptr && packet.renderTarget->IsAllocated())
			{
				glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
				glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
			}
		}

		// Buffer debug view (viewport toolbar "View SSAO/SSR"): runs
		// the effect's DEBUG_VIEW variant into the "debug_view"
		// texture; the editor presents it in place of the scene.
		if (packet.switches.test((size_t)PipelineSwitch::SSAO_VIEW))
			DrawEffectDebugView("SSAO", packet);
		else if (packet.switches.test((size_t)PipelineSwitch::SSR_VIEW))
			DrawEffectDebugView("SSR", packet);
		else
			SetDebugViewTexture(nullptr);

		// post
		m_CurrentShader = nullptr;
		m_CurrentMateral = nullptr;
		m_CurrentMesh = nullptr;

		// Now that every pass (transparent shadow-receive included) is
		// done sampling this frame's shadow maps, return them to the
		// temporary pool.
		for (auto &tex : packet.frameShadowTemps)
			Texture::ReleaseTemporary(tex);
		packet.frameShadowTemps.clear();

		FURY_PLOT("drawcmd_hits", (double)m_CacheHits);
		FURY_PLOT("drawcmd_rebuilds", (double)m_CacheRebuilds);
		FURY_PLOT("draw_calls", (double)RenderUtil::Instance()->GetDrawCall());
	}

	void PrelightPipeline::ReplayDrawCommand(DrawCommand &cmd, const std::shared_ptr<Pass> &pass,
		const Matrix4 *worldMatrix, FramePacket &packet)
	{
		auto shader = cmd.shader;
		auto material = cmd.material;

		bool materialChanged = material != m_CurrentMateral;
		m_CurrentMateral = material;

		bool shaderChanged = materialChanged || shader != m_CurrentShader;
		m_CurrentShader = shader;

		int cursor = 0;
		if (shaderChanged)
		{
			materialChanged = true;

			shader->Bind();
			shader->BindCameraData(packet.camera);

			for (const auto &tb : cmd.passTextureBinds)
				cursor = shader->BindTextureAt(cursor, tb.location, tb.texture);
		}
		if (materialChanged)
		{
			if (!shaderChanged)
				cursor = cmd.passBoundCount;
			for (const auto &tb : cmd.textureBinds)
				cursor = shader->BindTextureAt(cursor, tb.location, tb.texture);
			for (const auto &ub : cmd.uniformBinds)
				ub.uniform->BindLocation(ub.location);
			shader->BindFloatLocation(cmd.alphaCutoffLoc, cmd.alphaCutoff);
		}
		shader->SetTextureUnitCursor(cursor);

		// Wind sway uniforms. Bound per draw (u_time advances every
		// frame); silent no-op on shaders without the WIND variant.
		if (cmd.wind)
		{
			shader->BindFloatLocation(cmd.timeLoc, packet.engineTime);
			shader->BindFloatLocation(cmd.windParamsLoc, packet.windParams.x,
				packet.windParams.y, packet.windParams.z, packet.windParams.w);
		}

		if (pass->GetDrawMode() == DrawMode::TRANSPARENT)
			shader->BindIntLocation(cmd.lightTypeLoc, 0);

		if (worldMatrix != nullptr)
			shader->BindMatrixLocation(cmd.worldMatrixLoc, &worldMatrix->Raw[0]);

	}

	void PrelightPipeline::DrawUnitCached(const std::shared_ptr<Pass> &pass, const PacketUnit &unit,
		FramePacket &packet)
	{
		auto material = unit.material;
		auto mesh = unit.mesh;
		const int drawSubMesh = unit.subMesh;
		const bool billboard = unit.billboard;

		const std::uint64_t key = unit.nodeKey
			^ (static_cast<std::uint64_t>(drawSubMesh + 1) << 48)
			^ (static_cast<std::uint64_t>(pass->GetRenderIndex() & 0xff) << 40)
			^ (static_cast<std::uint64_t>(unit.lodIndex & 0xff) << 56);
		auto &cmd = m_DrawCommandCache[key];

		if (cmd.shader == nullptr || cmd.material != material || cmd.mesh != mesh
			|| cmd.materialVersion != material->GetRenderVersion())
		{
			++m_CacheRebuilds;

			auto shader = ResolveUnitShader(pass, material, mesh, billboard, false);
			if (shader == nullptr)
			{
				FURYW << "Failed to draw unit " << unit.nodeKey << ", shader not found!";
				cmd = DrawCommand();
				return;
			}

			cmd = DrawCommand();
			cmd.shader = shader;
			cmd.material = material;
			cmd.mesh = mesh;
			cmd.materialVersion = material->GetRenderVersion();
			cmd.subMesh = drawSubMesh;
			cmd.cullOff = material->GetTwoSided() || billboard;
			cmd.wind = (shader->GetTextureFlags() & (unsigned int)ShaderTexture::WIND) != 0;
			cmd.alphaCutoff = material->GetAlphaMode() == AlphaMode::MASK ? material->GetAlphaCutoff() : -1.0f;

			for (const auto &kv : material->GetTextures())
				cmd.textureBinds.push_back({ shader->GetUniformLocation(kv.first), kv.second });
			for (const auto &kv : material->GetUniforms())
				cmd.uniformBinds.push_back({ shader->GetUniformLocation(kv.first), kv.second });
			for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
			{
				auto ptr = pass->GetTextureAt(i, true);
				cmd.passTextureBinds.push_back({ shader->GetUniformLocation(ptr->GetName()), ptr });
			}
			for (const auto &tb : cmd.passTextureBinds)
				if (tb.location != -1 && tb.texture != nullptr)
					++cmd.passBoundCount;

			cmd.worldMatrixLoc = shader->GetUniformLocation(Matrix4::WORLD_MATRIX);
			cmd.alphaCutoffLoc = shader->GetUniformLocation("u_alpha_cutoff");
			cmd.timeLoc = shader->GetUniformLocation("u_time");
			cmd.windParamsLoc = shader->GetUniformLocation("u_wind_params");
			cmd.lodDebugLoc = shader->GetUniformLocation("lod_debug_color");
			cmd.lightTypeLoc = shader->GetUniformLocation("u_light_type");
		}
		else
		{
			++m_CacheHits;
		}

		ReplayDrawCommand(cmd, pass, &unit.worldMatrix, packet);

#if WITH_DBG_OVERLAY
		// LOD debug tint per unit tier (bound per draw: programs retain
		// stale values otherwise, see the uncached path's note).
		if (packet.switches.test((size_t)PipelineSwitch::LOD_DEBUG_COLORS))
		{
			Color lodColor = GetLodDebugColor(static_cast<unsigned int>(unit.lodIndex));
			cmd.shader->BindFloatLocation(cmd.lodDebugLoc, lodColor.r, lodColor.g, lodColor.b, lodColor.a);
		}
		else
		{
			cmd.shader->BindFloatLocation(cmd.lodDebugLoc, 0.0f, 0.0f, 0.0f, 0.0f);
		}
#endif

		bool meshChanged = mesh != m_CurrentMesh;
		m_CurrentMesh = mesh;
		if (meshChanged)
			cmd.shader->BindMesh(mesh);

		if (cmd.cullOff)
			glDisable(GL_CULL_FACE);

		if (mesh->GetSubMeshCount() > 0)
		{
			auto subMesh = mesh->GetSubMeshAt(drawSubMesh);
			if (subMesh == nullptr)
				return;
			cmd.shader->BindSubMesh(mesh, drawSubMesh);
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(subMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(subMesh->Indices.Data.size()));
		}
		else
		{
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(mesh->Indices.Data.size()));
		}

		if (cmd.cullOff)
		{
			if (pass->GetCullMode() != CullMode::NONE)
			{
				glEnable(GL_CULL_FACE);
				glCullFace(EnumUtil::CullModeToUint(pass->GetCullMode()).second);
			}
		}

		RenderUtil::Instance()->IncreaseMeshCount();
		RenderUtil::Instance()->IncreaseDrawCall();
	}

	void PrelightPipeline::DrawUnit(const std::shared_ptr<Pass> &pass, const PacketUnit &unit,
		FramePacket &packet, int lightIndex)
	{
		auto material = unit.material;
		auto mesh = unit.mesh;
		if (!mesh || !material)
			return;

		// Static opaque / transparent-base draws go through the
		// draw-command cache; skinned units and per-light additive
		// draws stay on the dynamic path below.
		const bool cacheable = kDrawCmdCache &&
			(pass->GetDrawMode() == DrawMode::OPAQUE ||
				(pass->GetDrawMode() == DrawMode::TRANSPARENT && lightIndex < 0)) &&
			!mesh->IsSkinnedMesh();
		if (cacheable)
		{
			DrawUnitCached(pass, unit, packet);
			return;
		}

		const int drawSubMesh = unit.subMesh;
		const bool billboard = unit.billboard;

		auto shader = ResolveUnitShader(pass, material, mesh, billboard,
			lightIndex >= 0 && packet.lights[lightIndex].castShadows);

		if (shader == nullptr)
		{
			FURYW << "Failed to draw unit " << unit.nodeKey << ", shader not found!";
			return;
		}

		bool materialChanged = material != m_CurrentMateral;
		m_CurrentMateral = material;

		bool meshChanged = mesh != m_CurrentMesh;
		m_CurrentMesh = mesh;

		bool shaderChanged = materialChanged || shader != m_CurrentShader;
		m_CurrentShader = shader;

		if (shaderChanged)
		{
			materialChanged = meshChanged = true;

			shader->Bind();
			shader->BindCameraData(packet.camera);

			for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
			{
				auto ptr = pass->GetTextureAt(i, true);
				shader->BindTexture(ptr->GetName(), ptr);
			}
		}

		if (materialChanged)
		{
			shader->BindMaterial(material);

			// Alpha test: only MASK materials get a cutoff >= 0;
			// everything else disables the discard branch via -1.
			// (Programs retain stale uniforms, so bind every time the
			// material changes.)
			shader->BindFloat("u_alpha_cutoff",
				material->GetAlphaMode() == AlphaMode::MASK ? material->GetAlphaCutoff() : -1.0f);
		}

		// Wind sway uniforms. Bound per draw (u_time advances every
		// frame); silent no-op on shaders without the WIND variant.
		if (shader->GetTextureFlags() & (unsigned int)ShaderTexture::WIND)
		{
			shader->BindFloat("u_time", packet.engineTime);
			shader->BindFloat("u_wind_params", packet.windParams.x, packet.windParams.y,
				packet.windParams.z, packet.windParams.w);
		}

		// Forward transparent shading: u_light_type 0 = ambient/emissive
		// base, 1/2/3 = directional/point/spot additive contribution.
		if (pass->GetDrawMode() == DrawMode::TRANSPARENT)
		{
			int lightType = 0;
			if (lightIndex >= 0)
			{
				const auto &pl = packet.lights[lightIndex];
				lightType = (int)pl.type + 1;
				shader->BindLightData(pl);

				// Shadow-receive for this light's direct
				// contribution (matches the deferred path's
				// behavior for opaques). Only the *_shadow_shader
				// variants declare the samplers; on any other
				// shader this block must not even bind dummies.
				if (shader->GetTextureFlags() & (unsigned int)ShaderTexture::SHADOW)
				{
					int shadowType = 0;
					Texture::Ptr shadowTex2D, shadowCube, shadowTexCSM;
					if (pl.castShadows)
					{
						const auto &sr = packet.shadowResults[lightIndex];
						if (sr.texture)
						{
							if (pl.type == LightType::POINT)
							{
								shadowType = 1;
								shadowCube = sr.texture;
								shader->BindMatrix("shadow_matrix", &packet.camera.worldMatrix.Raw[0]);
							}
							else if (pl.type == LightType::DIRECTIONAL &&
								!packet.switches.test((size_t)PipelineSwitch::CASCADED_SHADOW_MAP))
							{
								shadowType = 2;
								shadowTex2D = sr.texture;
								shader->BindMatrix("shadow_matrix", &sr.single.Raw[0]);
							}
							else if (pl.type == LightType::DIRECTIONAL)
							{
								// CSM (CASCADED_SHADOW_MAP on)
								if (!sr.csm.empty())
								{
									shadowType = 3;
									shadowTexCSM = sr.texture;
									shader->BindMatrices("shadow_matrix_csm", (int)sr.csm.size(), &sr.csm[0]);
									shader->BindFloat("shadow_far",
										sr.shadowFar.x, sr.shadowFar.y,
										sr.shadowFar.z, sr.shadowFar.w);
								}
							}
							else if (pl.type == LightType::SPOT)
							{
								shadowType = 4;
								shadowTex2D = sr.texture;
								shader->BindMatrix("shadow_matrix", &sr.single.Raw[0]);
							}
						}
					}
					shader->BindTexture("shadow_map", shadowTex2D ? shadowTex2D : GetDummyTexture2D());
					shader->BindTexture("shadow_buffer", shadowCube ? shadowCube : GetDummyCubeTexture());
					shader->BindTexture("shadow_buffer_csm", shadowTexCSM ? shadowTexCSM : GetDummyTexture2DArray());
					shader->BindInt("u_shadow_type", shadowType);
				}
			}
			shader->BindInt("u_light_type", lightType);
		}

		// glTF-standard skinning: skinned vertices reach world space via
		// Final = J_i W * ibm (Joint::GetFinalMatrix), so the mesh node's
		// own world transform must NOT be applied on top -- bind identity.
		const bool skinned = mesh->IsSkinnedMesh();
		if (skinned)
			shader->BindMatrix(Matrix4::WORLD_MATRIX, Matrix4());
		else
			shader->BindMatrix(Matrix4::WORLD_MATRIX, unit.worldMatrix);

		if (meshChanged)
		{
			if (skinned)
				shader->BindMesh(mesh, unit.skinPalette.data(),
					static_cast<int>(unit.skinPalette.size()));
			else
				shader->BindMesh(mesh);
		}

		// Per-instance LOD debug tint. When the LOD_DEBUG_COLORS switch is
		// on, push the active LOD's deterministic color onto the shader so
		// the fragment can replace/tint its output. When the switch is off,
		// we still bind the uniform -- but to vec4(0) -- because OpenGL
		// program objects retain their last-set uniform values indefinitely,
		// so a "do nothing" here would leave the previous frame's green
		// baked in. The shader's `lod_debug_color.a > 0.0` gate treats
		// alpha = 0 as "no override" and passes the diffuse through.
		// WITH_DBG_OVERLAY: headless builds included; stripped in Shipping.
#if WITH_DBG_OVERLAY
		if (packet.switches.test((size_t)PipelineSwitch::LOD_DEBUG_COLORS))
		{
			Color lodColor = GetLodDebugColor(static_cast<unsigned int>(unit.lodIndex));
			shader->BindFloat("lod_debug_color", lodColor.r, lodColor.g, lodColor.b, lodColor.a);
		}
		else
		{
			shader->BindFloat("lod_debug_color", 0.0f, 0.0f, 0.0f, 0.0f);
		}
#endif

		// Two-sided materials (foliage) disable backface culling for this
		// unit only; restored right after the draw so the pass's cull
		// mode still governs the next unit.
		const bool cullOff = material->GetTwoSided() || billboard;
		if (cullOff)
			glDisable(GL_CULL_FACE);

		if (mesh->GetSubMeshCount() > 0)
		{
			auto subMesh = mesh->GetSubMeshAt(drawSubMesh);
			if (subMesh == nullptr)
				return;
			shader->BindSubMesh(mesh, drawSubMesh);
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(subMesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(subMesh->Indices.Data.size()));
		}
		else
		{
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

			RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(mesh->Indices.Data.size()));
		}

		if (cullOff)
		{
			if (pass->GetCullMode() != CullMode::NONE)
			{
				glEnable(GL_CULL_FACE);
				glCullFace(EnumUtil::CullModeToUint(pass->GetCullMode()).second);
			}
		}

		if (skinned)
			RenderUtil::Instance()->IncreaseSkinnedMeshCount();
		else
			RenderUtil::Instance()->IncreaseMeshCount();

		RenderUtil::Instance()->IncreaseDrawCall();
	}

	void PrelightPipeline::DrawInstancedUnits(const std::shared_ptr<Pass> &pass, FramePacket &packet)
	{
		if (packet.instanced.empty())
			return;

		const bool useSSBO = InstancedMeshStreamer::Get().UseSSBO();
		auto &streamer = InstancedMeshStreamer::Get();

		for (auto &pk : packet.instanced)
		{
			auto baseMesh = pk.mesh;
			if (baseMesh == nullptr)
				continue;

			for (const auto &batch : pk.batches)
			{
				auto tierMesh = baseMesh->GetLodMesh(batch.LodTier);
				if (tierMesh == nullptr || batch.WorldMatrices.empty())
					continue;

				const unsigned int subCount = tierMesh->GetSubMeshCount();
				const unsigned int drawSlots = subCount > 0 ? subCount : 1;
				for (unsigned int sm = 0; sm < drawSlots; ++sm)
				{
					auto material = subCount > 0
						? (sm < pk.materials.size() ? pk.materials[sm] : nullptr)
						: (pk.materials.empty() ? nullptr : pk.materials[0]);
					// Billboard bucket: the quad's material comes from the
					// mesh (the component's slots key to LOD 0's submeshes).
					if (batch.Billboard)
						if (auto bbMat = baseMesh->GetBillboardMaterial())
							material = bbMat;
					if (material == nullptr)
						continue;

					// Cache-off escape hatch: straight to the uncached path.
					if (!kDrawCmdCache)
					{
						auto shader = ResolveUnitShader(pass, material, tierMesh, batch.Billboard, false);
						if (shader == nullptr)
							continue;
						shader->Bind();
						shader->BindCameraData(packet.camera);
						shader->BindMaterial(material);
						shader->BindFloat("u_alpha_cutoff",
							material->GetAlphaMode() == AlphaMode::MASK ? material->GetAlphaCutoff() : -1.0f);
						if (shader->GetTextureFlags() & (unsigned int)ShaderTexture::WIND)
						{
							shader->BindFloat("u_time", packet.engineTime);
							shader->BindFloat("u_wind_params", packet.windParams.x, packet.windParams.y,
								packet.windParams.z, packet.windParams.w);
						}
						const bool cullOff2 = material->GetTwoSided() || batch.Billboard;
						if (cullOff2)
							glDisable(GL_CULL_FACE);
						streamer.DrawInstanced(shader, tierMesh, subCount > 0 ? (int)sm : -1, batch.WorldMatrices);
						if (cullOff2 && pass->GetCullMode() != CullMode::NONE)
						{
							glEnable(GL_CULL_FACE);
							glCullFace(EnumUtil::CullModeToUint(pass->GetCullMode()).second);
						}
						continue;
					}

					// Draw-command cache per (component, tier, submesh).
					const std::uint64_t key = pk.nodeKey
						^ (static_cast<std::uint64_t>(batch.LodTier & 0xff) << 24)
						^ (static_cast<std::uint64_t>(sm + 1) << 40)
						^ (static_cast<std::uint64_t>(pass->GetRenderIndex() & 0xff) << 48)
						^ (useSSBO ? (1ull << 56) : 0);
					auto &cmd = m_DrawCommandCache[key];

					if (cmd.shader == nullptr || cmd.material != material || cmd.mesh != tierMesh
						|| cmd.materialVersion != material->GetRenderVersion())
					{
						++m_CacheRebuilds;

						unsigned int textureFlags = material->GetTextureFlags();
						if (material->GetAlphaMode() == AlphaMode::MASK)
							textureFlags |= (unsigned int)ShaderTexture::ALPHA_TEST;
						if (material->GetTwoSided())
							textureFlags |= (unsigned int)ShaderTexture::TWO_SIDED;
						if (material->GetWindEnabled())
							textureFlags |= (unsigned int)ShaderTexture::WIND;
						if (batch.Billboard)
							textureFlags |= (unsigned int)ShaderTexture::BILLBOARD |
								(unsigned int)ShaderTexture::TWO_SIDED |
								(unsigned int)ShaderTexture::ALPHA_TEST;
						textureFlags |= (unsigned int)ShaderTexture::INSTANCED;
						if (useSSBO)
							textureFlags |= (unsigned int)ShaderTexture::INSTANCE_SSBO;

						// Fallback order: drop the SSBO bit (divisor variant of
						// the same shader), then wind, then the remaining
						// vegetation bits. INSTANCED is never dropped -- a
						// non-instanced shader would draw the whole batch at
						// one transform.
						auto shader = pass->GetShader(ShaderType::STATIC_MESH, textureFlags);
						if (shader == nullptr && (textureFlags & (unsigned int)ShaderTexture::INSTANCE_SSBO))
							shader = pass->GetShader(ShaderType::STATIC_MESH,
								textureFlags & ~(unsigned int)ShaderTexture::INSTANCE_SSBO);
						if (shader == nullptr)
							shader = pass->GetShader(ShaderType::STATIC_MESH,
								textureFlags & ~(unsigned int)ShaderTexture::INSTANCE_SSBO & ~(unsigned int)ShaderTexture::WIND);
						if (shader == nullptr)
							shader = pass->GetShader(ShaderType::STATIC_MESH,
								textureFlags & ~(unsigned int)ShaderTexture::INSTANCE_SSBO &
									~(unsigned int)ShaderTexture::TWO_SIDED & ~(unsigned int)ShaderTexture::WIND);
						if (shader == nullptr)
						{
							FURYW << "Failed to draw instanced " << pk.nodeKey << ", shader not found!";
							cmd = DrawCommand();
							continue;
						}

						cmd = DrawCommand();
						cmd.shader = shader;
						cmd.material = material;
						cmd.mesh = tierMesh;
						cmd.materialVersion = material->GetRenderVersion();
						cmd.subMesh = subCount > 0 ? (int)sm : -1;
						cmd.cullOff = material->GetTwoSided() || batch.Billboard;
						cmd.wind = (shader->GetTextureFlags() & (unsigned int)ShaderTexture::WIND) != 0;
						cmd.alphaCutoff = material->GetAlphaMode() == AlphaMode::MASK ? material->GetAlphaCutoff() : -1.0f;

						for (const auto &kv : material->GetTextures())
							cmd.textureBinds.push_back({ shader->GetUniformLocation(kv.first), kv.second });
						for (const auto &kv : material->GetUniforms())
							cmd.uniformBinds.push_back({ shader->GetUniformLocation(kv.first), kv.second });
						for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
						{
							auto ptr = pass->GetTextureAt(i, true);
							cmd.passTextureBinds.push_back({ shader->GetUniformLocation(ptr->GetName()), ptr });
						}
						for (const auto &tb : cmd.passTextureBinds)
							if (tb.location != -1 && tb.texture != nullptr)
								++cmd.passBoundCount;

						cmd.alphaCutoffLoc = shader->GetUniformLocation("u_alpha_cutoff");
						cmd.timeLoc = shader->GetUniformLocation("u_time");
						cmd.windParamsLoc = shader->GetUniformLocation("u_wind_params");
						cmd.lodDebugLoc = shader->GetUniformLocation("lod_debug_color");
					}
					else
					{
						++m_CacheHits;
					}

					ReplayDrawCommand(cmd, pass, nullptr, packet);

#if WITH_DBG_OVERLAY
					// LOD debug tint per tier (billboard tier gets its own
					// palette slot via its tier index).
					if (packet.switches.test((size_t)PipelineSwitch::LOD_DEBUG_COLORS))
					{
						Color lodColor = GetLodDebugColor(batch.LodTier);
						cmd.shader->BindFloatLocation(cmd.lodDebugLoc, lodColor.r, lodColor.g, lodColor.b, lodColor.a);
					}
					else
					{
						cmd.shader->BindFloatLocation(cmd.lodDebugLoc, 0.0f, 0.0f, 0.0f, 0.0f);
					}
#endif

					if (cmd.cullOff)
						glDisable(GL_CULL_FACE);

					streamer.DrawInstanced(cmd.shader, tierMesh, subCount > 0 ? (int)sm : -1, batch.WorldMatrices);

					if (cmd.cullOff && pass->GetCullMode() != CullMode::NONE)
					{
						glEnable(GL_CULL_FACE);
						glCullFace(EnumUtil::CullModeToUint(pass->GetCullMode()).second);
					}
				}
			}
		}
	}

	void PrelightPipeline::DrawPointLight(const std::shared_ptr<Pass> &pass, FramePacket &packet, int lightIndex)
	{
		FURY_ZONE;
		const auto &pl = packet.lights[lightIndex];
		auto mesh = pl.volumeMesh;
		auto worldMatrix = pl.worldMatrix;

		Shader::Ptr shader = nullptr;
		bool castShadows = pl.castShadows;

		// find correct shader.
		shader = GetShaderByName(castShadows ? "pointlight_shadow_shader" : "pointlight_shader");
		if (shader == nullptr)
		{
			FURYW << "Shader for light " << pl.name << " not found!";
			return;
		}

		// draw shadowMap if we castShadows.
		std::pair<Texture::Ptr, Matrix4> shadowData;
		if (castShadows)
			shadowData = DrawPointLightShadowMap(packet, lightIndex);

		// ready to draw light volumn
		pass->Bind(false);

		// change depthTest && face culling state.
		{
			float camNear = (packet.camera.frustum.GetCurrentCorners()[0] - packet.camera.worldPos).Length();
			if (SphereBounds(pl.worldPos, pl.effectiveRadius + camNear).IsInsideFast(packet.camera.worldPos))
			{
				glDisable(GL_DEPTH_TEST);
				glCullFace(GL_FRONT);
			}
			else
			{
				glEnable(GL_DEPTH_TEST);
				glCullFace(GL_BACK);
			}

			worldMatrix.AppendScale(Vector4(pl.radius, 0.0f));
		}

		shader->Bind();

		shader->BindCameraData(packet.camera);
		shader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);

		if (castShadows && shadowData.first != nullptr)
		{
			shader->BindTexture("shadow_buffer", shadowData.first);
			shader->BindMatrix("shadow_matrix", &shadowData.second.Raw[0]);
		}

		shader->BindLightData(pl);
		shader->BindMesh(mesh);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer (released at end of ExecutePacket --
		// the transparent pass samples it for shadow-receiving)
		if (castShadows)
		{
			packet.frameShadowTemps.push_back(shadowData.first);
			packet.shadowResults[lightIndex].texture = shadowData.first;
			packet.shadowResults[lightIndex].single = shadowData.second;
		}
	}

	void PrelightPipeline::DrawDirLight(const std::shared_ptr<Pass> &pass, FramePacket &packet, int lightIndex)
	{
		FURY_ZONE;
		const auto &pl = packet.lights[lightIndex];
		auto mesh = pl.volumeMesh;
		auto worldMatrix = pl.worldMatrix;

		Shader::Ptr shader = nullptr;
		bool castShadows = pl.castShadows;
		bool useCascaded = packet.switches.test((size_t)PipelineSwitch::CASCADED_SHADOW_MAP);

		// find correct shader.
		shader = GetShaderByName(castShadows ?
			(useCascaded ? "dirlight_csm_shader" : "dirlight_shadow_shader") : "dirlight_shader");
		if (shader == nullptr)
		{
			FURYW << "Shader for light " << pl.name << " not found!";
			return;
		}

		// draw shadowMap if we castShadows.
		std::pair<Texture::Ptr, std::vector<Matrix4>> cascadedShadowData;
		std::pair<Texture::Ptr, Matrix4> shadowData;
		if (castShadows)
		{
			if (useCascaded)
				cascadedShadowData = DrawCascadedShadowMap(packet, lightIndex);
			else
				shadowData = DrawDirLightShadowMap(packet, lightIndex);
		}

		// ready to draw light volumn
		pass->Bind(false);

		// change depthTest && face culling state.
		glEnable(GL_DEPTH_TEST);
		glCullFace(GL_BACK);

		shader->Bind();

		shader->BindCameraData(packet.camera);
		shader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);

		if (castShadows)
		{
			if (useCascaded && cascadedShadowData.first != nullptr)
			{
				shader->BindTexture("shadow_buffer", cascadedShadowData.first);
				// for cacasded shadow maps
				shader->BindMatrices("shadow_matrix", static_cast<int>(cascadedShadowData.second.size()), &cascadedShadowData.second[0]);
				// split distances from the same source the map render used
				shader->BindFloat("shadow_far", packet.csmSplits[0], packet.csmSplits[1],
					packet.csmSplits[2], packet.csmSplits[3]);
			}
			else if (shadowData.first != nullptr)
			{
				shader->BindTexture("shadow_buffer", shadowData.first);
				shader->BindMatrix("shadow_matrix", &shadowData.second.Raw[0]);
			}
		}

		shader->BindLightData(pl);
		shader->BindMesh(mesh);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer (released at end of ExecutePacket);
		// cache the matrix data for the transparent pass's
		// shadow-receive path. CSM: the 4 cascade matrices +
		// shadow_far (linear-quarter-far split, matches the
		// deferred SunLight.glsl CSM block).
		if (castShadows)
		{
			auto &sr = packet.shadowResults[lightIndex];
			if (useCascaded && cascadedShadowData.first != nullptr)
			{
				packet.frameShadowTemps.push_back(cascadedShadowData.first);
				sr.texture = cascadedShadowData.first;
				sr.csm = cascadedShadowData.second;
				float base = packet.camera.farClip - packet.camera.nearClip;
				float avg = base / 4.0f;
				sr.shadowFar = Vector4(-avg, -avg * 2, -avg * 3, -avg * 4);
			}
			else if (shadowData.first != nullptr)
			{
				packet.frameShadowTemps.push_back(shadowData.first);
				sr.texture = shadowData.first;
				sr.single = shadowData.second;
			}
		}
	}

	void PrelightPipeline::DrawSpotLight(const std::shared_ptr<Pass> &pass, FramePacket &packet, int lightIndex)
	{
		FURY_ZONE;
		const auto &pl = packet.lights[lightIndex];
		auto mesh = pl.volumeMesh;
		auto worldMatrix = pl.worldMatrix;

		Shader::Ptr shader = nullptr;
		bool castShadows = pl.castShadows;

		// find correct shader.
		shader = GetShaderByName(castShadows ? "spotlight_shadow_shader" : "spotlight_shader");
		if (shader == nullptr)
		{
			FURYW << "Shader for light " << pl.name << " not found!";
			return;
		}

		// draw shadowMap if we castShadows.
		std::pair<Texture::Ptr, Matrix4> shadowData;
		if (castShadows)
			shadowData = DrawSpotLightShadowMap(packet, lightIndex);

		// ready to draw light volumn
		pass->Bind(false);

		// change depthTest && face culling state.
		{
			auto coneCenter = pl.worldPos;
			auto coneDir = pl.worldDir;

			float camNear = (packet.camera.frustum.GetCurrentCorners()[0] - packet.camera.worldPos).Length();
			float theta = pl.outterAngle * 0.5f;
			float height = pl.effectiveRadius;
			float extra = camNear / std::sin(theta);

			coneCenter = coneCenter - coneDir * extra;
			height += camNear + extra;

			if (MathUtil::PointInCone(coneCenter, coneDir, height, theta, packet.camera.worldPos))
			{
				glDisable(GL_DEPTH_TEST);
				glCullFace(GL_FRONT);
			}
			else
			{
				glEnable(GL_DEPTH_TEST);
				glCullFace(GL_BACK);
			}
		}

		shader->Bind();

		shader->BindCameraData(packet.camera);
		shader->BindMatrix(Matrix4::WORLD_MATRIX, worldMatrix);

		if (castShadows && shadowData.first != nullptr)
		{
			shader->BindTexture("shadow_buffer", shadowData.first);
			shader->BindMatrix("shadow_matrix", &shadowData.second.Raw[0]);
		}

		shader->BindLightData(pl);
		shader->BindMesh(mesh);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseLightCount();

		pass->UnBind();

		// collect used shadow buffer (released at end of ExecutePacket);
		// cache the spot view->shadow UV matrix for the transparent
		// pass + particle block.
		if (castShadows && shadowData.first != nullptr)
		{
			packet.frameShadowTemps.push_back(shadowData.first);
			auto &sr = packet.shadowResults[lightIndex];
			sr.texture = shadowData.first;
			sr.single = shadowData.second;
		}
	}

	void PrelightPipeline::DrawQuad(const std::shared_ptr<Pass> &pass, const FramePacket &packet)
	{
		auto shader = m_CurrentShader;
		auto mesh = MeshUtil::GetUnitQuad();

		if (shader == nullptr)
		{
			FURYW << "Failed to draw full screen quad, shader not found!";
			return;
		}

		shader->Bind();

		shader->BindMesh(mesh);
		shader->BindCameraData(packet.camera);

		// When rendering into the editor's offscreen viewport RT (a
		// non-sRGB RGBA8 FBO), GL_FRAMEBUFFER_SRGB is a no-op, so the
		// lambert shader gamma-encodes its output itself to keep the
		// viewport from rendering too dark. The default-framebuffer path
		// leaves this 0 and lets GL_FRAMEBUFFER_SRGB do the encoding.
		// Only screen-bound passes (no output textures) encode --
		// intermediate composites (e.g. LDR pass_combine -> ldr_composite)
		// must stay linear.
		shader->BindInt("u_gamma_correct",
			(packet.renderTarget != nullptr && pass->GetTextureCount(false) == 0) ? 1 : 0);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		// Aerial-perspective bindings for the combine pass (no-ops on
		// shaders without these uniforms). Sampler always bound: dummy 3D
		// when no sky is active.
		if (packet.hdrMode && packet.sky.valid && packet.sky.enabled && packet.sky.cameraVolume != nullptr)
		{
			shader->BindInt("u_atmosphere_enabled", 1);
			shader->BindFloat("u_ap_range", packet.sky.params.apRangeKm);
			shader->BindTexture("u_ap_volume", packet.sky.cameraVolume);
			// small sky-ambient lift while a sky drives the scene: keeps
			// away-facing slopes from crushing to pure black (no IBL)
			shader->BindFloat("u_ambient", 0.03f + 0.05f * packet.sky.params.daylight);
		}
		else
		{
			shader->BindInt("u_atmosphere_enabled", 0);
			shader->BindTexture("u_ap_volume", GetDummyTexture3D());
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(mesh->Indices.Data.size()));
	}

	void PrelightPipeline::DrawSky(const std::shared_ptr<Pass> &pass, FramePacket &packet)
	{
		FURY_ZONE;
		// HDR-only feature; no sky -> no draw, hdr_composite untouched.
		if (!packet.hdrMode)
			return;
		if (!packet.sky.valid || !packet.sky.enabled || !packet.sky.owner)
			return;
		auto shader = m_CurrentShader != nullptr ? m_CurrentShader : pass->GetFirstShader();
		if (shader == nullptr || !packet.camera.valid)
			return;

		// LUT/volume/cloud renders bind their own FBOs, so this runs before
		// the pass bind.
		packet.sky.owner->EnsureLutsRender(packet.sky.params, packet.camera);

		pass->Bind(false);   // never clear: hdr_composite holds the scene
		shader->Bind();

		auto mesh = MeshUtil::GetUnitQuad();
		shader->BindMesh(mesh);
		shader->BindCameraData(packet.camera);
		packet.sky.owner->BindAtmosphereUniforms(shader, packet.sky.params);

		for (unsigned int i = 0; i < pass->GetTextureCount(true); i++)
		{
			auto ptr = pass->GetTextureAt(i, true);
			shader->BindTexture(ptr->GetName(), ptr);
		}

		// LUT textures are read from the owner post-EnsureLutsRender: both
		// run on this (GL) thread, so the members are stable here.
		shader->BindTexture("u_skyview_lut", packet.sky.owner->GetSkyViewLut());
		shader->BindTexture("u_transmittance_lut", packet.sky.owner->GetTransmittanceLut());
		shader->BindTexture("u_cloud_tex", packet.sky.params.cloudsEnabled
			? packet.sky.owner->GetCloudTarget() : GetDummyTexture2D());
		shader->BindTexture("u_moon_tex", packet.sky.owner->GetMoonTexture()
			? packet.sky.owner->GetMoonTexture() : GetDummyTexture2D());

		shader->BindFloat("u_sun_ang_cos", cosf(packet.sky.params.sunAngularRadius));
		shader->BindFloat("u_sun_disc_intensity", packet.sky.params.sunDiscIntensity);
		shader->BindFloat("u_moon_dir", packet.sky.params.moonDir.x, packet.sky.params.moonDir.y, packet.sky.params.moonDir.z);
		shader->BindFloat("u_moon_ang_cos", cosf(packet.sky.params.moonAngularRadius));
		shader->BindFloat("u_moon_frame_scale", 1.0f / tanf(packet.sky.params.moonAngularRadius));
		shader->BindFloat("u_moon_intensity", packet.sky.params.moonIntensity);
		shader->BindInt("u_moon_enabled", packet.sky.params.moonEnabled && packet.sky.owner->GetMoonTexture() ? 1 : 0);
		shader->BindInt("u_clouds_enabled", packet.sky.params.cloudsEnabled ? 1 : 0);

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();

		RenderUtil::Instance()->IncreaseDrawCall();
	}

	void PrelightPipeline::DrawOcean(const std::shared_ptr<Pass> &pass, FramePacket &packet)
	{
		FURY_ZONE;
		// HDR-only feature (same constraint as the sky pass).
		if (!packet.hdrMode)
			return;
		if (!packet.camera.valid || packet.oceans.empty())
			return;
		auto shader = m_CurrentShader != nullptr ? m_CurrentShader : pass->GetFirstShader();
		if (shader == nullptr)
			return;

		// Pre-pass depth copy: shore foam samples the opaque scene depth,
		// which is also this pass's depth attachment (sampling the bound
		// attachment would be a feedback loop). Depth copies go through
		// glBlitFramebuffer (RenderUtil::Blit is a color draw).
		Texture::Ptr depthCopy;
		auto depthSrc = GetTextureByName("gbuffer_depth");
		if (depthSrc)
		{
			depthCopy = Texture::GetTemporary(depthSrc->GetWidth(), depthSrc->GetHeight(),
				1, depthSrc->GetFormat(), TextureType::TEXTURE_2D);

			static unsigned int s_DepthCopyFbos[2] = { 0, 0 };
			if (s_DepthCopyFbos[0] == 0)
				glGenFramebuffers(2, s_DepthCopyFbos);
			int w = depthSrc->GetWidth();
			int h = depthSrc->GetHeight();
			glBindFramebuffer(GL_READ_FRAMEBUFFER, s_DepthCopyFbos[0]);
			glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthSrc->GetID(), 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_DepthCopyFbos[1]);
			glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthCopy->GetID(), 0);
			glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}

		pass->Bind(false); // never clear: hdr_composite holds the lit scene
		shader->Bind();
		shader->BindCameraData(packet.camera);

		// per-buffer blend: color0 (hdr_composite) alpha-blends; color1
		// (gbuffer_normal) must stay replace or normals get smeared.
		// glEnablei/glDisablei are GL 3.0 core; per-buffer blend FUNC
		// (glBlendFunci) is 4.0-only, so the func stays global and
		// buffer 1 simply keeps blending disabled.
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnablei(GL_BLEND, 0);
		glDisablei(GL_BLEND, 1);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);

		// note: the GetDummyTexture* helpers are free functions in
		// namespace fury (RenderUtil.cpp), not RenderUtil members
		if (depthCopy)
			shader->BindTexture("u_scene_depth", depthCopy);
		else
			shader->BindTexture("u_scene_depth", GetDummyTexture2D());
		shader->BindFloat("u_rt_size", (float)pass->GetTextureAt(0, false)->GetWidth(),
			(float)pass->GetTextureAt(0, false)->GetHeight());

		// dominant directional light for the sun term (BindLight uniforms)
		int sunIndex = -1;
		for (int li = 0; li < (int)packet.lights.size(); ++li)
		{
			if (packet.lights[li].type == LightType::DIRECTIONAL)
			{
				sunIndex = li;
				break;
			}
		}
		if (sunIndex >= 0)
		{
			shader->BindLightData(packet.lights[sunIndex]);
			shader->BindInt("u_light_valid", 1);
		}
		else
		{
			shader->BindInt("u_light_valid", 0);
		}

		// CSM shadow-receive from the frame's sun map (transparent
		// pass reads the same caches)
		int shadowType = 0;
		if (sunIndex >= 0)
		{
			const auto &sr = packet.shadowResults[sunIndex];
			if (sr.texture && sr.csm.size() == 4)
			{
				shader->BindTexture("shadow_buffer_csm", sr.texture);
				shader->BindMatrices("shadow_matrix_csm", 4, sr.csm.data());
				shader->BindFloat("shadow_far", sr.shadowFar.x,
					sr.shadowFar.y, sr.shadowFar.z, sr.shadowFar.w);
				shadowType = 3;
			}
		}
		if (shadowType == 0)
			shader->BindTexture("shadow_buffer_csm", GetDummyTexture2DArray());
		shader->BindInt("u_shadow_type", shadowType);

		// aerial perspective volume (same source as PbrCombine)
		if (packet.sky.valid && packet.sky.enabled && packet.sky.owner)
		{
			// post-EnsureLutsRender members (same thread): valid once the
			// sky pass ran this frame; dummy fallbacks otherwise.
			auto camVolume = packet.sky.owner->GetCameraVolume();
			auto skyView = packet.sky.owner->GetSkyViewLut();
			shader->BindTexture("u_ap_volume", camVolume ? camVolume : GetDummyTexture3D());
			shader->BindTexture("u_skyview_lut", skyView ? skyView : GetDummyTexture2D());
			shader->BindInt("u_atmosphere_enabled", camVolume ? 1 : 0);
			shader->BindFloat("u_ap_range", packet.sky.params.apRangeKm);
			shader->BindFloat("u_bottom_radius", packet.sky.params.bottomRadiusKm);
			shader->BindFloat("u_view_height", packet.sky.params.viewHeightKm);
			// moonlight: the sun light dims to zero at night, but the water
			// should keep a cool moon glint (diffuse + a capped spec path)
			shader->BindFloat("u_moon_dir", packet.sky.params.moonDir.x,
				packet.sky.params.moonDir.y, packet.sky.params.moonDir.z);
			shader->BindFloat("u_moon_intensity", packet.sky.params.moonEnabled
				? packet.sky.params.moonIntensity : 0.0f);
		}
		else
		{
			shader->BindTexture("u_ap_volume", GetDummyTexture3D());
			shader->BindInt("u_atmosphere_enabled", 0);
			shader->BindTexture("u_skyview_lut", GetDummyTexture2D());
			shader->BindFloat("u_bottom_radius", 6360.0f);
			shader->BindFloat("u_view_height", 0.0f);
			shader->BindFloat("u_moon_dir", 0.0f, -1.0f, 0.0f);
			shader->BindFloat("u_moon_intensity", 0.0f);
		}

		Vector4 camPos = packet.camera.worldPos;
		// camera-radial band fades are centered on the camera (VS)
		shader->BindFloat("u_cam_xz", camPos.x, camPos.z);
		for (const auto &po : packet.oceans)
		{
			auto waves = po.waves;
			bool valid = waves && waves->IsValid();

			shader->BindFloat("u_water_level", po.nodePos.y + po.waterLevel);
			shader->BindFloat("u_time", po.waveTime);
			shader->BindInt("u_waves_valid", valid ? 1 : 0);

			if (valid)
			{
				// roles by tile (bands are sorted tile-ascending at load):
				// swell = largest, chop = smallest when a 3rd cascade exists,
				// ripple = the one in between. The chop band contributes
				// normals + foam only (no vertex displacement).
				const int bandCount = waves->GetBandCount();
				const auto &swellBand = waves->GetBand(bandCount - 1);
				const auto &rippleBand = waves->GetBand(bandCount >= 3 ? 1 : 0);
				const bool hasChop = bandCount >= 3;
				const auto &chopBand = waves->GetBand(0);

				shader->BindFloat("u_loop_seconds", waves->GetLoopSeconds());
				shader->BindFloat("u_frames", (float)waves->GetFrameCount());
				shader->BindFloat("u_swell_tile", swellBand.TileCm);
				shader->BindFloat("u_ripple_tile", rippleBand.TileCm);
				shader->BindTexture("u_disp_swell", swellBand.DispTexture);
				shader->BindTexture("u_disp_ripple", rippleBand.DispTexture);
				shader->BindTexture("u_nrm_swell", swellBand.NrmTexture);
				shader->BindTexture("u_nrm_ripple", rippleBand.NrmTexture);
				shader->BindInt("u_chop_valid", hasChop ? 1 : 0);
				if (hasChop)
				{
					shader->BindFloat("u_chop_tile", chopBand.TileCm);
					shader->BindTexture("u_nrm_chop", chopBand.NrmTexture);
				}
				else
				{
					shader->BindFloat("u_chop_tile", 300.0f);
					shader->BindTexture("u_nrm_chop", GetDummyTexture2DArray());
				}
			}
			else
			{
				shader->BindFloat("u_loop_seconds", 12.0f);
				shader->BindFloat("u_frames", 32.0f);
				shader->BindFloat("u_swell_tile", 10000.0f);
				shader->BindFloat("u_ripple_tile", 800.0f);
				shader->BindTexture("u_disp_swell", GetDummyTexture2DArray());
				shader->BindTexture("u_disp_ripple", GetDummyTexture2DArray());
				shader->BindTexture("u_nrm_swell", GetDummyTexture2DArray());
				shader->BindTexture("u_nrm_ripple", GetDummyTexture2DArray());
				shader->BindInt("u_chop_valid", 0);
				shader->BindFloat("u_chop_tile", 300.0f);
				shader->BindTexture("u_nrm_chop", GetDummyTexture2DArray());
			}

			shader->BindFloat("u_absorb_color", po.absorb.r, po.absorb.g, po.absorb.b);
			shader->BindFloat("u_scatter_color", po.scatter.r, po.scatter.g, po.scatter.b);
			shader->BindFloat("u_roughness", po.roughness);
			// always the true roughness: the SSAO water gate reads the same
			// gbuffer alpha as SSR (SSR's own roughness < 0.95 gate is
			// unaffected), so SSR-off water must not write 1.0 here
			shader->BindFloat("u_ssr_roughness", po.roughness);
			shader->BindFloat("u_normal_strength", po.normalStrength);
			shader->BindFloat("u_foam_amount", po.foamAmount);
			shader->BindFloat("u_shore_foam_depth", po.shoreFoamDepthCm);
			shader->BindFloat("u_wind_speed", po.windSpeed);
			shader->BindInt("u_debug_view", (int)po.debugView);
			shader->BindFloat("u_disp_debug_scale", 0.02f);

			// camera-radial band fades: ranges derive from the ring radii;
			// the legacy per-piece uniforms survive as multipliers (1 = on)
			shader->BindFloat("u_fade_ranges", po.fadeRanges.x, po.fadeRanges.y,
				po.fadeRanges.z, po.fadeRanges.w);
			shader->BindFloat("u_swell_fade", 1.0f);
			shader->BindFloat("u_ripple_fade", 1.0f);

			// distance fog converges far water to the sky horizon color;
			// fully fogged at the skirt radius so the far edge never reads.
			// The start follows the last ring's outer radius (never below
			// 1 km): with more rings the detailed band reaches further, and
			// a fixed start left a hard fog band against it.
			float fogStart = std::max(100000.0f, po.fadeRanges.w);
			float fogEnd = std::max(po.skirtRadiusCm, fogStart * 1.01f);
			shader->BindFloat("u_fog_start", fogStart);
			shader->BindFloat("u_fog_end", fogEnd);

			// debug view 3: ring-LOD wireframe via polygon mode (no CPU
			// line lists; restored right after this ocean's draws)
			bool wireframe = po.debugView == 3;
			if (wireframe)
				glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

			if (po.finite)
			{
				auto mesh = po.finiteMesh;
				if (!mesh)
					continue;
				shader->BindFloat("u_world_origin", po.nodePos.x, po.nodePos.y, po.nodePos.z);
				shader->BindFloat("u_y_offset", 0.0f);
				shader->BindFloat("u_debug_color", 0.2f, 0.9f, 0.9f);
				mesh->UpdateBuffer();
				shader->BindMesh(mesh);
				glDrawElements(GL_TRIANGLES, (GLsizei)mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);
				RenderUtil::Instance()->IncreaseDrawCall();
			}
			else
			{
				// per-piece wireframe colors: center white, rings cycle, skirt gray
				static const float kPieceColors[][3] = {
					{ 1.0f, 1.0f, 1.0f }, { 0.95f, 0.6f, 0.2f }, { 0.3f, 0.9f, 0.4f },
					{ 0.35f, 0.6f, 1.0f }, { 0.9f, 0.4f, 0.9f }, { 0.5f, 0.5f, 0.5f }
				};
				int pieceIndex = 0;
				for (const auto &piece : po.pieces)
				{
					if (!piece.mesh)
						continue;
					const float *pc = kPieceColors[std::min<int>(piece.isSkirt ? 5 : pieceIndex, 5)];
					shader->BindFloat("u_world_origin", piece.origin.x, piece.origin.y, piece.origin.z);
					shader->BindFloat("u_y_offset", piece.yOffset);
					shader->BindFloat("u_debug_color", pc[0], pc[1], pc[2]);
					piece.mesh->UpdateBuffer();
					shader->BindMesh(piece.mesh);
					glDrawElements(GL_TRIANGLES, (GLsizei)piece.mesh->Indices.Data.size(), GL_UNSIGNED_INT, 0);
					RenderUtil::Instance()->IncreaseDrawCall();
					++pieceIndex;
				}
			}

			if (wireframe)
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		}

		// restore: blend/depth state leaves with the pass
		glDisablei(GL_BLEND, 0);
		glDepthMask(GL_TRUE);
		shader->UnBind();

		if (depthCopy)
			Texture::ReleaseTemporary(depthCopy);
	}

	std::shared_ptr<Texture> PrelightPipeline::GetLightingOutputTexture(bool hdrMode) const
	{
		// hdr_composite (HDR) or ldr_composite (LDR), with fallbacks
		// for legacy pipelines that predate the composite textures.
		Texture::Ptr sourceTex = nullptr;
		if (hdrMode)
		{
			sourceTex = GetTextureByName("hdr_composite");
			if (!sourceTex) sourceTex = GetTextureByName("hdr_light");
			if (!sourceTex) sourceTex = GetTextureByName("gbuffer_light");
		}
		else
		{
			sourceTex = GetTextureByName("ldr_composite");
			if (!sourceTex) sourceTex = GetTextureByName("gbuffer_light");
			if (!sourceTex) sourceTex = GetTextureByName("hdr_light");
		}
		return sourceTex;
	}

	void PrelightPipeline::RunPostProcessChain(FramePacket &packet)
	{
		FURY_ZONE;
		if (packet.chain.empty()) return;
		if (!packet.camera.valid) return;

		// Chain input: the pipeline's lighting output texture.
		Texture::Ptr sourceTex = GetLightingOutputTexture(packet.hdrMode);
		if (!sourceTex)
		{
			FURYW << "RunPostProcessChain: no lighting output texture "
					 "found (gbuffer_light/hdr_composite) -- chain skipped";
			return;
		}

		// Ping-pong texture dims match the source. Chain effects
		// produce a same-size intermediate.
		const int W = sourceTex->GetWidth() > 0 ? sourceTex->GetWidth() : 1280;
		const int H = sourceTex->GetHeight() > 0 ? sourceTex->GetHeight() : 720;
		const TextureFormat fmt = sourceTex->GetFormat();

		// Ping-pong via the temp pool: fresh temp per intermediate
		// step, each read texture released after its consuming draw --
		// read and write never alias, nothing leaks.
		Texture::Ptr readTex = sourceTex;

		auto quad = MeshUtil::GetUnitQuad();
		const bool toRT = (packet.renderTarget != nullptr && packet.renderTarget->IsAllocated());

		// A throwaway Pass we configure once to host each temp
		// texture as an FBO color attachment. Pass owns the FBO +
		// viewport wiring so we don't have to repeat the GL calls
		// per effect.
		auto chainPass = Pass::Create("ChainFBO");
		chainPass->SetBlendMode(BlendMode::REPLACE);
		chainPass->SetClearMode(ClearMode::COLOR);
		chainPass->SetClearColor(Color(0, 0, 0, 1));

		// Final-write target: editor offscreen RT or default FB. We
		// bind one of these manually as the FBO color attachment
		// for the last effect.
		GLint prev_fbo = 0;
		GLint prev_vp[4] = {0, 0, 0, 0};
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
		glGetIntegerv(GL_VIEWPORT, prev_vp);

		auto bindScreenFBO = [&]()
		{
			if (toRT)
			{
				glBindFramebuffer(GL_FRAMEBUFFER, packet.renderTarget->GetFBO());
				glViewport(0, 0, packet.renderTarget->GetWidth(), packet.renderTarget->GetHeight());
			}
			else
			{
				// Final blit: track the live window, not the source's 1280x720.
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glViewport(0, 0, packet.windowW > 0 ? packet.windowW : W,
					packet.windowH > 0 ? packet.windowH : H);
			}
		};

		for (size_t i = 0; i < packet.chain.size(); ++i)
		{
			auto &effect = packet.chain[i];
			if (!effect) continue;
			const bool isLast = (i + 1 == packet.chain.size());

			// Compile shader on first use (cached by path+mode); LDR variants get the `LDR` define via a `|ldr`-suffixed key so effects can branch on HDR-only data -- e.g. SSR falls back to u_ldr_roughness when Lambert packs no roughness in normal.a.
			std::string shaderKey = effect->GetShaderPath();
			if (!packet.hdrMode)
				shaderKey += "|ldr";
			auto shader = GetShaderByName(shaderKey);
			if (!shader)
			{
				shader = Shader::Create(shaderKey, ShaderType::OTHER);
				for (const auto &d : effect->GetShaderDefines())
					shader->AddDefine(d);
				if (!packet.hdrMode)
					shader->AddDefine("LDR");
				if (!shader->LoadAndCompile(effect->GetShaderPath()))
				{
					FURYE << "RunPostProcessChain: failed to compile effect '"
						  << effect->GetName() << "' (" << effect->GetShaderPath() << ")";
					continue;
				}
				m_EntityManager->Add(shader);
			}

			// Resolve inputs before acquiring the write target so a
			// missing reserved texture skips the effect without leaking
			// a temp. `$`-prefixed names are reserved pipeline textures
			// bound read-only ($scene = previous chain output,
			// $gbuffer_* = G-buffer, $hdr_light/$ldr_composite =
			// lighting targets); plain names keep the legacy behavior
			// (previous output). Sampler uniform = name without `$`.
			std::vector<std::pair<std::string, Texture::Ptr>> resolvedInputs;
			bool missingInput = false;
			for (const auto &inputName : effect->GetInputs())
			{
				if (!inputName.empty() && inputName[0] == '$')
				{
					// $-prefix resolves against named textures (or the current frame); bare names reuse the stage output.
					Texture::Ptr reserved = (inputName == "$scene")
						? readTex : GetTextureByName(inputName.substr(1));
					if (!reserved)
					{
						FURYW << "RunPostProcessChain: effect '" << effect->GetName()
							  << "' input '" << inputName << "' not found -- effect skipped";
						missingInput = true;
						break;
					}
					resolvedInputs.emplace_back(inputName.substr(1), reserved);
				}
				else
				{
					resolvedInputs.emplace_back(inputName, readTex);
				}
			}
			if (missingInput)
				continue;

			// Final write goes to the screen/RT directly; earlier
			// steps write into a freshly-acquired temp via chainPass.
			int writeW, writeH;
			Texture::Ptr writeTex = nullptr;
			if (isLast)
			{
				bindScreenFBO();
				if (toRT)
				{
					writeW = packet.renderTarget->GetWidth();
					writeH = packet.renderTarget->GetHeight();
				}
				else
				{
					// u_rt_size mirrors the live viewport (FXAA/CRT need it).
					writeW = packet.windowW > 0 ? packet.windowW : W;
					writeH = packet.windowH > 0 ? packet.windowH : H;
				}
			}
			else
			{
				writeTex = Texture::GetTemporary(W, H, 0, fmt, TextureType::TEXTURE_2D);
				chainPass->RemoveAllTextures();
				chainPass->AddTexture(writeTex, false);
				chainPass->Bind(false);
				writeW = W;
				writeH = H;
			}

			shader->Bind();
			shader->BindMesh(quad);
			shader->BindCameraData(packet.camera);

			// Chain draws are full-screen REPLACES -- never inherit
			// pipeline state. Without this, the screen-bound final
			// effect runs with whatever the last pass left behind:
			// pass_transparent exits with GL_BLEND + glBlendFunc(ONE,
			// ONE) from its additive light loop, so a single-effect
			// chain (e.g. [ACES]) ADDED its output into the render
			// target every frame -- the "disable FXAA -> progressive
			// overexposure" bug. Multi-effect chains only looked clean
			// because the intermediate chainPass->Bind(REPLACE) reset
			// the blend state before the final draw.
			glDisable(GL_BLEND);
			glDisable(GL_DEPTH_TEST);
			glDepthMask(GL_FALSE);
			glDisable(GL_CULL_FACE);

			// u_rt_size: pixels (FXAA + CRT need it for fwidth /
			// scanline frequency).
			shader->BindFloat("u_rt_size", (float)writeW, (float)writeH);

			// Only the final chain effect sRGB-encodes; intermediates stay linear.
			shader->BindInt("u_gamma_correct", isLast ? 1 : 0);

			for (const auto &kv : resolvedInputs)
				shader->BindTexture(kv.first, kv.second);

			// Apply the effect's declared default uniforms, then this
			// entry's per-instance overrides (only names the effect
			// declares; unknown override names are ignored).
			for (const auto &kv : effect->GetUniforms())
			{
				auto &u = kv.second;
				if (!u) continue;
				u->Bind(shader->GetProgram(), kv.first);
			}
			if (i < packet.chainOverrides.size())
			{
				for (const auto &kv : packet.chainOverrides[i])
				{
					if (!kv.second) continue;
					if (effect->GetUniforms().find(kv.first) == effect->GetUniforms().end())
						continue;
					kv.second->Bind(shader->GetProgram(), kv.first);
				}
			}

			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(quad->Indices.Data.size()), GL_UNSIGNED_INT, 0);

			shader->UnBind();

			RenderUtil::Instance()->IncreaseDrawCall();
			RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(quad->Indices.Data.size()));

			if (!isLast)
				chainPass->UnBind();

			// Release the previous read texture if it was a temp
			// (sourceTex is owned by the pipeline entity manager;
			// don't release it). On the final step writeTex is
			// nullptr, so readTex becomes null and nothing dangles.
			if (readTex != sourceTex)
				Texture::ReleaseTemporary(readTex);

			readTex = writeTex;
		}

		// Restore default FB + viewport so subsequent draws (editor
		// debug overlay, GUI) render against the original target.
		// Render state goes back to the engine's boring defaults
		// (depth on, blend off) -- DrawDebug and ImGui set their own
		// state on top of this.
		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
		glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
	}

	void PrelightPipeline::DrawEffectDebugView(const std::string &effectName, FramePacket &packet)
	{
		if (!packet.camera.valid) return;

		auto effect = PostProcessRegistry::Get(effectName);
		if (!effect)
		{
			SetDebugViewTexture(nullptr);
			return;
		}

		Texture::Ptr sourceTex = GetLightingOutputTexture(packet.hdrMode);
		if (!sourceTex)
		{
			SetDebugViewTexture(nullptr);
			return;
		}

		// (Re)allocate the debug texture at the composite's size. The
		// entity manager owns it by name so the Profiler's GBuffer tab
		// finds it like any pipeline texture.
		const int W = sourceTex->GetWidth() > 0 ? sourceTex->GetWidth() : 1280;
		const int H = sourceTex->GetHeight() > 0 ? sourceTex->GetHeight() : 720;
		auto debugTex = GetTextureByName("debug_view");
		if (!debugTex || debugTex->GetWidth() != W || debugTex->GetHeight() != H)
		{
			debugTex = Texture::Create("debug_view");
			debugTex->CreateEmpty(W, H, 0, TextureFormat::RGBA8, TextureType::TEXTURE_2D);
			m_EntityManager->Add(debugTex);
		}

		// DEBUG_VIEW variant of the effect shader (own cache key).
		std::string shaderKey = effect->GetShaderPath() + "|debug";
		if (!packet.hdrMode) shaderKey += "|ldr";
		auto shader = GetShaderByName(shaderKey);
		if (!shader)
		{
			shader = Shader::Create(shaderKey, ShaderType::OTHER);
			for (const auto &d : effect->GetShaderDefines())
				shader->AddDefine(d);
			shader->AddDefine("DEBUG_VIEW");
			if (!packet.hdrMode)
				shader->AddDefine("LDR");
			if (!shader->LoadAndCompile(effect->GetShaderPath()))
			{
				FURYE << "DrawEffectDebugView: failed to compile debug view for '"
					  << effect->GetName() << "'";
				SetDebugViewTexture(nullptr);
				return;
			}
			m_EntityManager->Add(shader);
		}

		// Resolve inputs exactly like the chain runner ($-prefixed
		// reserved names; $scene / plain names = lighting output).
		std::vector<std::pair<std::string, Texture::Ptr>> resolvedInputs;
		for (const auto &inputName : effect->GetInputs())
		{
			if (!inputName.empty() && inputName[0] == '$')
			{
				Texture::Ptr reserved = (inputName == "$scene")
					? sourceTex : GetTextureByName(inputName.substr(1));
				if (!reserved)
				{
					FURYW << "DrawEffectDebugView: effect '" << effect->GetName()
						  << "' input '" << inputName << "' not found -- view skipped";
					SetDebugViewTexture(nullptr);
					return;
				}
				resolvedInputs.emplace_back(inputName.substr(1), reserved);
			}
			else
			{
				resolvedInputs.emplace_back(inputName, sourceTex);
			}
		}

		// Render into the debug texture. Same state rules as the
		// chain: fullscreen replace, never inherit pass state.
		GLint prev_fbo = 0;
		GLint prev_vp[4] = { 0, 0, 0, 0 };
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
		glGetIntegerv(GL_VIEWPORT, prev_vp);

		auto debugPass = Pass::Create("DebugViewFBO");
		debugPass->SetBlendMode(BlendMode::REPLACE);
		debugPass->SetClearMode(ClearMode::COLOR);
		debugPass->AddTexture(debugTex, false);
		debugPass->Bind(false);

		auto quad = MeshUtil::GetUnitQuad();
		shader->Bind();
		shader->BindMesh(quad);
		shader->BindCameraData(packet.camera);
		shader->BindFloat("u_rt_size", (float)W, (float)H);
		// The debug texture is displayed as-is by ImGui (non-sRGB):
		// encode here so the view matches the viewport's brightness.
		shader->BindInt("u_gamma_correct", 1);

		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);

		for (const auto &kv : resolvedInputs)
			shader->BindTexture(kv.first, kv.second);

		// Descriptor defaults, then the scene entry's overrides (so
		// the Edit dialog tunes the debug view live). The packet's
		// chain/overrides come from the same renderSettings the old
		// code re-read here.
		for (const auto &kv : effect->GetUniforms())
		{
			auto &u = kv.second;
			if (!u) continue;
			u->Bind(shader->GetProgram(), kv.first);
		}
		for (size_t ci = 0; ci < packet.chain.size() && ci < packet.chainOverrides.size(); ++ci)
		{
			if (!packet.chain[ci] || packet.chain[ci]->GetName() != effectName)
				continue;
			for (const auto &kv : packet.chainOverrides[ci])
			{
				if (!kv.second) continue;
				if (effect->GetUniforms().find(kv.first) == effect->GetUniforms().end())
					continue;
				kv.second->Bind(shader->GetProgram(), kv.first);
			}
			break;
		}

		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(quad->Indices.Data.size()), GL_UNSIGNED_INT, 0);

		shader->UnBind();
		debugPass->UnBind();

		glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
		glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseTriangleCount(static_cast<unsigned int>(quad->Indices.Data.size()));

		SetDebugViewTexture(debugTex);
	}

}