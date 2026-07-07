// Cli.cpp — argv parsing, subcommand dispatch, and per-subcommand handlers.
//
// Single binary architecture: the same `fury` executable hosts both the Lua
// launcher (runtime path) and the CLI (offline asset workflows). examples/main.cpp
// dispatches to fury::Cli::Run when argv[1] matches LooksLikeSubcommand; the
// CLI path doesn't open a window, initialize the engine, or load a Lua VM.
//
// Help text shape: short top-level help that lists subcommands, plus longer
// per-subcommand help. The same strings are mirrored in docs/CLI.md so an
// AI agent reading either source gets the same surface. If the doc and the
// help strings drift, the doc wins.

#include "Fury/Cli.h"

#include "Fury/AnimationClip.h"
#include "Fury/BufferManager.h"
#include "Fury/EntityManager.h"
#include "Fury/FbxConverter.h"
#include "Fury/FileUtil.h"
#include "Fury/GltfImporter.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/OcTree.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Texture.h"
#include "Fury/ThreadUtil.h"
#include "Fury/Vector4.h"

// tinygltf — same #define dance as GltfImporter.cpp (see comment there).
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fury
{
	namespace
	{
		constexpr const char *kTopHelp =
			"fury — engine launcher and asset CLI\n"
			"\n"
			"USAGE\n"
			"  fury                            run Demo.lua (default)\n"
			"  fury <script.lua>               run the given Lua script\n"
			"  fury <subcommand> [args...]     run a CLI subcommand\n"
			"\n"
			"SUBCOMMANDS\n"
			"  convert        convert assets between formats (glTF/FBX -> engine scene.json/.bin)\n"
			"                 plus scene -> scene (.json <-> .bin)\n"
			"  info           print a CPU-side summary of a scene or asset file\n"
			"  help           show this help; `help <subcommand>` for detail\n"
			"  version    print the engine version and exit\n"
			"\n"
			"EXIT CODES\n"
			"  0  success\n"
			"  1  user error (bad arguments, unsupported input)\n"
			"  2  internal error (uncaught exception)\n"
			"\n"
			"See docs/CLI.md for full reference.\n";

		constexpr const char *kConvertHelp =
			"fury convert — convert an asset to the engine's runtime scene format\n"
			"\n"
			"USAGE\n"
			"  fury convert gltf  <input.gltf|.glb>  <output.json|.bin>\n"
			"  fury convert fbx   <input.fbx>        <output.gltf|.glb|.json|.bin>\n"
			"  fury convert scene <input.json|.bin>  <output.json|.bin>\n"
			"\n"
			"KINDS\n"
			"  gltf   load a glTF 2.0 file via tinygltf, translate to engine types,\n"
			"         and serialize via FileUtil::SaveFile (.json, human-readable)\n"
			"         or SaveCompressedFile (.bin, LZ4-compressed).\n"
			"  fbx    convert FBX -> glTF via the vendored FBX2glTF subprocess.\n"
			"         If the output extension is .gltf/.glb, stop there. If it's\n"
			"         .json/.bin, chain into the gltf importer to produce the\n"
			"         engine runtime form. Intermediate glb files in tempdir are\n"
			"         cleaned up on success and preserved (path named in error)\n"
			"         on failure of the importer step.\n"
			"  scene  engine scene -> engine scene (`.bin` <-> `.json`).\n"
			"\n"
			"LOSSY MAPPINGS (v1)\n"
			"  PBR -> Lambert: baseColorFactor -> diffuse_color (rgb) +\n"
			"    transparency (1 - a); baseColorTexture -> diffuse_texture slot;\n"
			"    emissiveFactor -> emissive_color. metallicFactor, roughnessFactor,\n"
			"    metallicRoughnessTexture, normalTexture, occlusionTexture are read\n"
			"    but discarded with one warning per source material. HDR/PBR\n"
			"    pipeline is deferred.\n"
			"  Animation time-base: glTF samples (seconds) resampled at 24 fps\n"
			"    into engine ticks. CUBICSPLINE -> LINEAR with a per-sampler warning.\n"
			"\n"
			"REJECTIONS (exit 1 with stderr message)\n"
			"  Morph targets (primitive.targets non-empty)\n"
			"  Sparse accessors (accessor.sparse.isSparse)\n"
			"  Non-default buffer-view byteStride\n"
			"  Non-triangle primitives (primitive.mode != 4)\n"
			"  Non-empty extensionsRequired\n"
			"\n"
			"EXIT CODES: 0 success, 1 user error, 2 internal error.\n";

		constexpr const char *kInfoHelp =
			"fury info — print a CPU-side summary of an asset file\n"
			"\n"
			"USAGE\n"
			"  fury info <path>\n"
			"\n"
			"SUPPORTED EXTENSIONS\n"
			"  .json   engine scene format (Serializable JSON, FileUtil::LoadFile)\n"
			"  .bin    engine scene format (LZ4-compressed Serializable JSON)\n"
			"  .gltf   glTF 2.0 ASCII\n"
			"  .glb    glTF 2.0 binary\n"
			"  .fbx    chained via FBX2glTF subprocess into a temp glb, then read\n"
			"\n"
			"OUTPUT FIELDS\n"
			"  path, format, nodes, meshes (static/skinned), submeshes, vertices,\n"
			"  triangles, materials, animations, joints, aabb (min/max).\n"
			"\n"
			"Pairs well with `convert` for verifying round-trips. The counts\n"
			"reported for engine-format inputs come from the runtime loader; for\n"
			"glTF inputs they come from tinygltf directly. They should agree\n"
			"after a successful convert (modulo material splits introduced by\n"
			"submesh-per-material).\n";

		constexpr const char *kVersionHelp =
			"fury version — print engine version\n"
			"\n"
			"USAGE\n"
			"  fury version\n";

		constexpr const char *kVersionString = "fury 0.2.1\n";

		struct SceneCounts
		{
			int nodes = 0;
			int meshes_static = 0;
			int meshes_skinned = 0;
			int submeshes = 0;
			long long vertices = 0;
			long long triangles = 0;
			int materials = 0;
			int textures = 0;
			int animations = 0;
			int joints = 0;
			int meshes_total() const { return meshes_static + meshes_skinned; }
		};

		void CountScene(const std::shared_ptr<Scene> &scene,
			SceneCounts &out,
			Vector4 &aabb_min, Vector4 &aabb_max)
		{
			if (!scene) return;
			auto root = scene->GetRootNode();

			std::function<void(const std::shared_ptr<SceneNode>&)> walk =
				[&](const std::shared_ptr<SceneNode> &node) {
				if (!node) return;
				++out.nodes;
				auto comp_render = node->GetComponent<MeshRender>();
				if (comp_render)
				{
					auto mesh = comp_render->GetMesh();
					if (mesh)
					{
						for (unsigned int s = 0; s < mesh->GetSubMeshCount(); ++s)
						{
							++out.submeshes;
							auto sub = mesh->GetSubMeshAt(s);
							if (sub) out.triangles += sub->Indices.Data.size() / 3;
						}
						out.vertices += mesh->Positions.Data.size() / 3;
						auto bounds = mesh->GetAABB();
						Vector4 wmin = node->GetWorldMatrix().Multiply(bounds.GetMin());
						Vector4 wmax = node->GetWorldMatrix().Multiply(bounds.GetMax());
						aabb_min.x = std::min(aabb_min.x, std::min(wmin.x, wmax.x));
						aabb_min.y = std::min(aabb_min.y, std::min(wmin.y, wmax.y));
						aabb_min.z = std::min(aabb_min.z, std::min(wmin.z, wmax.z));
						aabb_max.x = std::max(aabb_max.x, std::max(wmin.x, wmax.x));
						aabb_max.y = std::max(aabb_max.y, std::max(wmin.y, wmax.y));
						aabb_max.z = std::max(aabb_max.z, std::max(wmin.z, wmax.z));
					}
				}
				for (unsigned int i = 0; i < node->GetChildCount(); ++i)
					walk(node->GetChildAt(i));
			};
			walk(root);

			auto em = scene->GetEntityManager();
			em->ForEach<Mesh>([&](const Mesh::Ptr &m) -> bool {
				if (m->IsSkinnedMesh()) ++out.meshes_skinned;
				else                    ++out.meshes_static;
				out.joints += m->GetJointCount();
				return true;
			});
			em->ForEach<Material>([&](const Material::Ptr &) -> bool { ++out.materials; return true; });
			em->ForEach<Texture>([&](const Texture::Ptr &) -> bool { ++out.textures; return true; });
			em->ForEach<AnimationClip>([&](const AnimationClip::Ptr &) -> bool { ++out.animations; return true; });
		}

		// ----- helpers --------------------------------------------------------

		std::string ToLowerExt(const std::string &p)
		{
			auto dot = p.find_last_of('.');
			if (dot == std::string::npos) return {};
			std::string ext = p.substr(dot);
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return ext;
		}

		std::string StemNoExt(const std::string &p)
		{
			auto slash = p.find_last_of("/\\");
			auto base = (slash == std::string::npos) ? p : p.substr(slash + 1);
			auto dot = base.find_last_of('.');
			return (dot == std::string::npos) ? base : base.substr(0, dot);
		}

		std::string DirOf(const std::string &p)
		{
			auto slash = p.find_last_of("/\\");
			return (slash == std::string::npos) ? std::string{} : p.substr(0, slash);
		}

		bool WantsHelp(const char *arg)
		{
			return arg && (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0);
		}

		// ----- subcommand handlers --------------------------------------------

		int DoHelp(int argc, char **argv)
		{
			if (argc <= 2)
			{
				std::cout << kTopHelp;
				return 0;
			}
			const std::string topic = argv[2];
			if (topic == "convert") { std::cout << kConvertHelp; return 0; }
			if (topic == "info")    { std::cout << kInfoHelp;    return 0; }
			if (topic == "version") { std::cout << kVersionHelp; return 0; }
			std::cerr << "fury help: unknown topic '" << topic << "'\n\n" << kTopHelp;
			return 1;
		}

		int DoVersion(int argc, char **argv)
		{
			(void)argc; (void)argv;
			std::cout << kVersionString;
			return 0;
		}


		// Save a `Scene` to disk via the right FileUtil entry point based on
		// the output extension. Returns the matching CLI exit code.
		int WriteSceneByExt(const std::shared_ptr<Scene> &scene,
			const std::string &out_path)
		{
			if (!FileUtil::SaveByExtension(scene, out_path)) return 1;
			std::cout << "wrote " << out_path << "\n";
			return 0;
		}

		int DoConvert(int argc, char **argv)
		{
			if (argc < 3 || WantsHelp(argv[2]))
			{
				std::cout << kConvertHelp;
				return argc < 3 ? 1 : 0;
			}
			const std::string kind = argv[2];

			if (kind != "gltf" && kind != "fbx" && kind != "scene")
			{
				std::cerr << "fury convert: unknown kind '" << kind
					<< "' (supported: gltf, fbx, scene)\n";
				return 1;
			}
			if (argc < 5)
			{
				std::cerr << "fury convert " << kind
					<< ": expected <input> <output> arguments\n";
				return 1;
			}

			const std::string input = argv[3];
			const std::string output = argv[4];
			const std::string in_ext = ToLowerExt(input);
			const std::string out_ext = ToLowerExt(output);

			// Set the active scene so any Save paths that use Scene::Path
			// resolve relative resources correctly. Cleared at function exit.
			Scene::Active = nullptr;

			if (kind == "scene")
			{
				if (in_ext != ".json" && in_ext != ".bin")
				{
					std::cerr << "fury convert scene: input must end in .json or .bin\n";
					return 1;
				}
				if (out_ext != ".json" && out_ext != ".bin")
				{
					std::cerr << "fury convert scene: output must end in .json or .bin\n";
					return 1;
				}

				std::string working_dir = DirOf(input);
				if (!working_dir.empty()) working_dir += "/";
				auto tree = OcTree::Create();
				auto scene = Scene::Create("convert_scene", working_dir, tree);
				Scene::Active = scene;
				bool loaded = (in_ext == ".json")
					? FileUtil::LoadFile(scene, input)
					: FileUtil::LoadCompressedFile(scene, input);
				if (!loaded)
				{
					Scene::Active = nullptr;
					std::cerr << "fury convert scene: failed to load '" << input << "'\n";
					return 1;
				}
				int rc = WriteSceneByExt(scene, output);
				Scene::Active = nullptr;
				return rc;
			}

			if (kind == "gltf")
			{
				if (in_ext != ".gltf" && in_ext != ".glb")
				{
					std::cerr << "fury convert gltf: input must end in .gltf or .glb\n";
					return 1;
				}
				if (out_ext != ".json" && out_ext != ".bin")
				{
					std::cerr << "fury convert gltf: output must end in .json or .bin\n";
					return 1;
				}

				GltfImporter::Options opts;
				auto scene = GltfImporter::Import(input, StemNoExt(input),
					DirOf(output).empty() ? std::string{} : DirOf(output) + "/", opts);
				if (!scene)
				{
					std::cerr << "fury convert gltf: import failed (see prior log output)\n";
					return 1;
				}
				Scene::Active = scene;
				int rc = WriteSceneByExt(scene, output);
				Scene::Active = nullptr;
				return rc;
			}

			// kind == "fbx"
			if (in_ext != ".fbx")
			{
				std::cerr << "fury convert fbx: input must end in .fbx\n";
				return 1;
			}
			if (out_ext != ".gltf" && out_ext != ".glb"
				&& out_ext != ".json" && out_ext != ".bin")
			{
				std::cerr << "fury convert fbx: output must end in .gltf, .glb, .json, or .bin\n";
				return 1;
			}

			// Step 1: invoke FBX2glTF. For .gltf/.glb outputs we write directly
			// to the user's target dir; for .json/.bin we use a tempdir and
			// clean up afterwards.
			const bool chain_to_engine_scene = (out_ext == ".json" || out_ext == ".bin");
			std::string fbx_outdir;
			if (chain_to_engine_scene)
			{
				try { fbx_outdir = (std::filesystem::temp_directory_path()
					/ ("fury_fbx_" + StemNoExt(input))).string(); }
				catch (...) { fbx_outdir = "/tmp/fury_fbx_" + StemNoExt(input); }
				std::error_code ec;
				std::filesystem::create_directories(fbx_outdir, ec);
				(void)ec;
			}
			else
			{
				fbx_outdir = DirOf(output);
				if (fbx_outdir.empty()) fbx_outdir = ".";
			}

			std::cout << "Converting FBX -> glTF via FBX2glTF...\n";
			auto fbx_res = FbxConverter::Convert(input, fbx_outdir);
			if (!fbx_res.stdout_capture.empty()) std::cout << fbx_res.stdout_capture;
			if (!fbx_res.ok())
			{
				if (!fbx_res.stderr_capture.empty()) std::cerr << fbx_res.stderr_capture << "\n";
				std::cerr << "fury convert fbx: FBX2glTF failed (exit "
					<< fbx_res.exit_code << ")\n";
				return 1;
			}

			if (!chain_to_engine_scene)
			{
				// Move/rename the produced .glb into <output> if names differ.
				if (fbx_res.output_path != output)
				{
					std::error_code ec;
					std::filesystem::rename(fbx_res.output_path, output, ec);
					if (ec)
					{
						std::cerr << "fury convert fbx: rename '" << fbx_res.output_path
							<< "' -> '" << output << "' failed: " << ec.message() << "\n";
						return 1;
					}
				}
				// .gltf request when FBX2glTF gave us a .glb: we'd need to
				// re-export. Since we always pass --binary, --output .gltf
				// would mismatch — reject with a clear message.
				if (out_ext == ".gltf")
				{
					std::cerr << "fury convert fbx: .gltf output not supported in v1 "
						<< "(FBX2glTF is invoked with --binary -> .glb). "
						<< "Use .glb or chain through .json/.bin.\n";
					return 1;
				}
				std::cout << "wrote " << output << "\n";
				return 0;
			}

			// Step 2: chain through the glTF importer to produce the engine
			// runtime form.
			GltfImporter::Options opts;
			auto scene = GltfImporter::Import(fbx_res.output_path, StemNoExt(input),
				DirOf(output).empty() ? std::string{} : DirOf(output) + "/", opts);
			if (!scene)
			{
				std::cerr << "fury convert fbx: gltf import of intermediate '"
					<< fbx_res.output_path << "' failed; intermediate preserved for inspection\n";
				return 1;
			}
			Scene::Active = scene;
			int rc = WriteSceneByExt(scene, output);
			Scene::Active = nullptr;

			// Clean up intermediate on success (preserve on failure for inspection).
			if (rc == 0)
			{
				std::error_code ec;
				std::filesystem::remove(fbx_res.output_path, ec);
				std::filesystem::remove(fbx_outdir, ec);
				(void)ec;
			}
			return rc;
		}

		int InfoEngineScene(const std::string &path, const std::string &ext)
		{
			std::error_code ec;
			std::string working_dir = DirOf(path);
			if (!working_dir.empty()) working_dir += "/";
			auto tree = OcTree::Create();
			auto scene = Scene::Create("info", working_dir, tree);
			Scene::Active = scene;
			bool ok = (ext == ".json")
				? FileUtil::LoadFile(scene, path)
				: FileUtil::LoadCompressedFile(scene, path);
			if (!ok)
			{
				Scene::Active = nullptr;
				std::cerr << "fury info: failed to load '" << path << "'\n";
				return 1;
			}

			SceneCounts c;
			Vector4 amin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
				std::numeric_limits<float>::max(), 1.0f);
			Vector4 amax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
				-std::numeric_limits<float>::max(), 1.0f);
			CountScene(scene, c, amin, amax);

			std::printf("path:           %s\n", path.c_str());
			std::printf("format:         %s\n", ext == ".json" ? "scene-json" : "scene-bin");
			std::printf("nodes:          %d\n", c.nodes);
			std::printf("meshes:         %d  (static: %d, skinned: %d)\n",
				c.meshes_total(), c.meshes_static, c.meshes_skinned);
			std::printf("submeshes:      %d\n", c.submeshes);
			std::printf("vertices:       %lld\n", c.vertices);
			std::printf("triangles:      %lld\n", c.triangles);
			std::printf("materials:      %d\n", c.materials);
			std::printf("textures:       %d\n", c.textures);
			std::printf("animations:     %d\n", c.animations);
			std::printf("joints:         %d\n", c.joints);
			if (c.nodes > 0)
				std::printf("aabb:           min=(%g, %g, %g) max=(%g, %g, %g)\n",
					amin.x, amin.y, amin.z, amax.x, amax.y, amax.z);
			else
				std::printf("aabb:           (empty)\n");

			Scene::Active = nullptr;
			return 0;
		}

		int InfoGltf(const std::string &path, const std::string &ext)
		{
			tinygltf::TinyGLTF loader;
			loader.SetImagesAsIs(true);
			loader.SetImageLoader(
				[](tinygltf::Image *, const int, std::string *, std::string *,
					int, int, const unsigned char *, int, void *) -> bool {
					return true;
				},
				nullptr);
			tinygltf::Model model;
			std::string err, warn;
			bool ok = (ext == ".glb")
				? loader.LoadBinaryFromFile(&model, &err, &warn, path)
				: loader.LoadASCIIFromFile(&model, &err, &warn, path);
			if (!ok)
			{
				std::cerr << "fury info: failed to load '" << path << "': "
					<< (err.empty() ? "(no error)" : err) << "\n";
				return 1;
			}
			if (!warn.empty()) std::cerr << "fury info: warn: " << warn << "\n";

			int nodes = static_cast<int>(model.nodes.size());
			int meshes = static_cast<int>(model.meshes.size());
			int meshes_skinned = 0;
			for (const auto &node : model.nodes)
				if (node.skin >= 0) ++meshes_skinned;
			int meshes_static = meshes - meshes_skinned;
			int submeshes = 0;
			long long vertices = 0, triangles = 0;
			for (const auto &m : model.meshes)
			{
				submeshes += static_cast<int>(m.primitives.size());
				for (const auto &p : m.primitives)
				{
					auto it = p.attributes.find("POSITION");
					if (it != p.attributes.end()
						&& it->second >= 0
						&& it->second < static_cast<int>(model.accessors.size()))
						vertices += static_cast<long long>(model.accessors[it->second].count);
					if (p.indices >= 0 && p.indices < static_cast<int>(model.accessors.size()))
						triangles += model.accessors[p.indices].count / 3;
				}
			}
			int materials = static_cast<int>(model.materials.size());
			int anims = static_cast<int>(model.animations.size());
			int joints = 0;
			for (const auto &skin : model.skins) joints += static_cast<int>(skin.joints.size());

			std::printf("path:           %s\n", path.c_str());
			std::printf("format:         %s\n", ext == ".glb" ? "gltf-binary" : "gltf");
			std::printf("nodes:          %d\n", nodes);
			std::printf("meshes:         %d  (static: %d, skinned: %d)\n",
				meshes, meshes_static, meshes_skinned);
			std::printf("submeshes:      %d\n", submeshes);
			std::printf("vertices:       %lld\n", vertices);
			std::printf("triangles:      %lld\n", triangles);
			std::printf("materials:      %d\n", materials);
			std::printf("animations:     %d\n", anims);
			std::printf("joints:         %d\n", joints);
			std::printf("aabb:           (not computed for glTF inputs in v1)\n");
			return 0;
		}

		int InfoFbx(const std::string &path)
		{
			// Convert FBX -> temp glb via FBX2glTF, then re-dispatch to InfoGltf.
			std::string tmpdir;
			try { tmpdir = (std::filesystem::temp_directory_path()
				/ ("fury_info_" + StemNoExt(path))).string(); }
			catch (...) { tmpdir = "/tmp/fury_info_" + StemNoExt(path); }
			std::error_code ec;
			std::filesystem::create_directories(tmpdir, ec);

			auto fbx_res = FbxConverter::Convert(path, tmpdir);
			if (!fbx_res.ok())
			{
				if (!fbx_res.stderr_capture.empty()) std::cerr << fbx_res.stderr_capture << "\n";
				std::cerr << "fury info: FBX -> glTF chain failed\n";
				return 1;
			}
			int rc = InfoGltf(fbx_res.output_path, ".glb");
			std::filesystem::remove(fbx_res.output_path, ec);
			std::filesystem::remove(tmpdir, ec);
			return rc;
		}

		int DoInfo(int argc, char **argv)
		{
			if (argc < 3 || WantsHelp(argv[2]))
			{
				std::cout << kInfoHelp;
				return argc < 3 ? 1 : 0;
			}
			const std::string path = argv[2];
			const std::string ext = ToLowerExt(path);
			if (ext == ".json" || ext == ".bin") return InfoEngineScene(path, ext);
			if (ext == ".gltf" || ext == ".glb") return InfoGltf(path, ext);
			if (ext == ".fbx")                   return InfoFbx(path);
			std::cerr << "fury info: unsupported extension '" << ext
				<< "' (expected .json, .bin, .gltf, .glb, .fbx)\n";
			return 1;
		}
	}

	bool Cli::LooksLikeSubcommand(const char *arg0)
	{
		if (!arg0) return false;
		const char *tokens[] = {
			"convert", "info", "help", "--help", "-h", "version", "--version", nullptr,
		};
		for (const char **t = tokens; *t; ++t)
			if (std::strcmp(arg0, *t) == 0) return true;
		return false;
	}

	int Cli::Run(int argc, char **argv)
	{
		// The CLI path skips Engine::Initialize entirely, but FURY* logging
		// macros assume Log<0> is up. Bring it up here at WARN level
		// (console-only, no file) so importer warnings appear on stderr but
		// debug chatter doesn't drown the user.
		// ThreadUtil must be up before Log<0> — Formatter::Simple calls
		// ThreadUtil::Instance()->IsMainThread() while emitting each log
		// record, so any log after Initialize would crash on the missing
		// singleton. Pass 0 workers; the CLI doesn't enqueue tasks.
		ThreadUtil::Initialize(0);
		ThreadUtil::Instance()->SetMainThread();
		Log<0>::Initialize(LogLevel::WARN, nullptr, /*console=*/true,
			Formatter::Simple, /*append=*/false);
		// Texture::Create calls BufferManager::Instance()->Add even on the
		// CPU-only path — the converter creates Texture records as a
		// serializable shape (path + sRGB + filter/wrap) without GPU upload,
		// but the manager add-call still needs the singleton to exist.
		BufferManager::Initialize();

		if (argc < 2) { std::cout << kTopHelp; return 0; }
		try
		{
			const std::string sub = argv[1];
			if (sub == "help" || sub == "--help" || sub == "-h")   return DoHelp(argc, argv);
			if (sub == "version" || sub == "--version")            return DoVersion(argc, argv);
			if (sub == "convert")                                  return DoConvert(argc, argv);
			if (sub == "info")                                     return DoInfo(argc, argv);
			std::cerr << "fury: unknown subcommand '" << sub << "'\n\n" << kTopHelp;
			return 1;
		}
		catch (const std::exception &e)
		{
			std::cerr << "fury " << (argv[1] ? argv[1] : "?") << ": uncaught exception: "
				<< e.what() << "\n";
			return 2;
		}
		catch (...)
		{
			std::cerr << "fury " << (argv[1] ? argv[1] : "?") << ": uncaught unknown exception\n";
			return 2;
		}
	}
}
