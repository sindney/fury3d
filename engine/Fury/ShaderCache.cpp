#include "Fury/ShaderCache.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "Fury/AssetBackend.h"
#include "Fury/DdcStore.h"
#include "Fury/FileUtil.h"
#include "Fury/HashUtil.h"

namespace fury
{
	int g_ShaderCompilerInvocations = 0;

	namespace
	{
		const char *kCompilerVersion = "slang-2026.19+spirv-cross-1.4.363.0";

		bool ResolveIncludes(const std::string &src, const std::string &dir,
			std::vector<std::string> &stack, int depth, std::string &out, std::string &error)
		{
			if (depth > 8)
			{
				error = "shader include depth exceeded at " + dir;
				return false;
			}
			std::stringstream stream(src);
			std::string line;
			while (std::getline(stream, line))
			{
				const size_t start = line.find_first_not_of(" \t");
				if (start != std::string::npos && line.compare(start, 8, "#include") == 0)
				{
					const size_t open = line.find('"', start + 8);
					const size_t close = open == std::string::npos ? std::string::npos : line.find('"', open + 1);
					if (open == std::string::npos || close == std::string::npos)
					{
						error = "malformed shader include: " + line;
						return false;
					}
					const std::string path = dir + line.substr(open + 1, close - open - 1);
					if (std::find(stack.begin(), stack.end(), path) != stack.end())
					{
						error = "shader include cycle at " + path;
						return false;
					}
					std::string includeSource;
					if (!FileUtil::LoadString(path, includeSource))
					{
						error = "shader include not found: " + path;
						return false;
					}
					stack.push_back(path);
					std::string resolved;
					const std::string childDir = path.substr(0, path.find_last_of('/') + 1);
					if (!ResolveIncludes(includeSource, childDir, stack, depth + 1, resolved, error))
						return false;
					stack.pop_back();
					std::stringstream includeStream(resolved);
					std::string includeLine;
					while (std::getline(includeStream, includeLine))
					{
						const size_t version = includeLine.find_first_not_of(" \t");
						if (version != std::string::npos && includeLine.compare(version, 8, "#version") == 0)
							continue;
						out += includeLine + '\n';
					}
					continue;
				}
				out += line + '\n';
			}
			return true;
		}

		struct VersionedSource
		{
			std::string version;
			std::string body;
		};

		VersionedSource SplitVersion(const std::string &source)
		{
			VersionedSource out;
			const size_t index = source.find("#version");
			if (index != std::string::npos)
			{
				const size_t eol = source.find('\n', index);
				if (eol != std::string::npos)
					out.version = source.substr(index, eol - index + 1);
			}
			if (out.version.empty())
			{
				out.version = "#version 330 core";
				out.body = source;
			}
			else
			{
				const size_t start = index + out.version.size();
				out.body = source.substr(0, index) + source.substr(start);
			}
			return out;
		}

		std::vector<std::string> ArtifactExtensions(ShaderTarget target)
		{
			switch (target)
			{
			case ShaderTarget::OpenGL:
				return { ".vert.glsl", ".frag.glsl", ".refl" };
			case ShaderTarget::Vulkan:
				return { ".vert.spv", ".frag.spv", ".refl" };
			case ShaderTarget::Metal:
				return { ".vert.metal", ".frag.metal", ".refl" };
			}
			return {};
		}

		std::string RelativePath(const DdcStore &ddc, const std::string &key, const std::string &ext)
		{
			const auto path = std::filesystem::path(ddc.PathForKey(key, ext));
			const auto relative = path.lexically_relative(std::filesystem::path(ddc.Root()));
			if (!relative.empty() && *relative.begin() != "..")
				return relative.generic_string();
			return path.generic_string();
		}

		bool ReadArtifact(const DdcStore &ddc, ShaderArtifact &artifact)
		{
			const std::string rel = RelativePath(ddc, artifact.key, artifact.extension);
			if (AssetBackend::ReadPackedBytes("DDC/" + rel, artifact.bytes))
				return true;

			std::ifstream stream(ddc.PathForKey(artifact.key, artifact.extension), std::ios::binary);
			if (!stream)
				return false;
			artifact.bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
			return stream.good() || stream.eof();
		}

		void AddString(rapidjson::Value &object, const char *name, const std::string &value,
			rapidjson::Document::AllocatorType &allocator)
		{
			rapidjson::Value key(name, allocator);
			rapidjson::Value str(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
			object.AddMember(key, str, allocator);
		}

		rapidjson::Value SerializeStage(const ShaderStageCompile &stage,
			rapidjson::Document::AllocatorType &allocator)
		{
			rapidjson::Value out(rapidjson::kObjectType);
			AddString(out, "globalsBlockName", stage.globalsBlockName, allocator);
			out.AddMember("globalsBlockSize", static_cast<uint64_t>(stage.globalsBlockSize), allocator);
			out.AddMember("globalsBlockIndex", stage.globalsBlockIndex, allocator);
			rapidjson::Value globals(rapidjson::kArrayType);
			for (const auto &member : stage.globals)
			{
				rapidjson::Value item(rapidjson::kObjectType);
				AddString(item, "name", member.name, allocator);
				item.AddMember("offset", static_cast<uint64_t>(member.offset), allocator);
				item.AddMember("size", static_cast<uint64_t>(member.size), allocator);
				rapidjson::Value defaults(rapidjson::kArrayType);
				for (unsigned char byte : member.defaultValue)
					defaults.PushBack(static_cast<unsigned int>(byte), allocator);
				item.AddMember("defaults", defaults, allocator);
				globals.PushBack(item, allocator);
			}
			out.AddMember("globals", globals, allocator);
			rapidjson::Value inputs(rapidjson::kArrayType);
			for (const auto &input : stage.stageInputs)
			{
				rapidjson::Value item(rapidjson::kObjectType);
				AddString(item, "name", input.name, allocator);
				item.AddMember("location", input.location, allocator);
				inputs.PushBack(item, allocator);
			}
			out.AddMember("stageInputs", inputs, allocator);
			return out;
		}

		std::vector<unsigned char> SerializeReflection(const ShaderModuleCompile &compiled)
		{
			rapidjson::Document doc;
			doc.SetObject();
			auto &allocator = doc.GetAllocator();
			doc.AddMember("version", 2, allocator);
			doc.AddMember("vertex", SerializeStage(compiled.vertex, allocator), allocator);
			doc.AddMember("fragment", SerializeStage(compiled.fragment, allocator), allocator);
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			doc.Accept(writer);
			const char *text = buffer.GetString();
			return std::vector<unsigned char>(text, text + buffer.GetSize());
		}

		bool ReadString(const rapidjson::Value &object, const char *name, std::string &out)
		{
			if (!object.HasMember(name) || !object[name].IsString())
				return false;
			out.assign(object[name].GetString(), object[name].GetStringLength());
			return true;
		}

		bool DeserializeStage(const rapidjson::Value &value, ShaderStageCompile &out)
		{
			if (!value.IsObject() || !ReadString(value, "globalsBlockName", out.globalsBlockName)
				|| !value.HasMember("globalsBlockSize") || !value["globalsBlockSize"].IsUint64()
				|| !value.HasMember("globalsBlockIndex") || !value["globalsBlockIndex"].IsInt()
				|| !value.HasMember("globals") || !value["globals"].IsArray()
				|| !value.HasMember("stageInputs") || !value["stageInputs"].IsArray())
				return false;
			out.globalsBlockSize = static_cast<size_t>(value["globalsBlockSize"].GetUint64());
			out.globalsBlockIndex = value["globalsBlockIndex"].GetInt();
			for (const auto &item : value["globals"].GetArray())
			{
				ShaderStageCompile::Member member;
				if (!item.IsObject() || !ReadString(item, "name", member.name)
					|| !item.HasMember("offset") || !item["offset"].IsUint64()
					|| !item.HasMember("size") || !item["size"].IsUint64())
					return false;
				member.offset = static_cast<size_t>(item["offset"].GetUint64());
				member.size = static_cast<size_t>(item["size"].GetUint64());
				if (!item.HasMember("defaults") || !item["defaults"].IsArray())
					return false;
				for (const auto &byte : item["defaults"].GetArray())
				{
					if (!byte.IsUint() || byte.GetUint() > 255)
						return false;
					member.defaultValue.push_back(static_cast<unsigned char>(byte.GetUint()));
				}
				if (member.offset + member.size > out.globalsBlockSize
					|| member.defaultValue.size() > member.size)
					return false;
				out.globals.push_back(std::move(member));
			}
			for (const auto &item : value["stageInputs"].GetArray())
			{
				ShaderStageCompile::StageInput input;
				if (!item.IsObject() || !ReadString(item, "name", input.name)
					|| !item.HasMember("location") || !item["location"].IsInt())
					return false;
				input.location = item["location"].GetInt();
				out.stageInputs.push_back(std::move(input));
			}
			return true;
		}

		bool DeserializeReflection(const std::vector<unsigned char> &bytes, ShaderModuleCompile &out)
		{
			rapidjson::Document doc;
			doc.Parse(reinterpret_cast<const char *>(bytes.data()), bytes.size());
			return !doc.HasParseError() && doc.IsObject() && doc.HasMember("version")
				&& doc["version"].IsInt() && doc["version"].GetInt() == 2
				&& doc.HasMember("vertex") && doc.HasMember("fragment")
				&& DeserializeStage(doc["vertex"], out.vertex)
				&& DeserializeStage(doc["fragment"], out.fragment);
		}

		ShaderArtifact TextArtifact(const std::string &key, const std::string &ext, const std::string &text)
		{
			ShaderArtifact artifact;
			artifact.key = key;
			artifact.extension = ext;
			artifact.bytes.assign(text.begin(), text.end());
			return artifact;
		}
	}

	const char *ShaderTargetName(ShaderTarget target)
	{
		switch (target)
		{
		case ShaderTarget::OpenGL: return "opengl";
		case ShaderTarget::Vulkan: return "vulkan";
		case ShaderTarget::Metal: return "metal";
		}
		return "unknown";
	}

	bool ParseShaderTarget(const std::string &name, ShaderTarget &target)
	{
		if (name == "opengl") target = ShaderTarget::OpenGL;
		else if (name == "vulkan") target = ShaderTarget::Vulkan;
		else if (name == "metal") target = ShaderTarget::Metal;
		else return false;
		return true;
	}

	bool LoadShaderSource(const std::string &path, std::string &source, std::string &error)
	{
		std::string input;
		if (!FileUtil::LoadString(path, input))
		{
			error = "cannot read shader " + path;
			return false;
		}
		const std::string dir = path.substr(0, path.find_last_of('/') + 1);
		std::vector<std::string> stack{ path };
		source.clear();
		return ResolveIncludes(input, dir, stack, 0, source, error);
	}

	std::string BuildShaderStageSource(const std::string &source, const std::string &stageDefine,
		const std::vector<std::string> &defines, bool withEditor, bool withDebugOverlay)
	{
		VersionedSource split = SplitVersion(source);
		if (std::find(defines.begin(), defines.end(), "INSTANCE_SSBO") != defines.end())
			split.version = "#version 430 core\n";
		std::string out = split.version + "\n#define " + stageDefine + "\n";
		if (withEditor)
			out += "#define WITH_EDITOR\n";
		if (withDebugOverlay)
			out += "#define WITH_DBG_OVERLAY\n";
		for (const auto &define : defines)
			out += "#define " + define + "\n";
		return out + split.body;
	}

	ShaderCompileInput BuildShaderCompileInput(const std::string &name, const std::string &source,
		const std::vector<std::string> &defines, bool withEditor, bool withDebugOverlay)
	{
		ShaderCompileInput input;
		input.name = name;
		input.vertexSource = BuildShaderStageSource(source, "VERTEX_SHADER", defines, withEditor, withDebugOverlay);
		input.fragmentSource = BuildShaderStageSource(source, "FRAGMENT_SHADER", defines, withEditor, withDebugOverlay);
		return input;
	}

	std::string MakeShaderDdcKey(const ShaderCompileInput &input, ShaderTarget target)
	{
		std::string settings = std::string("backend=") + ShaderTargetName(target)
			+ "|compiler=" + kCompilerVersion
			+ "|master=" + std::to_string(FURY_SHADER_DDC_MASTER_VERSION);
		for (const auto &define : input.defines)
			settings += "|define=" + define.first + "=" + define.second;
		for (const auto &path : input.searchPaths)
			settings += "|include=" + path;
		std::string source = input.vertexSource;
		source.push_back('\0');
		source += input.fragmentSource;
		return DdcStore::MakeKey("SHADER_", settings, HashUtil::Sha1Hex(source));
	}

	std::vector<ShaderArtifact> MakeShaderArtifacts(const ShaderCompileInput &input,
		const ShaderModuleCompile &compiled, ShaderTarget target)
	{
		const std::string key = MakeShaderDdcKey(input, target);
		std::vector<ShaderArtifact> out;
		if (target == ShaderTarget::OpenGL)
		{
			out.push_back(TextArtifact(key, ".vert.glsl", input.vertexSource));
			out.push_back(TextArtifact(key, ".frag.glsl", input.fragmentSource));
		}
		else if (target == ShaderTarget::Vulkan)
		{
			out.push_back({ key, ".vert.spv", compiled.vertex.spirv });
			out.push_back({ key, ".frag.spv", compiled.fragment.spirv });
		}
		else
		{
			out.push_back(TextArtifact(key, ".vert.metal", compiled.vertex.msl));
			out.push_back(TextArtifact(key, ".frag.metal", compiled.fragment.msl));
		}
		out.push_back({ key, ".refl", SerializeReflection(compiled) });
		return out;
	}

	std::string ShaderArtifactPakKey(const ShaderArtifact &artifact, const DdcStore &ddc)
	{
		return "DDC/" + RelativePath(ddc, artifact.key, artifact.extension);
	}

	bool StoreShaderArtifacts(DdcStore &ddc, const std::vector<ShaderArtifact> &artifacts, std::string &error)
	{
		static std::atomic<unsigned int> s_NextTemp{ 0 };
		const auto tempDir = std::filesystem::temp_directory_path() / "fury_shader_ddc";
		std::error_code ec;
		std::filesystem::create_directories(tempDir, ec);
		if (ec)
		{
			error = "cannot create shader DDC staging directory: " + ec.message();
			return false;
		}
		for (const auto &artifact : artifacts)
		{
			const auto temp = tempDir / (artifact.key + artifact.extension + "."
				+ std::to_string(++s_NextTemp) + ".tmp");
			{
				std::ofstream stream(temp, std::ios::binary);
				stream.write(reinterpret_cast<const char *>(artifact.bytes.data()), artifact.bytes.size());
				if (!stream)
				{
					error = "cannot stage shader artifact " + temp.string();
					return false;
				}
			}
			const std::string stored = ddc.Store(artifact.key, temp.string(), artifact.extension);
			std::filesystem::remove(temp, ec);
			if (stored.empty())
			{
				error = "cannot store shader artifact " + artifact.key + artifact.extension;
				return false;
			}
		}
		return true;
	}

	bool LoadShaderArtifacts(const DdcStore &ddc, const std::string &key, ShaderTarget target,
		std::vector<ShaderArtifact> &artifacts)
	{
		artifacts.clear();
		for (const auto &ext : ArtifactExtensions(target))
		{
			ShaderArtifact artifact{ key, ext, {} };
			if (!ReadArtifact(ddc, artifact))
			{
				artifacts.clear();
				return false;
			}
			artifacts.push_back(std::move(artifact));
		}
		return true;
	}

	bool CompileOrLoadMetalShader(const ShaderCompileInput &input, ShaderModuleCompile &out,
		bool *cacheHit, std::string &error)
	{
		DdcStore ddc(DdcStore::ResolveRoot());
		const std::string key = MakeShaderDdcKey(input, ShaderTarget::Metal);
		std::vector<ShaderArtifact> artifacts;
		if (LoadShaderArtifacts(ddc, key, ShaderTarget::Metal, artifacts))
		{
			out.vertex.msl.assign(artifacts[0].bytes.begin(), artifacts[0].bytes.end());
			out.fragment.msl.assign(artifacts[1].bytes.begin(), artifacts[1].bytes.end());
			if (DeserializeReflection(artifacts[2].bytes, out))
			{
				out.ok = true;
				if (cacheHit) *cacheHit = true;
				return true;
			}
			out = ShaderModuleCompile{};
		}

		if (cacheHit) *cacheHit = false;
		++g_ShaderCompilerInvocations;
		if (!CompileGlslToMSL(input, out))
		{
			error = out.error;
			return false;
		}
		if (!StoreShaderArtifacts(ddc, MakeShaderArtifacts(input, out, ShaderTarget::Metal), error))
			return false;
		return true;
	}
}
