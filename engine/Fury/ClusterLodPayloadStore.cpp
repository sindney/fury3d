#include "Fury/ClusterLodPayloadStore.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "Fury/AssetBackend.h"
#include "Fury/DdcStore.h"
#include "Fury/FileUtil.h"
#include "Fury/HashUtil.h"

namespace fury
{
	namespace
	{
		std::string Hex64(std::uint64_t value)
		{
			char text[17];
			std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
			return text;
		}

		std::uint32_t FloatBits(float value)
		{
			std::uint32_t bits;
			std::memcpy(&bits, &value, sizeof(bits));
			return bits;
		}

		bool SameOptions(const ClusterLodBuildOptions &a, const ClusterLodBuildOptions &b)
		{
			return a.cluster_triangle_limit == b.cluster_triangle_limit
				&& a.cluster_vertex_limit == b.cluster_vertex_limit
				&& a.group_cluster_limit == b.group_cluster_limit
				&& FloatBits(a.reduction_ratio) == FloatBits(b.reduction_ratio)
				&& FloatBits(a.target_error) == FloatBits(b.target_error);
		}

		std::string SourceRoot(const DdcStore &ddc)
		{
			if (const char *override_root = std::getenv("FURY_CLOD_ROOT"))
			{
				if (override_root[0] != '\0')
					return override_root;
			}
			const std::string executable = FileUtil::GetExecutablePath();
			const auto default_ddc = std::filesystem::absolute(
				std::filesystem::path(executable).parent_path() / "DDC").lexically_normal();
			const auto actual_ddc = std::filesystem::absolute(ddc.Root()).lexically_normal();
			if (actual_ddc == default_ddc)
				return (std::filesystem::path(executable).parent_path() / "Resource" / "ClusterLOD").string();
			return (std::filesystem::path(ddc.Root()) / "source").string();
		}

		std::string SourcePath(const DdcStore &ddc, const std::string &key)
		{
			return SourceRoot(ddc) + "/" + key + ".clod";
		}

		bool WriteBytes(const std::string &path, const std::vector<std::uint8_t> &bytes, std::string &error)
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
			if (ec)
			{
				error = "cannot create cluster payload directory";
				return false;
			}
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				error = "cannot write cluster payload " + path;
				return false;
			}
			stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
			if (!stream)
			{
				error = "cannot write complete cluster payload " + path;
				return false;
			}
			return true;
		}
	}

	std::string MakeClusterLodPayloadKey(const ClusterLodDescriptor &descriptor)
	{
		char settings[192];
		std::snprintf(settings, sizeof(settings), "%u|%u|%u|%u|%u|%u|%08x|%08x|%08x",
			descriptor.schema_version, ClusterLodBuilderVersion, ClusterLodMeshoptimizerVersion,
			descriptor.options.cluster_triangle_limit, descriptor.options.cluster_vertex_limit,
			descriptor.options.group_cluster_limit, FloatBits(descriptor.options.reduction_ratio),
			FloatBits(descriptor.options.target_error), ClusterLodPayloadHeaderSize);
		return "CLOD_" + HashUtil::Sha1Hex(settings + ("|" + Hex64(descriptor.content_hash)));
	}

	std::string MakeClusterLodPackedKey(const std::string &payload_key)
	{
		return "cluster_meshes/" + payload_key + ".clod";
	}

	bool StoreClusterLodPayload(DdcStore &ddc, const ClusterLodAsset &asset,
		const ClusterLodDescriptor &descriptor, std::uint32_t vertex_count, std::string &error)
	{
		std::vector<std::uint8_t> bytes;
		if (!WriteClusterLodPayload(asset, descriptor.content_hash, vertex_count, bytes, error))
			return false;
		const std::string key = MakeClusterLodPayloadKey(descriptor);
		const std::string temporary = ddc.Root() + "/" + key + ".tmp";
		std::error_code ec;
		std::filesystem::create_directories(ddc.Root(), ec);
		if (ec)
		{
			error = "cannot create cluster DDC directory";
			return false;
		}
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				error = "cannot write cluster payload temporary file";
				return false;
			}
			stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
			if (!stream)
			{
				error = "cannot write complete cluster payload";
				return false;
			}
		}
		const std::string stored = ddc.Store(key, temporary, ".clod");
		std::filesystem::remove(temporary, ec);
		if (stored.empty())
		{
			error = "cannot store cluster payload under " + key;
			return false;
		}
		return WriteBytes(SourcePath(ddc, key), bytes, error);
	}

	ClusterLodPayloadResult LoadClusterLodPayload(const ClusterLodDescriptor &descriptor,
		std::uint32_t expected_vertex_count, DdcStore &ddc, std::string &error)
	{
		error.clear();
		const std::string key = MakeClusterLodPayloadKey(descriptor);
		std::vector<unsigned char> bytes;
		bool found = false;
		if (AssetBackend::HasMountedPak())
			found = AssetBackend::ReadPackedBytes(MakeClusterLodPackedKey(key), bytes);
		if (!found)
		{
			const std::string source = SourcePath(ddc, key);
			if (std::filesystem::exists(source))
			{
				found = AssetBackend::ReadAssetBytes(source, bytes);
				if (found && !ddc.Has(key, ".clod"))
					ddc.Store(key, source, ".clod");
			}
		}
		if (!found)
		{
			const std::string path = ddc.Lookup(key, ".clod");
			if (!path.empty())
				found = AssetBackend::ReadAssetBytes(path, bytes);
		}
		if (!found)
		{
			ClusterLodPayloadResult result;
			result.error = "missing mandatory cluster payload " + key;
			error = result.error;
			return result;
		}

		ClusterLodPayloadResult result = ReadClusterLodPayload(bytes.data(), bytes.size());
		if (!result)
		{
			error = "invalid cluster payload " + key + ": " + result.error;
			return result;
		}
		if (result.source_hash != descriptor.content_hash)
			error = "stale cluster payload source hash for " + key;
		else if (result.schema_version != descriptor.schema_version)
			error = "incompatible cluster descriptor schema for " + key;
		else if (result.vertex_count != expected_vertex_count)
			error = "cluster payload vertex count mismatch for " + key;
		else if (!SameOptions(result.asset->options, descriptor.options))
			error = "stale cluster payload build options for " + key;
		if (!error.empty())
		{
			result.asset.reset();
			result.error = error;
		}
		return result;
	}
}
