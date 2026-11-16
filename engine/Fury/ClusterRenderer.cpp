#include "Fury/ClusterRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <unordered_map>

#include "Fury/ClusterLodSelection.h"
#include "Fury/ClusterShaderSources.h"
#include "Fury/FramePacket.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/Pass.h"
#include "Fury/Pipeline.h"
#include "Fury/RenderUtil.h"
#include "Fury/Shader.h"
#include "Fury/ShaderCache.h"
#include "Fury/Texture.h"
#include "Fury/Uniform.h"
#include "Fury/RHI/RHIBackend.h"
#include "Fury/RHI/RHICommandList.h"

namespace fury
{
	namespace
	{
		constexpr std::uint32_t InvalidCluster = ClusterLodInvalidIndex;
		constexpr std::size_t MaxSubmeshes = 8;

		struct GpuInstance
		{
			float world[16];
			float sphere[4];
			float camera[4];
			float scaleThreshold[4];
			std::uint32_t offsets[4];
		};
		static_assert(sizeof(GpuInstance) == 128);
		static_assert(sizeof(ClusterLodGpuCluster) == 64);
		static_assert(sizeof(ClusterLodGpuGroup) == 64);

		struct GpuBatch
		{
			std::uint32_t a[4];
			std::uint32_t b[4];
		};
		static_assert(sizeof(GpuBatch) == 32);

		struct VisibleRecord
		{
			std::uint32_t instanceID;
			std::uint32_t clusterID;
			std::uint32_t meshID;
			std::uint32_t batchID;
		};
		static_assert(sizeof(VisibleRecord) == 16);

		struct MeshResources
		{
			const Mesh *mesh = nullptr;
			std::uint32_t clusterOffset = 0;
			std::uint32_t groupOffset = 0;
			RHIBufferRef mergedIndices;
		};

		struct PipelineEntry
		{
			Shader::Ptr shader;
			RHIShaderModuleRef module;
			RHIGraphicsPipelineRef pipeline;
			std::vector<RHIIndirectVertexBinding> vertexBuffers;
			std::array<RHIBufferRef, 5> sourceBuffers;
			unsigned int materialVersion = 0;
			std::uint64_t lastSeenFrame = 0;
		};

		struct BatchWork
		{
			GpuBatch gpu;
			std::shared_ptr<Mesh> mesh;
			std::shared_ptr<Material> material;
			std::uint32_t capacity = 0;
			RHIBufferRef mergedIndices;
			std::shared_ptr<PipelineEntry> pipeline;
			RHIIndirectBatchRef indirect;
		};

		struct FrameBuffers
		{
			RHIBufferRef instances;
			RHIBufferRef clusters;
			RHIBufferRef groups;
			RHIBufferRef selectedCounts;
			RHIBufferRef selectedPairs;
			RHIBufferRef frustum;
			RHIBufferRef batches;
			RHIBufferRef visibleRecords;
			RHIBufferRef arguments;
			RHIBufferRef counts;
			RHIBufferRef stats;
			RHIBufferRef instanceMaterials;
			RHIBufferRef limits;
		};

		RHICommandList *Cmd()
		{
			return GRHIDevice ? GRHIDevice->GetCommandList() : nullptr;
		}

		RHIBufferRef MakeBuffer(const char *name, RHIBufferType type, size_t size,
			const void *data = nullptr, bool indirect = false)
		{
			RHIBufferDesc desc;
			desc.Name = name;
			desc.Type = type;
			desc.Usage = RHIBufferUsage::STATIC;
			desc.UsageFlags = RHI_BUFFER_TRANSFER_SRC | (indirect ? RHI_BUFFER_INDIRECT : 0u);
			desc.Size = std::max<size_t>(size, 16);
			if (data && size < desc.Size)
			{
				unsigned char padded[16] = {};
				std::memcpy(padded, data, size);
				return GRHIDevice->CreateBuffer(desc, padded);
			}
			return GRHIDevice->CreateBuffer(desc, data);
		}

		float Length(const Vector4 &value)
		{
			return std::sqrt(value.SquareLength());
		}

		void AppendLegacyInstance(const PacketClusterInstance &instance, FramePacket &packet)
		{
			for (unsigned int i = 0; i < instance.mesh->GetSubMeshCount(); ++i)
			{
				PacketUnit unit;
				unit.nodeKey = instance.nodeKey;
				unit.mesh = instance.mesh;
				unit.material = instance.materials[i];
				unit.subMesh = static_cast<int>(i);
				unit.worldMatrix = instance.worldMatrix;
				unit.worldBounds = instance.worldBounds;
				unit.worldPos = instance.worldBounds.GetCenter();
				packet.opaqueUnits.push_back(std::move(unit));
			}
		}

		bool ConeTransformSafe(const Matrix4 &world)
		{
			const Vector4 x(world.Raw[0], world.Raw[1], world.Raw[2], 0.0f);
			const Vector4 y(world.Raw[4], world.Raw[5], world.Raw[6], 0.0f);
			const Vector4 z(world.Raw[8], world.Raw[9], world.Raw[10], 0.0f);
			const float lx = Length(x);
			const float ly = Length(y);
			const float lz = Length(z);
			if (lx <= 0.0f || ly <= 0.0f || lz <= 0.0f)
				return false;
			const float maximum = std::max({ lx, ly, lz });
			const float minimum = std::min({ lx, ly, lz });
			if (maximum - minimum > maximum * 1e-3f)
				return false;
			return std::abs(x * y) + std::abs(x * z) + std::abs(y * z) <= maximum * maximum * 1e-3f;
		}

		std::vector<Vector4> MakeFrustumPlanes(const FramePacket &packet)
		{
			std::vector<Vector4> planes;
			planes.reserve(6);
			for (const auto &plane : packet.camera.frustum.GetPlanes())
			{
				const Vector4 normal = plane.GetNormal();
				planes.emplace_back(normal.x, normal.y, normal.z, plane.GetDistance());
			}
			return planes;
		}

		void SetNamed(RHICommandList *cmd, const char *name, unsigned int binding, const RHIBufferRef &buffer)
		{
			cmd->SetStorageBufferNamed(name, binding, buffer);
		}

		std::shared_ptr<Shader> ResolveClusterShader(const std::shared_ptr<Pass> &pass,
			const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh)
		{
			if (auto shader = material->GetShaderForPass(pass->GetRenderIndex()))
				return shader;
			unsigned int flags = material->GetTextureFlags();
			if (material->GetAlphaMode() == AlphaMode::MASK)
				flags |= static_cast<unsigned int>(ShaderTexture::ALPHA_TEST);
			if (material->GetTwoSided())
				flags |= static_cast<unsigned int>(ShaderTexture::TWO_SIDED);
			const ShaderType type = mesh->IsSkinnedMesh() ? ShaderType::SKINNED_MESH : ShaderType::STATIC_MESH;
			auto shader = pass->GetShader(type, flags);
			if (!shader)
				shader = pass->GetShader(type, flags & ~static_cast<unsigned int>(ShaderTexture::TWO_SIDED));
			if (!shader)
				shader = pass->GetShader(type, material->GetTextureFlags());
			return shader;
		}
	}

	struct ProjectionFactor
	{
		float value = 0.0f;
		std::uint64_t lastSeenFrame = 0;
	};

	struct ClusterRenderer::Impl
	{
		RHIShaderModuleRef selectModule;
		RHIShaderModuleRef cullModule;
		std::map<std::pair<std::string, std::string>, std::shared_ptr<PipelineEntry>> pipelines;
		std::unordered_map<std::string, ProjectionFactor> projectionFactors;
		std::vector<BatchWork> batches;
		FrameBuffers buffers;
		RHIBufferRef constantVertex;
		std::uint64_t preparedFrameIndex = 0;
		std::uint32_t candidateInstances = 0;
		std::uint32_t fallbackInstances = 0;

		bool EnsureModules()
		{
			if (selectModule && cullModule)
				return true;
			auto blob = [](const std::string &text,
				const std::vector<std::pair<std::string, std::string>> &defines = {})
			{
				RHIShaderBlob blob;
				blob.Data.assign(text.begin(), text.end());
				blob.Defines = defines;
				blob.IsSource = true;
				return blob;
			};
#if WITH_DBG_OVERLAY
			constexpr bool debugOverlay = true;
#else
			constexpr bool debugOverlay = false;
#endif
			ShaderCompileInput selectInput = BuildComputeShaderCompileInput(
				"ClusterSelect", ClusterSelectShaderSource, {}, false, debugOverlay);
			ShaderCompileInput cullInput = BuildComputeShaderCompileInput(
				"ClusterCull", ClusterCullShaderSource, {}, false, debugOverlay);
			selectModule = GRHIDevice->CreateComputeModule("ClusterSelect", blob(selectInput.computeSource, selectInput.defines));
			cullModule = GRHIDevice->CreateComputeModule("ClusterCull", blob(cullInput.computeSource, cullInput.defines));
			return selectModule && selectModule->IsValid() && cullModule && cullModule->IsValid();
		}

		std::shared_ptr<PipelineEntry> GetPipeline(const std::shared_ptr<Pass> &pass,
			const std::shared_ptr<Material> &material, const std::shared_ptr<Mesh> &mesh)
		{
			auto base = ResolveClusterShader(pass, material, mesh);
			if (!base)
			{
				FURYW << "Cluster GBuffer shader missing for material '" << material->GetName() << "'";
				return nullptr;
			}
			mesh->UpdateBuffer();
			const std::array<RHIBufferRef, 5> sourceBuffers = {
				mesh->Positions.GetRHIBuffer(), mesh->Normals.GetRHIBuffer(), mesh->Tangents.GetRHIBuffer(),
				mesh->UVs.GetRHIBuffer(), mesh->Colors.GetRHIBuffer()
			};
			const auto key = std::make_pair(base->GetUUID(), mesh->GetUUID());
			auto found = pipelines.find(key);
			if (found != pipelines.end() && found->second->materialVersion == material->GetRenderVersion()
				&& found->second->sourceBuffers == sourceBuffers)
			{
				found->second->lastSeenFrame = preparedFrameIndex;
				return found->second;
			}
			auto entry = std::make_shared<PipelineEntry>();
			entry->sourceBuffers = sourceBuffers;
			entry->lastSeenFrame = preparedFrameIndex;
			entry->shader = Shader::Create(base->GetName() + "_cluster", base->GetType(), base->GetTextureFlags());
			for (const auto &define : base->GetDefines())
				entry->shader->AddDefine(define);
			entry->shader->AddDefine("CLUSTER_BASE_INSTANCE");
			if (!entry->shader->LoadAndCompile(base->GetFilePath()))
			{
				FURYE << "Cluster GBuffer shader compilation failed for material '" << material->GetName() << "'";
				return nullptr;
			}
			entry->module = entry->shader->GetRHIModule();
			if (!entry->module)
				return nullptr;

			RHIGraphicsPipelineDesc desc;
			desc.Name = base->GetName() + "_cluster";
			desc.DepthTest = true;
			desc.DepthWrite = true;
			desc.Cull = material->GetTwoSided() ? CullMode::NONE : CullMode::BACK;
			unsigned int stream = 0;
			auto add = [&](const std::string &name, const RHIBufferRef &buffer, int components, bool empty)
			{
				const int location = entry->module->ResolveAttribLocation(name);
				if (location < 0)
					return;
				RHIVertexAttribute attr;
				attr.Name = name;
				attr.Format = components == 2 ? RHIVertexFormat::FLOAT2 :
					components == 4 ? RHIVertexFormat::FLOAT4 : RHIVertexFormat::FLOAT3;
				attr.Offset = 0;
				attr.Stride = components * sizeof(float);
				attr.Stream = stream;
				attr.Location = location;
				attr.ConstantFill = empty;
				desc.VertexAttributes.push_back(attr);
				if (empty)
				{
					if (!constantVertex)
					{
						const float fill[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
						constantVertex = MakeBuffer("ClusterConstantVertex", RHIBufferType::VERTEX, sizeof(fill), fill);
					}
					entry->vertexBuffers.push_back({ stream, constantVertex });
				}
				else
				{
					entry->vertexBuffers.push_back({ stream, buffer });
				}
				++stream;
			};
			add(mesh->Positions.Name, mesh->Positions.GetRHIBuffer(), 3, mesh->Positions.Data.empty());
			add(mesh->Normals.Name, mesh->Normals.GetRHIBuffer(), 3, mesh->Normals.Data.empty());
			add(mesh->Tangents.Name, mesh->Tangents.GetRHIBuffer(), 3, mesh->Tangents.Data.empty());
			add(mesh->UVs.Name, mesh->UVs.GetRHIBuffer(), 2, mesh->UVs.Data.empty());
			add(mesh->Colors.Name, mesh->Colors.GetRHIBuffer(), 4, mesh->Colors.Data.empty());
			entry->pipeline = GRHIDevice->CreateGraphicsPipeline(desc, entry->module);
			if (!entry->pipeline)
				return nullptr;
			entry->materialVersion = material->GetRenderVersion();
			pipelines[key] = entry;
			return entry;
		}

		void PrepareBatches(FramePacket &packet)
		{
			batches.clear();
			buffers = FrameBuffers();
			candidateInstances = static_cast<std::uint32_t>(packet.clusterInstances.size());
			fallbackInstances = 0;
			std::vector<PacketClusterInstance> accepted;
			std::vector<GpuInstance> gpuInstances;
			std::vector<ClusterLodGpuCluster> gpuClusters;
			std::vector<ClusterLodGpuGroup> gpuGroups;
			std::vector<std::uint32_t> instanceMaterials;
			std::map<const Mesh *, MeshResources> meshes;
			std::size_t capacityBudget = 262144;
			if (const char *value = std::getenv("FURY_CLUSTER_LOD_BUDGET"))
				capacityBudget = std::max<size_t>(std::strtoull(value, nullptr, 10), 1);
			std::size_t used = 0;
			for (const auto &instance : packet.clusterInstances)
			{
				const auto &asset = instance.mesh->GetClusterLodAsset();
				const std::size_t leafCount = std::count_if(asset->clusters.begin(), asset->clusters.end(),
					[](const ClusterLodCluster &cluster) { return cluster.level == 0; });
				if (used + leafCount > capacityBudget || instance.mesh->GetSubMeshCount() > MaxSubmeshes)
				{
					AppendLegacyInstance(instance, packet);
					++fallbackInstances;
					continue;
				}
				const std::uint32_t selectedOffset = static_cast<std::uint32_t>(used);
				used += leafCount;
				const Mesh *key = instance.mesh.get();
				if (meshes.find(key) == meshes.end())
				{
					MeshResources resources;
					resources.mesh = key;
					resources.clusterOffset = gpuClusters.size();
					resources.groupOffset = gpuGroups.size();
					meshes.emplace(key, resources);
				}
				MeshResources &resources = meshes[key];
				if (resources.mergedIndices == nullptr)
				{
					resources.clusterOffset = gpuClusters.size();
					resources.groupOffset = gpuGroups.size();
					for (ClusterLodGpuCluster cluster : asset->gpu_clusters)
					{
						if (cluster.group_index != InvalidCluster) cluster.group_index += resources.groupOffset;
						if (cluster.parent_group_index != InvalidCluster) cluster.parent_group_index += resources.groupOffset;
						gpuClusters.push_back(cluster);
					}
					for (ClusterLodGpuGroup group : asset->gpu_groups)
					{
						if (group.parent_group_index != InvalidCluster)
							group.parent_group_index += resources.groupOffset;
						gpuGroups.push_back(group);
					}
					resources.mergedIndices = MakeBuffer("ClusterMergedIndices", RHIBufferType::INDEX,
						asset->merged_indices->size() * sizeof(std::uint32_t), asset->merged_indices->data());
				}

				GpuInstance gpu;
				std::memcpy(gpu.world, instance.worldMatrix.Raw, sizeof(gpu.world));
				const Vector4 center = instance.worldBounds.GetCenter();
				const float radius = instance.worldBounds.GetInfinite() ? 1e9f : Length(instance.worldBounds.GetExtents());
				gpu.sphere[0] = center.x;
				gpu.sphere[1] = center.y;
				gpu.sphere[2] = center.z;
				gpu.sphere[3] = radius;
				gpu.camera[0] = packet.camera.worldPos.x;
				gpu.camera[1] = packet.camera.worldPos.y;
				gpu.camera[2] = packet.camera.worldPos.z;
				const float desired = packet.viewportH * 0.5f * packet.camera.projectionMatrix.Raw[5];
				auto previous = projectionFactors.find(instance.nodeUuid);
				const float effective = ClusterLodUpdateProjectionFactor(desired,
					previous == projectionFactors.end() ? 0.0f : previous->second.value);
				projectionFactors[instance.nodeUuid] = { effective, preparedFrameIndex };
				gpu.camera[3] = effective;
				gpu.scaleThreshold[0] = ClusterLodConservativeScale(instance.worldMatrix.Raw);
				gpu.scaleThreshold[1] = packet.clusterLodError;
				gpu.scaleThreshold[2] = ConeTransformSafe(instance.worldMatrix) ? 1.0f : 0.0f;
				gpu.scaleThreshold[3] = 0.0f;
				gpu.offsets[0] = resources.clusterOffset;
				gpu.offsets[1] = asset->clusters.size();
				gpu.offsets[2] = selectedOffset;
				gpu.offsets[3] = leafCount;
				gpuInstances.push_back(gpu);
				accepted.push_back(instance);
			}
			packet.clusterInstances = std::move(accepted);
			if (packet.clusterInstances.empty())
				return;

			std::size_t visibleOffset = 0;
			std::size_t argumentOffset = 0;
			std::size_t countOffset = 0;
			instanceMaterials.assign(packet.clusterInstances.size() * MaxSubmeshes, InvalidCluster);
			for (const auto &[key, resources] : meshes)
			{
				const auto &asset = key->GetClusterLodAsset();
				for (unsigned int materialIndex = 0; materialIndex < key->GetSubMeshCount(); ++materialIndex)
				{
					std::map<const Material *, std::pair<std::shared_ptr<Material>, std::size_t>> capacities;
					for (const auto &instance : packet.clusterInstances)
					{
						if (instance.mesh.get() != key)
							continue;
						const auto &material = instance.materials[materialIndex];
						auto &entry = capacities[material.get()];
						entry.first = material;
						entry.second += std::count_if(asset->clusters.begin(), asset->clusters.end(),
							[&](const ClusterLodCluster &cluster)
							{
								return cluster.level == 0 && cluster.material_index == materialIndex;
							});
					}
					for (const auto &[materialKey, entry] : capacities)
					{
						const auto &material = entry.first;
						const std::size_t capacity = entry.second;
						if (capacity == 0)
							continue;
						BatchWork batch;
						batch.mesh = key->GetLodMesh(0);
						batch.material = material;
						batch.capacity = capacity;
						batch.mergedIndices = resources.mergedIndices;
						batch.gpu.a[0] = resources.clusterOffset;
						batch.gpu.a[1] = materialIndex;
						batch.gpu.a[2] = visibleOffset;
						batch.gpu.a[3] = argumentOffset;
						batch.gpu.b[0] = countOffset;
						batch.gpu.b[1] = gpuInstances.size();
						batch.gpu.b[2] = capacity;
						batch.gpu.b[3] = batches.size();
						for (std::size_t instanceIndex = 0; instanceIndex < packet.clusterInstances.size(); ++instanceIndex)
						{
							const auto &instance = packet.clusterInstances[instanceIndex];
							if (instance.mesh.get() == key && instance.materials[materialIndex].get() == materialKey)
								instanceMaterials[instanceIndex * MaxSubmeshes + materialIndex] = batch.gpu.b[3];
						}
						batches.push_back(std::move(batch));
						visibleOffset += capacity;
						argumentOffset += capacity;
						countOffset += (capacity + GRHIDevice->GetCapabilities().MaxDrawIndirectCount - 1)
							/ GRHIDevice->GetCapabilities().MaxDrawIndirectCount;
					}
				}
			}

			std::vector<GpuBatch> gpuBatches;
			for (const BatchWork &batch : batches)
				gpuBatches.push_back(batch.gpu);
			buffers.instances = MakeBuffer("ClusterInstances", RHIBufferType::STORAGE,
				gpuInstances.size() * sizeof(GpuInstance), gpuInstances.data());
			buffers.clusters = MakeBuffer("ClusterRecords", RHIBufferType::STORAGE,
				gpuClusters.size() * sizeof(ClusterLodGpuCluster), gpuClusters.data());
			buffers.groups = MakeBuffer("ClusterGroups", RHIBufferType::STORAGE,
				gpuGroups.size() * sizeof(ClusterLodGpuGroup), gpuGroups.data());
			buffers.selectedCounts = MakeBuffer("ClusterSelectedCounts", RHIBufferType::STORAGE,
				gpuInstances.size() * sizeof(std::uint32_t));
			buffers.selectedPairs = MakeBuffer("ClusterSelectedPairs", RHIBufferType::STORAGE,
				used * sizeof(std::uint32_t) * 2);
			const auto planes = MakeFrustumPlanes(packet);
			buffers.frustum = MakeBuffer("ClusterFrustum", RHIBufferType::STORAGE,
				planes.size() * sizeof(Vector4), planes.data());
			buffers.batches = MakeBuffer("ClusterBatchRecords", RHIBufferType::STORAGE,
				gpuBatches.size() * sizeof(GpuBatch), gpuBatches.data());
			buffers.visibleRecords = MakeBuffer("ClusterVisibleRecords", RHIBufferType::STORAGE,
				visibleOffset * sizeof(VisibleRecord));
			buffers.arguments = MakeBuffer("ClusterIndirectArguments", RHIBufferType::STORAGE,
				argumentOffset * sizeof(RHIIndirectDrawArgs), nullptr, true);
			buffers.counts = MakeBuffer("ClusterIndirectCounts", RHIBufferType::STORAGE,
				countOffset * sizeof(std::uint32_t), nullptr, true);
			buffers.stats = MakeBuffer("ClusterStats", RHIBufferType::STORAGE, 16 * sizeof(std::uint32_t));
			const std::uint32_t maxDrawCount = GRHIDevice->GetCapabilities().MaxDrawIndirectCount;
			buffers.limits = MakeBuffer("ClusterLimits", RHIBufferType::STORAGE,
				sizeof(maxDrawCount), &maxDrawCount);
			buffers.instanceMaterials = MakeBuffer("ClusterInstanceMaterials", RHIBufferType::STORAGE,
				instanceMaterials.size() * sizeof(std::uint32_t), instanceMaterials.data());
		}

		void Fallback(FramePacket &packet)
		{
			fallbackInstances += static_cast<std::uint32_t>(packet.clusterInstances.size());
			for (const auto &instance : packet.clusterInstances)
				AppendLegacyInstance(instance, packet);
			packet.clusterInstances.clear();
			batches.clear();
			buffers = FrameBuffers();
		}

		void Prepare(FramePacket &packet, const std::shared_ptr<Pass> &pass)
		{
			preparedFrameIndex = packet.frameIndex;
			if (!packet.clusterInstances.empty())
				PrepareBatches(packet);
			for (auto it = projectionFactors.begin(); it != projectionFactors.end();)
				it = it->second.lastSeenFrame == preparedFrameIndex ? std::next(it) : projectionFactors.erase(it);
			auto prunePipelines = [&]()
			{
				for (auto it = pipelines.begin(); it != pipelines.end();)
					it = it->second->lastSeenFrame == preparedFrameIndex ? std::next(it) : pipelines.erase(it);
			};
			if (packet.clusterInstances.empty())
			{
				prunePipelines();
				return;
			}
			if (!EnsureModules())
			{
				FURYE << "Cluster compute shader creation failed; using legacy rendering";
				Fallback(packet);
				return;
			}
			for (BatchWork &batch : batches)
			{
				batch.pipeline = GetPipeline(pass, batch.material, batch.mesh);
				if (!batch.pipeline)
				{
					Fallback(packet);
					return;
				}
			}
			prunePipelines();
		}

		void Execute(FramePacket &packet, const std::shared_ptr<Pass> &pass)
		{
			if (packet.clusterInstances.empty() || batches.empty())
				return;
			auto *cmd = Cmd();
			if (!cmd)
				return;
			cmd->ClearStorageBuffer(buffers.selectedCounts);
			cmd->ClearStorageBuffer(buffers.counts);
			cmd->ClearStorageBuffer(buffers.stats);
			cmd->BindShaderModule(selectModule);
			SetNamed(cmd, "ClusterInstances", 0, buffers.instances);
			SetNamed(cmd, "Clusters", 1, buffers.clusters);
			SetNamed(cmd, "Groups", 2, buffers.groups);
			SetNamed(cmd, "SelectedCounts", 3, buffers.selectedCounts);
			SetNamed(cmd, "SelectedPairs", 4, buffers.selectedPairs);
			SetNamed(cmd, "FrustumPlanes", 5, buffers.frustum);
			SetNamed(cmd, "ClusterStats", 10, buffers.stats);
			cmd->Dispatch(packet.clusterInstances.size(), 1, 1);
			cmd->Barrier();
			cmd->BindShaderModule(cullModule);
			SetNamed(cmd, "ClusterInstances", 0, buffers.instances);
			SetNamed(cmd, "Clusters", 1, buffers.clusters);
			SetNamed(cmd, "SelectedCounts", 3, buffers.selectedCounts);
			SetNamed(cmd, "SelectedPairs", 4, buffers.selectedPairs);
			SetNamed(cmd, "FrustumPlanes", 5, buffers.frustum);
			SetNamed(cmd, "ClusterBatches", 6, buffers.batches);
			SetNamed(cmd, "ClusterVisibleRecords", 7, buffers.visibleRecords);
			SetNamed(cmd, "IndirectArguments", 8, buffers.arguments);
			SetNamed(cmd, "IndirectCounts", 9, buffers.counts);
			SetNamed(cmd, "ClusterStats", 10, buffers.stats);
			SetNamed(cmd, "InstanceMaterials", 11, buffers.instanceMaterials);
			SetNamed(cmd, "ClusterLimits", 12, buffers.limits);
			cmd->Dispatch(batches.size(), 1, 1);
			cmd->Barrier();

			for (BatchWork &batch : batches)
			{
				RHIIndirectBatchDesc desc;
				desc.Arguments = buffers.arguments;
				desc.Counts = buffers.counts;
				desc.Capacity = batch.capacity;
				desc.ArgumentOffset = batch.gpu.a[3] * sizeof(RHIIndirectDrawArgs);
				desc.CountOffset = batch.gpu.b[0] * sizeof(std::uint32_t);
				desc.Pipeline = batch.pipeline->pipeline;
				desc.IndexBuffer = batch.mergedIndices;
				desc.VertexBuffers = batch.pipeline->vertexBuffers;
				desc.StorageBuffers.push_back({ "ClusterInstances", 0, buffers.instances });
				desc.StorageBuffers.push_back({ "ClusterVisibleRecords", 1, buffers.visibleRecords });
				desc.StorageBuffers.push_back({ "Clusters", 2, buffers.clusters });
				for (const auto &texture : batch.material->GetTextures())
					desc.Textures.push_back({ texture.first, texture.second->GetRHITexture() });
				if (batch.material->GetTextures().find(Material::DIFFUSE_TEXTURE) == batch.material->GetTextures().end())
					desc.Textures.push_back({ Material::DIFFUSE_TEXTURE, GetDummyTexture2D()->GetRHITexture() });
				if (batch.material->GetTextures().find(Material::NORMAL_TEXTURE) == batch.material->GetTextures().end())
					desc.Textures.push_back({ Material::NORMAL_TEXTURE, GetDummyNormalTexture()->GetRHITexture() });
				for (unsigned int i = 0; i < pass->GetTextureCount(true); ++i)
				{
					auto texture = pass->GetTextureAt(i, true);
					desc.Textures.push_back({ texture->GetName(), texture->GetRHITexture() });
				}
				batch.indirect = GRHIDevice->CreateIndirectBatch(desc);
				if (!batch.indirect || !cmd->PrepareIndirectBatch(batch.indirect))
				{
					FURYE << "Cluster indirect batch preparation failed for material '" << batch.material->GetName()
						<< "'; using legacy rendering";
					Fallback(packet);
					return;
				}
			}
			for (BatchWork &batch : batches)
			{
				cmd->SetPipeline(batch.pipeline->pipeline);
				batch.pipeline->shader->BindCameraData(packet.camera);
				for (const auto &uniform : batch.material->GetUniforms())
					uniform.second->BindAt(cmd->ResolveUniform(uniform.first));
				cmd->SetUniformFloat("u_alpha_cutoff", batch.material->GetAlphaMode() == AlphaMode::MASK
					? batch.material->GetAlphaCutoff() : -1.0f);
				cmd->SetUniformInt("normal_texture_valid",
					batch.material->GetTextures().find(Material::NORMAL_TEXTURE) != batch.material->GetTextures().end() ? 1 : 0);
				const int debugView = packet.switches.test((size_t)PipelineSwitch::CLUSTER_LOD_VIEW) ? 1
					: (packet.switches.test((size_t)PipelineSwitch::CLUSTER_ID_VIEW) ? 2 : 0);
				cmd->SetUniformInt("cluster_debug_view", debugView);
				if (!cmd->DrawIndexedIndirect(batch.indirect))
					FURYE << "Cluster indirect execution failed for material '" << batch.material->GetName() << "'";
			}
		}

		bool ReadStats(ClusterLodStats &out) const
		{
			out = {};
			out.frameIndex = preparedFrameIndex;
			out.candidateInstances = candidateInstances;
			out.fallbackInstances = fallbackInstances;
			if (!GRHIDevice)
			{
				out.unavailableReason = "no RHI device";
				return false;
			}
			if (!buffers.stats)
			{
				out.unavailableReason = "no accepted cluster instances";
				return false;
			}
			RHIBufferReadbackRequest request;
			request.Buffer = buffers.stats;
			request.Size = 16 * sizeof(std::uint32_t);
			request.FrameIndex = preparedFrameIndex;
			RHIBufferReadbackResult result;
			if (!GRHIDevice->ReadbackBuffer(request, result) || result.Bytes.size() != request.Size)
			{
				out.unavailableReason = "statistics readback failed";
				return false;
			}
			std::uint32_t values[16];
			std::memcpy(values, result.Bytes.data(), sizeof(values));
			out.instances = static_cast<std::uint32_t>(buffers.instances->GetDesc().Size / sizeof(GpuInstance));
			out.visibleInstances = values[0];
			out.selected = values[1];
			out.visible = values[2];
			out.commands = values[3];
			out.selectionOverflow = values[4];
			out.visibilityOverflow = values[5];
			out.drawnTriangles = values[6];
			out.batches = static_cast<std::uint32_t>(batches.size());
			return true;
		}

		void LogDiagnostics(std::uint64_t frameIndex, const FramePacket &packet) const
		{
			if (!GRHIDevice || !buffers.stats)
				return;
			auto readWords = [frameIndex](const RHIBufferRef &buffer, size_t offset, size_t count,
				std::vector<std::uint32_t> &out)
			{
				RHIBufferReadbackRequest request;
				request.Buffer = buffer;
				request.Offset = offset * sizeof(std::uint32_t);
				request.Size = count * sizeof(std::uint32_t);
				request.FrameIndex = frameIndex;
				RHIBufferReadbackResult result;
				if (!buffer)
					return false;
				if (!GRHIDevice->ReadbackBuffer(request, result) || result.Bytes.size() != request.Size)
				{
					FURYW << "Cluster diagnostics readback failed for " << buffer->GetDesc().Name
						<< " offset=" << request.Offset << " size=" << request.Size
						<< " allocation=" << buffer->GetDesc().Size;
					return false;
				}
				out.resize(count);
				std::memcpy(out.data(), result.Bytes.data(), request.Size);
				return true;
			};
			std::vector<std::uint32_t> stats;
			if (!readWords(buffers.stats, 0, 16, stats))
			{
				FURYW << "Cluster diagnostics readback failed for frame " << frameIndex;
				return;
			}
			FURYI << "Cluster diagnostics frame=" << frameIndex
				<< " instances=" << buffers.instances->GetDesc().Size / sizeof(GpuInstance)
				<< " candidateInstances=" << candidateInstances << " fallbackInstances=" << fallbackInstances
				<< " batches=" << batches.size()
				<< " visibleInstances=" << stats[0] << " selected=" << stats[1]
				<< " visible=" << stats[2] << " commands=" << stats[3] << " drawnTriangles=" << stats[6]
				<< " selectionOverflow=" << stats[4] << " visibilityOverflow=" << stats[5];

			const size_t instanceCount = buffers.instances->GetDesc().Size / sizeof(GpuInstance);
			std::vector<std::uint32_t> selectedCounts;
			std::vector<std::uint32_t> instances;
			std::vector<std::uint32_t> selectedPairs;
			std::vector<std::uint32_t> clusters;
			readWords(buffers.selectedCounts, 0, instanceCount, selectedCounts);
			readWords(buffers.instances, 0, instanceCount * 32, instances);
			readWords(buffers.selectedPairs, 0, buffers.selectedPairs->GetDesc().Size / sizeof(std::uint32_t), selectedPairs);
			readWords(buffers.clusters, 0, buffers.clusters->GetDesc().Size / sizeof(std::uint32_t), clusters);
			auto asFloat = [](std::uint32_t value)
			{
				float out;
				std::memcpy(&out, &value, sizeof(out));
				return out;
			};
			std::map<std::uint32_t, std::pair<size_t, size_t>> levelTotals;
			size_t selectionMismatches = 0;
			size_t selectedRoots = 0;
			for (size_t i = 0; i < instanceCount && i < selectedCounts.size() && i * 32 + 31 < instances.size(); ++i)
			{
				const size_t selectedOffset = instances[i * 32 + 30];
				FURYI << "Cluster instance " << i << " selected=" << selectedCounts[i]
					<< " clusterOffset=" << instances[i * 32 + 28]
					<< " clusterCount=" << instances[i * 32 + 29]
					<< " selectedOffset=" << selectedOffset << " leafCapacity=" << instances[i * 32 + 31]
					<< " threshold=" << asFloat(instances[i * 32 + 25])
					<< " projectionScale=" << asFloat(instances[i * 32 + 23]);
				for (size_t j = 0; j < selectedCounts[i]; ++j)
				{
					const size_t pair = (selectedOffset + j) * 2;
					if (pair + 1 >= selectedPairs.size())
						continue;
					const size_t cluster = selectedPairs[pair + 1] * 16;
					if (cluster + 15 < clusters.size())
					{
						auto &total = levelTotals[clusters[cluster + 15]];
						++total.first;
						total.second += clusters[cluster + 13] / 3;
						if (clusters[cluster + 11] == InvalidCluster)
							++selectedRoots;
					}
				}
				if (selectedCounts[i] > 0 && i < packet.clusterInstances.size())
				{
					std::vector<std::uint32_t> gpuSelected;
					for (size_t j = 0; j < selectedCounts[i]; ++j)
					{
						const size_t pair = (selectedOffset + j) * 2;
						if (pair + 1 < selectedPairs.size())
							gpuSelected.push_back(selectedPairs[pair + 1] - instances[i * 32 + 28]);
					}
					std::sort(gpuSelected.begin(), gpuSelected.end());
					const auto &instance = packet.clusterInstances[i];
					ClusterLodSelectionParams params;
					params.camera_position[0] = packet.camera.worldPos.x;
					params.camera_position[1] = packet.camera.worldPos.y;
					params.camera_position[2] = packet.camera.worldPos.z;
					params.world_scale = asFloat(instances[i * 32 + 24]);
					params.projection_scale = asFloat(instances[i * 32 + 23]);
					params.error_threshold = asFloat(instances[i * 32 + 25]);
					params.world_matrix = instance.worldMatrix.Raw;
					auto cpuSelected = SelectClusterLodCut(*instance.mesh->GetClusterLodAsset(), params);
					std::sort(cpuSelected.begin(), cpuSelected.end());
					if (cpuSelected != gpuSelected)
					{
						++selectionMismatches;
						FURYW << "Cluster selection mismatch instance=" << i
							<< " cpu=" << cpuSelected.size() << " gpu=" << gpuSelected.size();
					}
				}
				for (size_t j = 0; j < std::min<size_t>(selectedCounts[i], 4); ++j)
				{
					const size_t pair = (selectedOffset + j) * 2;
					if (pair + 1 < selectedPairs.size())
					{
						const size_t cluster = selectedPairs[pair + 1] * 16;
						FURYI << "  pair " << j << " instance=" << selectedPairs[pair]
							<< " cluster=" << selectedPairs[pair + 1]
							<< " level=" << (cluster + 15 < clusters.size() ? clusters[cluster + 15] : InvalidCluster);
					}
				}
			}
			FURYI << "Cluster selection comparison mismatches=" << selectionMismatches;
			FURYI << "Cluster selected roots=" << selectedRoots;
			for (const auto &[level, total] : levelTotals)
				FURYI << "Cluster selected level " << level << " clusters=" << total.first
					<< " triangles=" << total.second;

			std::vector<std::uint32_t> counts;
			std::vector<std::uint32_t> arguments;
			std::vector<std::uint32_t> records;
			readWords(buffers.counts, 0, buffers.counts->GetDesc().Size / sizeof(std::uint32_t), counts);
			readWords(buffers.arguments, 0, buffers.arguments->GetDesc().Size / sizeof(std::uint32_t), arguments);
			readWords(buffers.visibleRecords, 0, buffers.visibleRecords->GetDesc().Size / sizeof(std::uint32_t), records);
			const size_t maxDrawCount = GRHIDevice->GetCapabilities().MaxDrawIndirectCount;
			for (size_t i = 0; i < batches.size(); ++i)
			{
				const BatchWork &batch = batches[i];
				const size_t countOffset = batch.gpu.b[0];
				const size_t visibleOffset = batch.gpu.a[2];
				const size_t argumentOffset = batch.gpu.a[3];
				const size_t chunks = (batch.capacity + maxDrawCount - 1) / maxDrawCount;
				size_t drawCount = 0;
				for (size_t chunk = 0; chunk < chunks && countOffset + chunk < counts.size(); ++chunk)
					drawCount += counts[countOffset + chunk];
				FURYI << "Cluster batch " << i << " material='" << batch.material->GetName()
					<< "' capacity=" << batch.capacity << " commands=" << drawCount
					<< " visibleOffset=" << visibleOffset << " argumentOffset=" << argumentOffset;
				for (size_t command = 0; command < std::min<size_t>(drawCount, 3); ++command)
				{
					const size_t arg = (argumentOffset + command) * 5;
					const size_t record = (visibleOffset + command) * 4;
					if (arg + 4 < arguments.size() && record + 3 < records.size())
						FURYI << "  command " << command << " indexCount=" << arguments[arg]
							<< " instances=" << arguments[arg + 1] << " firstIndex=" << arguments[arg + 2]
							<< " vertexOffset=" << static_cast<std::int32_t>(arguments[arg + 3])
							<< " firstInstance=" << arguments[arg + 4]
							<< " record=" << records[record] << "," << records[record + 1]
							<< "," << records[record + 2] << "," << records[record + 3];
				}
				if (batch.indirect && batch.indirect->GetExecutionRangeBuffer())
				{
					std::vector<std::uint32_t> ranges;
					if (readWords(batch.indirect->GetExecutionRangeBuffer(), 0, chunks * 2, ranges))
						for (size_t chunk = 0; chunk < chunks; ++chunk)
							FURYI << "  range " << chunk << " location=" << ranges[chunk * 2]
								<< " length=" << ranges[chunk * 2 + 1];
				}
			}
		}
	};

	ClusterRenderer::ClusterRenderer() : m_Impl(std::make_unique<Impl>()) {}
	ClusterRenderer::~ClusterRenderer() = default;

	void ClusterRenderer::Prepare(FramePacket &packet, const std::shared_ptr<Pass> &pass)
	{
		m_Impl->Prepare(packet, pass);
	}

	void ClusterRenderer::Execute(FramePacket &packet, const std::shared_ptr<Pass> &pass)
	{
		m_Impl->Execute(packet, pass);
	}

	bool ClusterRenderer::ReadStats(ClusterLodStats &out) const
	{
		return m_Impl->ReadStats(out);
	}

	void ClusterRenderer::LogDiagnostics(std::uint64_t frameIndex, const FramePacket &packet) const
	{
		m_Impl->LogDiagnostics(frameIndex, packet);
	}
}
