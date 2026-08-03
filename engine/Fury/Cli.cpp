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
#include "Fury/BoxBounds.h"
#include "Fury/BufferManager.h"
#include "Fury/EntityManager.h"
#include "Fury/FbxConverter.h"
#include "Fury/FileUtil.h"
#include "Fury/GLLoader.h"
#include "Fury/GltfImporter.h"
#include "Fury/Log.h"
#include "Fury/LuaBindings.h"
#include "Fury/Material.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/ParticleSystem.h"
#include "Fury/OcTree.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/ThreadUtil.h"
#include "Fury/Vector4.h"

#include <sol/sol.hpp>

// tinygltf — same #define dance as GltfImporter.cpp (see comment there).
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>

// stb_image_write — implementation lives in Engine.cpp; we just
// need the prototype for `fury render-mesh` PNG output.
#include <stb_image_write.h>

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
			"  exec           load a scene and run a Lua script against it (headless)\n"
			"  render-mesh    render a specific mesh from a scene to a PNG (needs GL)\n"
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
			"  fury convert gltf  <input.gltf|.glb>  <output.json|.bin> [scale opts]\n"
			"  fury convert fbx   <input.fbx>        <output.gltf|.glb|.json|.bin> [scale opts]\n"
			"  fury convert scene <input.json|.bin>  <output.json|.bin> [scale opts]\n"
			"\n"
			"SCALE OPTIONS (all kinds; applied to the top-level nodes before save)\n"
			"  --auto-scale   apply the editor import heuristic: when the scene's\n"
			"                 largest bounds dimension is under 100 units (1 m),\n"
			"                 scale up by the smallest power of 100 that reaches it.\n"
			"  --scale N      multiply every top-level node's local scale by N\n"
			"                 (explicit; wins over --auto-scale).\n"
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
			"MATERIAL / ANIMATION MAPPINGS\n"
			"  PBR: baseColorFactor -> diffuse_color (rgb) + transparency (1 - a);\n"
			"    metallicFactor/roughnessFactor -> metallic_factor / roughness_factor\n"
			"    slots; baseColorTexture -> diffuse slot; emissiveFactor -> emissive.\n"
			"  alphaMode/alphaCutoff -> material alpha_mode (OPAQUE/MASK/BLEND);\n"
			"    KHR_materials_transmission -> BLEND with alpha = transmissionFactor.\n"
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

		constexpr const char *kExecHelp =
			"fury exec — load a scene and run a Lua script against it (headless)\n"
			"\n"
			"USAGE\n"
			"  fury exec <scene> <script.lua> [args...]\n"
			"\n"
			"SUPPORTED SCENE EXTENSIONS\n"
			"  .json   engine scene format (Serializable JSON)\n"
			"  .bin    engine scene format (LZ4-compressed Serializable JSON)\n"
			"  .gltf   glTF 2.0 ASCII\n"
			"  .glb    glTF 2.0 binary\n"
			"  .fbx    chained via FBX2glTF subprocess into a temp glb, then read\n"
			"\n"
			"INVARIANTS\n"
			"  No SFML window is opened. No Engine::Initialize is called. No\n"
			"  OpenGL context is created. The script runs to completion and the\n"
			"  process exits — this is a one-shot batch path, not a frame loop.\n"
			"\n"
			"SCRIPT ARGS\n"
			"  arg[0]    = <script.lua> path\n"
			"  arg[1..N] = trailing args (forwarded verbatim, no flag parsing)\n"
			"\n"
			"  Scripts that want rendering, GUI, or screenshots should use the\n"
			"  Lua launcher path with --screenshot instead.\n"
			"\n"
			"AVAILABLE LUA API\n"
			"  See docs/LUA_API.md (auto-generated from engine/Fury/LuaBindings.cpp,\n"
			"  the source of truth).\n"
			"\n"
			"EXIT CODES: 0 success, 1 user error (bad args / file / Lua error),\n"
			"2 internal error (uncaught C++ exception).\n";

		// Tiny RAII helper: swap Scene::Active to the provided scene on
		// construction, restore the previous value on destruction. Used by
		// DoExec so all exit paths (success, Lua error, C++ exception)
		// reliably reset the static. Matches the convention established by
		// Importer.LoadScene (engine/Fury/LuaBindings.cpp) and the convert/info
		// paths in this file.
		struct ActiveSceneGuard
		{
			std::shared_ptr<Scene> prev;
			explicit ActiveSceneGuard(const std::shared_ptr<Scene> &s) : prev(Scene::Active)
			{
				Scene::Active = s;
			}
			~ActiveSceneGuard() { Scene::Active = prev; }
		};

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
			int particle_systems = 0;
			int particle_renderers = 0;
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
				if (node->GetComponent<ParticleRenderer>()) ++out.particle_renderers;
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
			em->ForEach<ParticleSystem>([&](const ParticleSystem::Ptr &) -> bool { ++out.particle_systems; return true; });
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
			if (topic == "exec")    { std::cout << kExecHelp;    return 0; }
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

		// Load a scene from `<path>` based on its extension. Returns the loaded
		// Scene::Ptr or nullptr on failure (error already logged). Mirrors the
		// dispatch shape used by DoConvert / DoInfo. The engine scene loaders
		// (FileUtil::LoadFile / LoadCompressedFile) require Scene::Active to
		// be the loading target, so we set / restore it locally — same
		// pattern as Importer.LoadScene in LuaBindings.cpp. Renamed to
		// `LoadSceneForExecImpl` so the public Cli::LoadSceneForExec
		// (defined outside the anonymous namespace) can delegate here
		// without colliding on the unqualified name.
		std::shared_ptr<Scene> LoadSceneForExecImpl(const std::string &path)
		{
			const std::string ext = ToLowerExt(path);
			std::string working_dir = DirOf(path);
			if (!working_dir.empty()) working_dir += "/";

			if (ext == ".json" || ext == ".bin")
			{
				auto tree = OcTree::Create();
				auto scene = Scene::Create("exec", working_dir, tree);
				auto prev_active = Scene::Active;
				Scene::Active = scene;
				bool ok = (ext == ".json")
					? FileUtil::LoadFile(scene, path)
					: FileUtil::LoadCompressedFile(scene, path);
				Scene::Active = prev_active;
				return ok ? scene : nullptr;
			}
			if (ext == ".gltf" || ext == ".glb")
			{
				GltfImporter::Options opts;
				return GltfImporter::Import(path, StemNoExt(path),
					DirOf(path).empty() ? std::string{} : DirOf(path) + "/", opts);
			}
			if (ext == ".fbx")
			{
				// Mirror DoConvert's FBX path: invoke FBX2glTF to write a temp
				// .glb, feed it through GltfImporter, clean up on success.
				std::string tmpdir;
				try { tmpdir = (std::filesystem::temp_directory_path()
					/ ("fury_exec_" + StemNoExt(path))).string(); }
				catch (...) { tmpdir = "/tmp/fury_exec_" + StemNoExt(path); }
				std::error_code ec;
				std::filesystem::create_directories(tmpdir, ec);

				auto fbx_res = FbxConverter::Convert(path, tmpdir);
				if (!fbx_res.ok())
				{
					if (!fbx_res.stderr_capture.empty())
						std::cerr << fbx_res.stderr_capture << "\n";
					std::cerr << "fury exec: FBX2glTF failed (exit "
						<< fbx_res.exit_code << ")\n";
					return nullptr;
				}
				GltfImporter::Options opts;
				auto scene = GltfImporter::Import(fbx_res.output_path,
					StemNoExt(path), working_dir, opts);
				std::filesystem::remove(fbx_res.output_path, ec);
				std::filesystem::remove(tmpdir, ec);
				return scene;
			}
			return nullptr;
		}

		// `fury exec` — load a scene, run a Lua script against it, exit. Headless:
		// no SFML window, no Engine::Initialize, no OpenGL context, no MeshUtil
		// static primitive teardown (none of those are touched on this path).
		// `Scene::Active` is swapped for the script's lifetime and reset on
		// every exit path (success / Lua error / C++ exception) via an RAII
		// guard.
		//
		// Args: argv[2] = scene path, argv[3] = script path, argv[4..] = script
		// args (forwarded verbatim). `--help` / `-h` at argv[2] prints help
		// without touching a scene or sol::state.
		int DoExec(int argc, char **argv)
		{
			if (argc >= 3 && WantsHelp(argv[2]))
			{
				std::cout << kExecHelp;
				return 0;
			}
			if (argc < 4)
			{
				std::cerr << "fury exec: expected <scene> <script.lua> arguments\n";
				return 1;
			}

			const std::string scene_path = argv[2];
			const std::string script_path = argv[3];

			// Validate scene extension up-front so unsupported input produces
			// a clear error before we open the Lua VM.
			const std::string scene_ext = ToLowerExt(scene_path);
			if (scene_ext != ".json" && scene_ext != ".bin"
				&& scene_ext != ".gltf" && scene_ext != ".glb"
				&& scene_ext != ".fbx")
			{
				std::cerr << "fury exec: unsupported scene extension '" << scene_ext
					<< "' (expected .json, .bin, .gltf, .glb, .fbx)\n";
				return 1;
			}

			// Verify the script file exists before we load the scene — a
			// typo'd script is the more common user mistake and we'd rather
			// not load + throw away a scene for it.
			if (!FileUtil::FileExist(script_path))
			{
				std::cerr << "fury exec: script file not found: '" << script_path << "'\n";
				return 1;
			}

			try
			{
				auto scene = LoadSceneForExecImpl(scene_path);
				if (!scene)
				{
					std::cerr << "fury exec: failed to load scene '" << scene_path << "'\n";
					return 1;
				}

				// ActiveSceneGuard sets Scene::Active = scene on construction
				// and restores the previous value on destruction, covering
				// every exit path (return / exception) without duplication.
				ActiveSceneGuard active_guard(scene);

				sol::state lua;
				lua.open_libraries(
					sol::lib::base,
					sol::lib::string,
					sol::lib::math,
					sol::lib::table,
					sol::lib::io,
					sol::lib::os,
					sol::lib::package);

				fury::LuaBindings::Register(lua);

				// Build the standard Lua `arg` table. `arg[0]` is the script
				// path; `arg[1..N]` are the trailing args after the script.
				sol::table arg_tbl = lua.create_named_table("arg");
				arg_tbl[0] = script_path;
				for (int i = 4; i < argc; ++i)
					arg_tbl[i - 3] = std::string(argv[i]);

				// Run the script under sol::protected_function with an error
				// handler so a Lua-side error is captured cleanly (stderr +
				// exit 1) instead of escaping as a C++ exception.
				auto load_result = lua["loadfile"](script_path);
				if (!load_result.valid())
				{
					sol::error err = load_result;
					std::cerr << "fury exec: failed to load '" << script_path
						<< "': " << err.what() << "\n";
					return 1;
				}
				sol::protected_function script = load_result;
				if (!script.valid())
				{
					std::cerr << "fury exec: loadfile returned no function for '"
						<< script_path << "'\n";
					return 1;
				}

				auto result = script();
				if (!result.valid())
				{
					sol::error err = result;
					std::cerr << "fury exec: '" << script_path
						<< "': " << err.what() << "\n";
					return 1;
				}

				return 0;
			}
			catch (const std::exception &e)
			{
				std::cerr << "fury exec: " << e.what() << "\n";
				return 2;
			}
			catch (...)
			{
				std::cerr << "fury exec: unknown exception\n";
				return 2;
			}
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

		// Largest world-AABB dimension over all MeshRender nodes (0 when
		// the scene has no finite mesh bounds). Same walk as the
		// Scene:ComputeWorldAABB Lua binding.
		static float SceneMaxDim(const Scene::Ptr &scene)
		{
			auto root = scene ? scene->GetRootNode() : nullptr;
			if (!root) return 0.0f;
			BoxBounds total(true);
			bool any = false;
			std::function<void(const SceneNode::Ptr &)> walk =
				[&](const SceneNode::Ptr &n) {
					if (!n) return;
					if (n->GetComponent<MeshRender>())
					{
						BoxBounds wb = n->GetWorldAABB();
						if (!wb.GetInfinite()) { total.Encapsulate(wb); any = true; }
					}
					for (unsigned int i = 0; i < n->GetChildCount(); ++i)
						walk(n->GetChildAt(i));
				};
			walk(root);
			if (!any) return 0.0f;
			auto ext = total.GetMax() - total.GetMin();
			return std::max({ ext.x, ext.y, ext.z });
		}

		// Multiply every top-level node's local scale. Mirrors
		// Editor.lua's scale_import_roots (the scene root's own
		// transform stays identity, so the children carry the content).
		static void ApplyRootScale(const Scene::Ptr &scene, float factor)
		{
			auto root = scene->GetRootNode();
			for (unsigned int i = 0; i < root->GetChildCount(); ++i)
			{
				auto c = root->GetChildAt(i);
				c->SetLocalScale(c->GetLocalScale() * factor);
				c->Recompose(true);
			}
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

			// Optional scale pass: --scale N (explicit) or --auto-scale
			// (editor-import heuristic: smallest power of 100 bringing
			// the scene's largest bounds dimension to >= 100 units).
			float explicitScale = 0.0f;
			bool autoScale = false;
			for (int i = 5; i < argc; ++i)
			{
				if (std::strcmp(argv[i], "--auto-scale") == 0)
					autoScale = true;
				else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
					explicitScale = std::strtof(argv[++i], nullptr);
			}
			auto applyScaleOptions = [&](const Scene::Ptr &scene)
			{
				const float dim = SceneMaxDim(scene);
				float factor = 0.0f;
				const char* why = nullptr;
				if (explicitScale > 0.0f)
				{
					factor = explicitScale;
					why = "--scale";
				}
				else if (autoScale && dim > 0.0f && dim < 100.0f)
				{
					factor = 100.0f;
					while (dim * factor < 100.0f) factor *= 100.0f;
					why = "--auto-scale";
				}
				if (factor > 0.0f && factor != 1.0f)
				{
					ApplyRootScale(scene, factor);
					std::cout << "fury convert: applied " << why << " x" << factor
						<< " (max-dim " << dim << " -> " << dim * factor << " units)\n";
				}
				else if (autoScale)
				{
					std::cout << "fury convert: --auto-scale not needed"
						<< " (max-dim " << dim << " units)\n";
				}
			};

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
				applyScaleOptions(scene);
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
				applyScaleOptions(scene);
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
			applyScaleOptions(scene);
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
			std::printf("particles:      %d systems, %d renderers\n",
				c.particle_systems, c.particle_renderers);
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
			"convert", "info", "exec", "help", "--help", "-h", "version", "--version", nullptr,
		};
		for (const char **t = tokens; *t; ++t)
			if (std::strcmp(arg0, *t) == 0) return true;
		return false;
	}

	// Public wrapper around the file-local helper. Lets
	// launcher-side tools (e.g. `fury render-mesh`) reuse the
	// same scene-loading dispatch as `fury exec`. Body is a
	// thin delegation; the real logic lives in the anonymous
	// namespace above so DoExec can keep its direct call.
	std::shared_ptr<Scene> Cli::LoadSceneForExec(const std::string &path)
	{
		return LoadSceneForExecImpl(path);
	}

	// `fury render-mesh <scene> <mesh_name> <output.png> [--lod N]` — render a
	// mesh to a 256×256 PNG. Shares the camera + shader with the editor's
	// thumbnail via RenderMeshLambert. The launcher (main.cpp) owns the GL
	// context.
	int Cli::RenderMesh(int argc, char **argv)
	{
		if (argc < 5)
		{
			std::cerr << "fury render-mesh: expected <scene> <mesh_name> <output.png>\n"
					  << "  example: fury render-mesh Resource/Scene/scene.json T90 /tmp/t90.png\n"
					  << "           fury render-mesh Resource/Scene/scene.json T90 /tmp/t90.png --lod 1\n";
			return 1;
		}
		const std::string scene_path = argv[2];
		const std::string mesh_name = argv[3];
		const std::string output_path = argv[4];

		// Optional --lod N lets us render a specific LOD mesh. Default = 0
		// (the base mesh itself, which is what the editor's LOD picker
		// defaults to before any LODs are generated).
		int lod_index = 0;
		bool lod_explicit = false;
		for (int i = 5; i + 1 < argc; ++i)
		{
			const std::string flag = argv[i];
			if (flag == "--lod" || flag == "-l")
			{
				lod_index = std::atoi(argv[i + 1]);
				lod_explicit = true;
				++i;
			}
		}

		int exit_code = 0;
		try
		{
			auto scene = Cli::LoadSceneForExec(scene_path);
			if (!scene)
			{
				std::cerr << "fury render-mesh: failed to load scene '"
						  << scene_path << "'\n";
				return 1;
			}
			fury::Scene::Active = scene;
			auto em = scene->GetEntityManager();
			auto mesh = em ? em->Get<fury::Mesh>(mesh_name) : nullptr;
			if (!mesh)
			{
				std::cerr << "fury render-mesh: mesh '" << mesh_name
						  << "' not found in scene '" << scene_path << "'\n";
				return 1;
			}

			if (lod_explicit)
			{
				const unsigned int lod_count = mesh->GetLodCount();
				if (lod_index < 0 || static_cast<unsigned int>(lod_index) >= lod_count)
				{
					std::cerr << "fury render-mesh: --lod " << lod_index
							  << " out of range (mesh has " << lod_count
							  << " LOD(s), indices 0.." << (lod_count ? lod_count - 1 : 0)
							  << ")\n";
					return 1;
				}
				auto lod_mesh = mesh->GetLodMesh(static_cast<unsigned int>(lod_index));
				if (!lod_mesh)
				{
					std::cerr << "fury render-mesh: mesh->GetLodMesh(" << lod_index
							  << ") returned null\n";
					return 1;
				}
				mesh = lod_mesh;
			}

			const int W = 256, H = 256;
			GLuint fbo = 0;
			glGenFramebuffers(1, &fbo);
			auto color = fury::Texture::GetTemporary(W, H, 1,
				fury::TextureFormat::RGBA8, fury::TextureType::TEXTURE_2D);
			auto depth = fury::Texture::GetTemporary(W, H, 1,
				fury::TextureFormat::DEPTH24, fury::TextureType::TEXTURE_2D);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				GL_TEXTURE_2D, color->GetID(), 0);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				GL_TEXTURE_2D, depth->GetID(), 0);
			const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE)
			{
				std::cerr << "fury render-mesh: FBO incomplete (0x"
						  << std::hex << status << std::dec << ")\n";
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDeleteFramebuffers(1, &fbo);
				return 1;
			}

			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glViewport(0, 0, W, H);
			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glEnable(GL_DEPTH_TEST);

			fury::RenderMeshLambert(mesh, W, H);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glDisable(GL_DEPTH_TEST);

			// Read back + flip rows (GL origin is bottom-left, PNG is top-left).
			std::vector<unsigned char> pixels(W * H * 4);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			std::vector<unsigned char> flipped(pixels.size());
			const size_t row_bytes = W * 4;
			for (int y = 0; y < H; ++y)
				std::memcpy(&flipped[y * row_bytes],
							&pixels[(H - 1 - y) * row_bytes], row_bytes);

			const int rc = stbi_write_png(output_path.c_str(),
				W, H, 4, flipped.data(), static_cast<int>(row_bytes));
			if (rc == 0)
			{
				std::cerr << "fury render-mesh: stbi_write_png failed for '"
						  << output_path << "'\n";
				exit_code = 1;
			}
			else
			{
				FURYI << "fury render-mesh: wrote " << output_path
					  << " (mesh='" << mesh_name << "', scene='"
					  << scene_path << "')";
			}
			glDeleteFramebuffers(1, &fbo);
			fury::Scene::Active.reset();
		}
		catch (const std::exception &e)
		{
			std::cerr << "fury render-mesh: uncaught exception: " << e.what() << "\n";
			exit_code = 2;
		}
		catch (...)
		{
			std::cerr << "fury render-mesh: uncaught unknown exception\n";
			exit_code = 2;
		}
		return exit_code;
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
			if (sub == "exec")                                     return DoExec(argc, argv);
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
