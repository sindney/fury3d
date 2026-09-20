#include "CookPipeline.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "stb_image_write.h"

#if PLATFORM_WINDOWS
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "Fury/AnimationClip.h"
#include "Fury/DdcStore.h"
#include "Fury/EntityManager.h"
#include "Fury/EnumUtil.h"
#include "Fury/FileUtil.h"
#include "Fury/HashUtil.h"
#include "Fury/Heightmap.h"
#include "Fury/Ktx2Loader.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/OceanWaves.h"
#include "Fury/ParticleSystem.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Terrain.h"
#include "Fury/Texture.h"

namespace fury
{
	int g_CookKtxInvocations = 0;

	namespace
	{
		enum class Usage { Color, ColorAlpha, Normal, Hdr };

		struct TextureEntry
		{
			std::string key;
			std::string ddcRel;
			std::string format;
			unsigned int width = 0, height = 0, mips = 0;
			bool srgb = false;
		};

		struct PassthroughEntry
		{
			std::string key;
			std::string source;
			std::string reason;
			std::string note;
		};

		std::string ToLower(const std::string& s)
		{
			std::string out = s;
			std::transform(out.begin(), out.end(), out.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			return out;
		}

		std::string ShellQuote(const std::string& s)
		{
			std::string out = "'";
			for (char c : s)
			{
				if (c == '\'')
					out += "'\\''";
				else
					out += c;
			}
			out += "'";
			return out;
		}

		// Runs a shell command with output landing in logPath. Returns the
		// process exit code, -1 when the shell could not run at all.
		int RunShell(const std::string& cmd, const std::string& logPath)
		{
			const std::string full = cmd + " > " + ShellQuote(logPath) + " 2>&1";
			const int rc = std::system(full.c_str());
#if PLATFORM_WINDOWS
			return rc;
#else
			if (rc == -1)
				return -1;
			if (WIFEXITED(rc))
				return WEXITSTATUS(rc);
			return 128;
#endif
		}

		// Last maxBytes of a log file, trailing whitespace trimmed -- the
		// error tail quoted back to the user on a ktx failure.
		std::string LogTail(const std::string& path, std::streamoff maxBytes = 4096)
		{
			std::ifstream in(path, std::ios::binary | std::ios::ate);
			if (!in)
				return "";
			const std::streamoff size = in.tellg();
			const std::streamoff start = size > maxBytes ? size - maxBytes : 0;
			in.seekg(start);
			std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			while (!data.empty() && std::isspace((unsigned char)data.back()))
				data.pop_back();
			return data;
		}

		std::filesystem::path MakeTempDir()
		{
#if PLATFORM_WINDOWS
			const long pid = _getpid();
#else
			const long pid = (long)getpid();
#endif
			static int counter = 0;
			const auto dir = std::filesystem::temp_directory_path() /
				(std::to_string(pid) + "-" + std::to_string(++counter));
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			return dir;
		}

		void RemoveTree(const std::filesystem::path& dir)
		{
			std::error_code ec;
			std::filesystem::remove_all(dir, ec);
		}

		// <exe dir>/ktx, honoring FURY_KTX_CLI first. Returns "" when absent.
		std::string ResolveKtxPath(const CookOptions& opts)
		{
			if (!opts.ktxPath.empty())
				return opts.ktxPath;

			if (const char* env = std::getenv("FURY_KTX_CLI"))
			{
				if (env[0] != '\0')
					return env;
			}

			const std::string exe = FileUtil::GetExecutablePath();
			if (exe.empty())
				return "ktx";
			return (std::filesystem::path(exe).parent_path() / "ktx").string();
		}

		std::string QueryKtxVersion(const std::string& ktx)
		{
			const auto tmp = MakeTempDir();
			const auto log = tmp / "version.txt";
			const std::string cmd = ShellQuote(ktx) + " --version";
			if (RunShell(cmd, log.string()) != 0)
			{
				RemoveTree(tmp);
				return "unknown";
			}
			std::ifstream in(log);
			std::string line((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			RemoveTree(tmp);

			// "ktx version: v4.4.2" -> take the token after the 'v' that
			// starts the numeric version (not the 'v' in "version").
			size_t start = std::string::npos;
			for (size_t i = 0; i + 1 < line.size(); ++i)
			{
				if (line[i] == 'v' && std::isdigit((unsigned char)line[i + 1]))
				{
					start = i + 1;
					break;
				}
			}
			if (start == std::string::npos)
				return "unknown";
			std::string ver = line.substr(start);
			while (!ver.empty() && (std::isspace((unsigned char)ver.back()) || ver.back() == '\r' || ver.back() == '\n'))
				ver.pop_back();
			// Keep the dotted numeric prefix only.
			size_t end = 0;
			while (end < ver.size() && (std::isdigit((unsigned char)ver[end]) || ver[end] == '.'))
				++end;
			return end > 0 ? ver.substr(0, end) : "unknown";
		}

		std::string StemOf(const std::string& scenePath)
		{
			const std::filesystem::path p(scenePath);
			return (p.parent_path() / p.stem()).string();
		}

		bool IsHdrSource(const std::string& resolvedPath)
		{
			const std::string ext = ToLower(std::filesystem::path(resolvedPath).extension().string());
			return ext == ".hdr" || ext == ".exr";
		}

		// Decodes the source once to learn its channel count; 2 or 4
		// channels carry alpha. A failed decode warns and reports opaque.
		bool DetectAlpha(const std::string& resolvedPath)
		{
			std::vector<unsigned char> pixels;
			int width = 0, height = 0, channels = 0;
			if (!FileUtil::LoadImage(resolvedPath, pixels, width, height, channels))
			{
				FURYW << "Cook: cannot decode " << resolvedPath << " for alpha detection, assuming opaque";
				return false;
			}
			return channels == 2 || channels == 4;
		}

		// slot name containing "normal" (case-insensitive) marks a normal map.
		std::unordered_map<std::string, bool> InferNormalUsage(const std::shared_ptr<EntityManager>& em)
		{
			std::unordered_map<std::string, bool> normalByPath;
			em->ForEach<Material>([&](const std::shared_ptr<Material>& material)
			{
				for (const auto& slot : material->GetTextures())
				{
					const std::shared_ptr<Texture>& texture = slot.second;
					if (!texture || texture->GetPath().empty())
						continue;
					const bool isNormal = ToLower(slot.first).find("normal") != std::string::npos;
					const auto it = normalByPath.find(texture->GetPath());
					if (it == normalByPath.end())
					{
						normalByPath.emplace(texture->GetPath(), isNormal);
					}
					else if (it->second != isNormal)
					{
						FURYW << "Cook: materials disagree on normal-map usage of " << texture->GetPath()
							<< ", keeping the first binding";
					}
				}
				return true;
			});
			return normalByPath;
		}

		// Canonical path -> usage override from <scene>.cook.json.
		std::unordered_map<std::string, Usage> LoadOverrides(const std::string& path)
		{
			std::unordered_map<std::string, Usage> overrides;
			if (path.empty() || !FileUtil::FileExist(path))
				return overrides;

			std::string json;
			if (!FileUtil::LoadString(path, json))
			{
				FURYW << "Cook: cannot read overrides file " << path << ", ignoring";
				return overrides;
			}

			rapidjson::Document dom;
			if (dom.Parse(json.c_str()).HasParseError() || !dom.IsObject())
			{
				FURYW << "Cook: overrides file " << path << " is not a JSON object, ignoring";
				return overrides;
			}

			auto parseUsage = [](const std::string& s, Usage& out) -> bool
			{
				if (s == "color") out = Usage::Color;
				else if (s == "color_alpha") out = Usage::ColorAlpha;
				else if (s == "normal") out = Usage::Normal;
				else if (s == "hdr") out = Usage::Hdr;
				else return false;
				return true;
			};

			for (auto it = dom.MemberBegin(); it != dom.MemberEnd(); ++it)
			{
				if (!it->value.IsObject() || !it->value.HasMember("usage") || !it->value["usage"].IsString())
				{
					FURYW << "Cook: override entry for " << it->name.GetString() << " lacks a usage string, skipping";
					continue;
				}
				Usage usage;
				if (!parseUsage(it->value["usage"].GetString(), usage))
				{
					FURYW << "Cook: override entry for " << it->name.GetString()
						<< " has unknown usage '" << it->value["usage"].GetString() << "', skipping";
					continue;
				}
				overrides.emplace(it->name.GetString(), usage);
			}
			return overrides;
		}

		// Transcode target for a usage, "" -> passthrough (reason/note set).
		std::string FormatForUsage(Usage usage, bool alpha, const std::string& target,
			std::string& reason, std::string& note)
		{
			reason.clear();
			note.clear();
			if (usage == Usage::Hdr)
			{
				reason = target == "legacy" ? "hdr-legacy" : "hdr-modern";
				note = target == "legacy"
					? "hdr source kept uncompressed on legacy target"
					: "ktx 4.4.2 has no uastc-hdr/bc6hu support, hdr source kept uncompressed";
				return "";
			}
			if (usage == Usage::Normal)
				return "bc5";
			if (target == "modern")
				return "bc7";
			return alpha ? "bc3" : "bc1";
		}

		// ktx create ingests png/jpg/exr/hdr/ktx only; anything else (tga,
		// bmp, ...) is decoded via stb and re-encoded to png into tmp.
		bool KtxIngestible(const std::string& path)
		{
			const std::string ext = ToLower(path.size() >= 4 ? path.substr(path.find_last_of('.')) : "");
			return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
				|| ext == ".exr" || ext == ".hdr" || ext == ".ktx" || ext == ".ktx2";
		}

		bool ConvertForKtx(const std::string& src, const std::filesystem::path& tmp,
			std::string& outPath, std::string& errOut)
		{
			if (KtxIngestible(src))
			{
				outPath = src;
				return true;
			}
			std::vector<unsigned char> pixels;
			int w = 0, h = 0, channels = 0;
			if (!FileUtil::LoadImage(src, pixels, w, h, channels))
			{
				errOut = "cannot decode source image " + src + " for ktx conversion";
				return false;
			}
			outPath = (tmp / "source.png").string();
			if (stbi_write_png(outPath.c_str(), w, h, channels, pixels.data(), 0) == 0)
			{
				errOut = "cannot write converted png for " + src;
				return false;
			}
			return true;
		}

		// create -> encode uastc -> transcode <target>. On success resultPath
		// holds the cooked ktx2 inside tmp; on failure errOut carries the
		// failing command and the tail of its log.
		bool RunKtxPipeline(const std::string& ktx, const std::string& src, bool srgb, bool normalMode,
			const std::string& target, const std::filesystem::path& tmp, bool verbose,
			std::string& resultPath, std::string& errOut)
		{
			const auto step1 = (tmp / "step1.ktx2").string();
			const auto step2 = (tmp / "step2.ktx2").string();
			const auto result = (tmp / "out.ktx2").string();

			std::string ingestible;
			if (!ConvertForKtx(src, tmp, ingestible, errOut))
				return false;

			std::string cmd = ShellQuote(ktx) + " create --format " + (srgb ? "R8G8B8A8_SRGB" : "R8G8B8A8_UNORM");
			if (!srgb)
				cmd += " --assign-tf linear";
			cmd += " --generate-mipmap " + ShellQuote(ingestible) + " " + ShellQuote(step1);
			if (verbose)
				std::cout << "[cook]   " << cmd << "\n";
			++g_CookKtxInvocations;
			if (RunShell(cmd, (tmp / "log1.txt").string()) != 0)
			{
				errOut = "ktx create failed: " + cmd + "\n" + LogTail((tmp / "log1.txt").string());
				return false;
			}

			cmd = ShellQuote(ktx) + " encode --codec uastc";
			if (normalMode)
				cmd += " --normal-mode";
			cmd += " " + ShellQuote(step1) + " " + ShellQuote(step2);
			if (verbose)
				std::cout << "[cook]   " << cmd << "\n";
			++g_CookKtxInvocations;
			if (RunShell(cmd, (tmp / "log2.txt").string()) != 0)
			{
				errOut = "ktx encode failed: " + cmd + "\n" + LogTail((tmp / "log2.txt").string());
				return false;
			}

			cmd = ShellQuote(ktx) + " transcode --target " + target + " " + ShellQuote(step2) + " " + ShellQuote(result);
			if (verbose)
				std::cout << "[cook]   " << cmd << "\n";
			++g_CookKtxInvocations;
			if (RunShell(cmd, (tmp / "log3.txt").string()) != 0)
			{
				errOut = "ktx transcode failed: " + cmd + "\n" + LogTail((tmp / "log3.txt").string());
				return false;
			}

			resultPath = result;
			return true;
		}

		// Harvests vkFormat/dims/mip count from the cooked ktx2.
		bool HarvestKtx2(const std::string& path, TextureEntry& entry, std::string& errOut)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
			{
				errOut = "cannot read cooked file " + path;
				return false;
			}
			std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			in.close();

			const Ktx2Image image = Ktx2Loader::Parse(bytes.data(), bytes.size());
			if (!image.m_Valid)
			{
				errOut = "cooked file " + path + " is not a valid ktx2: " + image.m_Error;
				return false;
			}

			const TextureFormat format = EnumUtil::TextureFormatFromVkFormat(image.m_VkFormat);
			if (format == TextureFormat::UNKNOW)
			{
				std::ostringstream oss;
				oss << "cooked file " << path << " has unsupported vkFormat " << image.m_VkFormat;
				errOut = oss.str();
				return false;
			}

			entry.format = ToLower(EnumUtil::TextureFormatToString(format));
			entry.width = image.m_Width;
			entry.height = image.m_Height;
			entry.mips = (unsigned int)image.m_Levels.size();
			return true;
		}

		bool WriteManifest(const std::string& path, const std::string& scenePath, const std::string& target,
			const std::string& toolVersion, const std::vector<TextureEntry>& textures,
			const std::vector<PassthroughEntry>& passthrough, const std::vector<std::string>& unresolved)
		{
			rapidjson::StringBuffer sb;
			rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
			writer.StartObject();

			writer.Key("scene");
			writer.String(scenePath.c_str());
			writer.Key("target");
			writer.String(target.c_str());
			writer.Key("tool");
			writer.String((std::string("ktx ") + toolVersion).c_str());

			writer.Key("textures");
			writer.StartArray();
			for (const TextureEntry& entry : textures)
			{
				writer.StartObject();
				writer.Key("key");
				writer.String(entry.key.c_str());
				writer.Key("ddc");
				writer.String(entry.ddcRel.c_str());
				writer.Key("format");
				writer.String(entry.format.c_str());
				writer.Key("width");
				writer.Uint(entry.width);
				writer.Key("height");
				writer.Uint(entry.height);
				writer.Key("mips");
				writer.Uint(entry.mips);
				writer.Key("srgb");
				writer.Bool(entry.srgb);
				writer.EndObject();
			}
			writer.EndArray();

			writer.Key("passthrough");
			writer.StartArray();
			for (const PassthroughEntry& entry : passthrough)
			{
				writer.StartObject();
				writer.Key("key");
				writer.String(entry.key.c_str());
				writer.Key("source");
				writer.String(entry.source.c_str());
				writer.Key("reason");
				writer.String(entry.reason.c_str());
				if (!entry.note.empty())
				{
					writer.Key("note");
					writer.String(entry.note.c_str());
				}
				writer.EndObject();
			}
			writer.EndArray();

			writer.Key("unresolved");
			writer.StartArray();
			for (const std::string& item : unresolved)
				writer.String(item.c_str());
			writer.EndArray();

			writer.EndObject();

			std::ofstream out(path);
			if (!out)
				return false;
			out.write(sb.GetString(), (std::streamsize)sb.GetSize());
			return (bool)out;
		}

		// fileBacked: missing resolved file counts as unresolved. Embedded
		// types (mesh/clip/particle content lives inside the scene file)
		// treat their path as provenance: resolve-or-skip, never unresolved.
		template<typename T>
		void CollectPassthrough(const std::shared_ptr<EntityManager>& em, const char* reason,
			bool fileBacked, std::vector<PassthroughEntry>& out, std::unordered_set<std::string>& unresolved)
		{
			em->ForEach<T>([&](const std::shared_ptr<T>& entity)
			{
				const std::string path = entity->GetPath();
				if (path.empty())
					return true;
				const std::string resolved = Scene::ResolveAsset(path);
				if (!FileUtil::FileExist(resolved))
				{
					if (fileBacked)
						unresolved.insert(path);
					return true;
				}
				std::string key = FileUtil::ToCanonicalAssetKey(resolved);
				if (key.empty())
					key = path;
				out.push_back({ key, resolved, reason, "" });
				return true;
			});
		}
	}

	CookResult CookSceneAssets(const std::shared_ptr<Scene>& scene, const std::string& scenePath, const CookOptions& opts)
	{
		CookResult result;
		g_CookKtxInvocations = 0;

		std::string target = opts.target;
		if (target.empty())
			target = PLATFORM_MACOS ? "legacy" : "modern";
		if (target != "legacy" && target != "modern")
		{
			result.error = "unknown cook target '" + opts.target + "' (expected legacy or modern)";
			return result;
		}

		const std::string ktx = ResolveKtxPath(opts);
		if (!FileUtil::FileExist(ktx))
		{
			result.error = "ktx tool not found at '" + ktx + "'; set FURY_KTX_CLI or pass CookOptions::ktxPath";
			return result;
		}
		const std::string ktxVersion = QueryKtxVersion(ktx);

		const std::string ddcRoot = DdcStore::ResolveRoot(opts.ddcRoot);
		DdcStore ddc(ddcRoot);

		std::string overridesPath = opts.overridesPath;
		if (overridesPath.empty())
		{
			const std::string candidate = StemOf(scenePath) + ".cook.json";
			std::error_code probeEc;
			if (std::filesystem::exists(candidate, probeEc))
				overridesPath = candidate;
		}
		const auto overrides = LoadOverrides(overridesPath);

		const std::shared_ptr<EntityManager> em = scene->GetEntityManager();
		const auto normalByPath = InferNormalUsage(em);

		// Gather unique texture paths (EntityManager path map is already
		// deduplicated; pathless entries cannot cook).
		std::vector<std::string> texturePaths;
		em->ForEach<Texture>([&](const std::shared_ptr<Texture>& texture)
		{
			const std::string path = texture->GetPath();
			if (!path.empty())
				texturePaths.push_back(path);
			return true;
		});

		// Terrain components bind splat/layer textures at render-setup time,
		// so a headless cook never sees them in the EM; collect them from the
		// node tree. Layer textures are albedo (sRGB), the splat map is data.
		std::unordered_map<std::string, bool> srgbHints;
		{
			std::unordered_set<std::string> seen(texturePaths.begin(), texturePaths.end());
			std::function<void(const std::shared_ptr<SceneNode>&)> walk = [&](const std::shared_ptr<SceneNode>& node) {
				if (!node) return;
				if (auto terrain = node->GetComponent<Terrain>())
				{
					if (!terrain->GetSplatmapPath().empty() && seen.insert(terrain->GetSplatmapPath()).second)
					{
						texturePaths.push_back(terrain->GetSplatmapPath());
						srgbHints[terrain->GetSplatmapPath()] = false;
					}
					for (int i = 0; i < 4; ++i)
					{
						const std::string& p = terrain->GetLayer(i).TexturePath;
						if (!p.empty() && seen.insert(p).second)
						{
							texturePaths.push_back(p);
							srgbHints[p] = true;
						}
					}
				}
				for (unsigned int c = 0; c < node->GetChildCount(); ++c)
					walk(node->GetChildAt(c));
			};
			walk(scene->GetRootNode());
		}

		result.texturesTotal = (int)texturePaths.size();

		std::vector<TextureEntry> textures;
		std::vector<PassthroughEntry> passthrough;
		std::unordered_set<std::string> unresolvedSet;

		for (size_t i = 0; i < texturePaths.size(); ++i)
		{
			const std::string& path = texturePaths[i];
			const std::shared_ptr<Texture> texture = em->Get<Texture>(path);

			const std::string resolved = Scene::ResolveAsset(path);
			if (!FileUtil::FileExist(resolved))
			{
				unresolvedSet.insert(path);
				std::cout << "[cook] " << (i + 1) << "/" << texturePaths.size()
					<< " unresolved " << path << "\n";
				continue;
			}

			std::string key = FileUtil::ToCanonicalAssetKey(resolved);
			if (key.empty())
				key = path;

			const bool srgb = texture ? texture->IsSRGB()
				: (srgbHints.find(path) != srgbHints.end() && srgbHints.at(path));
			const bool hasOverride = overrides.find(key) != overrides.end() || overrides.find(path) != overrides.end();
			const Usage usage = hasOverride
				? (overrides.find(key) != overrides.end() ? overrides.at(key) : overrides.at(path))
				: (IsHdrSource(resolved) ? Usage::Hdr
					: (normalByPath.find(path) != normalByPath.end() && normalByPath.at(path) ? Usage::Normal
						: Usage::Color));

			// Alpha is decoded from the source only when it changes the
			// legacy format choice; other paths never need the decode.
			const bool alpha = usage == Usage::Color && target == "legacy" && !hasOverride
				? DetectAlpha(resolved)
				: (usage == Usage::ColorAlpha);

			std::string reason, note;
			const std::string format = FormatForUsage(usage, alpha, target, reason, note);
			if (format.empty())
			{
				passthrough.push_back({ key, resolved, reason, note });
				std::cout << "[cook] " << (i + 1) << "/" << texturePaths.size()
					<< " passthrough " << key << " (" << reason << ")\n";
				continue;
			}

			const std::string settings = "v1|target=" + target + "|format=" + format +
				"|srgb=" + (srgb ? "1" : "0") + "|ktx=" + ktxVersion;
			std::string sourceSha1;
			if (!HashUtil::Sha1HexOfFile(resolved, sourceSha1))
			{
				result.error = "cannot hash source texture " + resolved;
				return result;
			}
			const std::string ddcKey = DdcStore::MakeKey("TEXTURE", settings, sourceSha1);

			TextureEntry entry;
			entry.key = key;
			entry.srgb = srgb;

			std::string stored = ddc.Lookup(ddcKey);
			if (!stored.empty())
			{
				++result.texturesDdcHits;
				if (!HarvestKtx2(stored, entry, result.error))
					return result;
				std::cout << "[cook] " << (i + 1) << "/" << texturePaths.size()
					<< " ddc-hit " << key << " (" << entry.format << ")\n";
			}
			else
			{
				const auto tmp = MakeTempDir();
				std::string cooked;
				if (!RunKtxPipeline(ktx, resolved, srgb, usage == Usage::Normal, format, tmp, opts.verbose, cooked, result.error))
				{
					RemoveTree(tmp);
					return result;
				}
				stored = ddc.Store(ddcKey, cooked);
				RemoveTree(tmp);
				if (stored.empty())
				{
					result.error = "cannot store DDC entry for " + resolved + " under " + ddcRoot;
					return result;
				}
				++result.texturesCooked;
				if (!HarvestKtx2(stored, entry, result.error))
					return result;
				std::cout << "[cook] " << (i + 1) << "/" << texturePaths.size()
					<< " cooked " << key << " (" << entry.format << ")\n";
			}

			// DDC path relative to the store root, e.g. ab/cd/<hex>.ktx2.
			const std::string rootPrefix = ddc.Root() + "/";
			entry.ddcRel = stored.compare(0, rootPrefix.size(), rootPrefix) == 0
				? stored.substr(rootPrefix.size())
				: stored;
			textures.push_back(std::move(entry));
		}

		CollectPassthrough<Mesh>(em, "mesh", false, passthrough, unresolvedSet);
		CollectPassthrough<Heightmap>(em, "heightmap", true, passthrough, unresolvedSet);
		CollectPassthrough<OceanWaves>(em, "ocean", true, passthrough, unresolvedSet);
		CollectPassthrough<ParticleSystem>(em, "particle", false, passthrough, unresolvedSet);
		CollectPassthrough<AnimationClip>(em, "animation", false, passthrough, unresolvedSet);

		// Heightmap .r16 files pair with a same-stem .json sidecar read at
		// load time; pack it when it exists on disk.
		{
			const size_t base = passthrough.size();
			for (size_t i = 0; i < base; ++i)
			{
				if (passthrough[i].reason != "heightmap")
					continue;
				const std::string sidecar = StemOf(passthrough[i].source) + ".json";
				std::error_code ec;
				if (!std::filesystem::exists(sidecar, ec))
					continue;
				std::string key = FileUtil::ToCanonicalAssetKey(sidecar);
				if (key.empty())
					key = sidecar;
				passthrough.push_back({ key, sidecar, "heightmap-sidecar", "" });
			}
		}

		// Ocean wave data: ocean.json references baked payloads via *File
		// keys (dispFile/nrmFile per band), resolved against its own dir.
		{
			const size_t base = passthrough.size();
			for (size_t i = 0; i < base; ++i)
			{
				if (passthrough[i].reason != "ocean")
					continue;
				std::string text;
				if (!FileUtil::LoadString(passthrough[i].source, text))
					continue;
				rapidjson::Document doc;
				doc.Parse(text.c_str());
				if (doc.HasParseError() || !doc.IsObject())
					continue;
				const std::string dir = std::filesystem::path(passthrough[i].source).parent_path().string();

				std::vector<std::string> files;
				std::function<void(const rapidjson::Value&)> walk = [&](const rapidjson::Value& v) {
					if (v.IsObject())
					{
						for (auto it = v.MemberBegin(); it != v.MemberEnd(); ++it)
						{
							const std::string name = it->name.GetString();
							if (name.size() >= 4 && name.compare(name.size() - 4, 4, "File") == 0
								&& it->value.IsString())
								files.push_back(it->value.GetString());
							else
								walk(it->value);
						}
					}
					else if (v.IsArray())
					{
						for (auto it = v.Begin(); it != v.End(); ++it)
							walk(*it);
					}
				};
				walk(doc);

				for (const auto& f : files)
				{
					const std::string resolved = FileUtil::NormalizePath(dir + "/" + f);
					std::error_code ec;
					if (!std::filesystem::exists(resolved, ec))
					{
						unresolvedSet.insert(f);
						continue;
					}
					std::string key = FileUtil::ToCanonicalAssetKey(resolved);
					if (key.empty())
						key = resolved;
					passthrough.push_back({ key, resolved, "ocean-payload", "" });
				}
			}
		}

		std::sort(textures.begin(), textures.end(),
			[](const TextureEntry& a, const TextureEntry& b) { return a.key < b.key; });
		std::sort(passthrough.begin(), passthrough.end(),
			[](const PassthroughEntry& a, const PassthroughEntry& b) { return a.key < b.key; });
		result.unresolved.assign(unresolvedSet.begin(), unresolvedSet.end());
		std::sort(result.unresolved.begin(), result.unresolved.end());

		std::string manifestPath = opts.manifestOut;
		if (manifestPath.empty())
			manifestPath = StemOf(scenePath) + ".cookmanifest.json";
		if (!WriteManifest(manifestPath, scenePath, target, ktxVersion, textures, passthrough, result.unresolved))
		{
			result.error = "cannot write cook manifest to " + manifestPath;
			return result;
		}
		result.manifestPath = manifestPath;

		std::cout << "[cook] done: " << result.texturesCooked << " cooked, " << result.texturesDdcHits
			<< " ddc-hits, " << result.unresolved.size() << " unresolved, "
			<< passthrough.size() << " passthrough -> " << manifestPath << "\n";

		return result;
	}
}
