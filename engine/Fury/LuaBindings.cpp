#include <sol/sol.hpp>

#include "Fury/LuaBindings.h"

#include "Fury/AnimationClip.h"
#include "Fury/Camera.h"
#include "Fury/Color.h"
#include "Fury/Component.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Engine.h"
#include "Fury/Entity.h"
#include "Fury/EntityManager.h"
#include "Fury/EnumUtil.h"
#include "Fury/FbxConverter.h"
#include "Fury/FileUtil.h"
#include "Fury/GltfImporter.h"
#include "Fury/Gui.h"
#include "Fury/InputUtil.h"
#include "Fury/Light.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/OcTree.h"
#include "Fury/Pipeline.h"
#include "Fury/PrelightPipeline.h"
#include "Fury/Quaternion.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Serializable.h"
#include "Fury/Transform.h"
#include "Fury/TypeComparable.h"
#include "Fury/Vector4.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace fury
{
	namespace LuaBindings
	{
		// Launcher options — populated by main.cpp before the script runs.
		// Engine.run reads these (and writes back the exit code).
		static LauncherEngineOptions *s_launcher_options = nullptr;

		void SetLauncherOptions(LauncherEngineOptions *options)
		{
			s_launcher_options = options;
		}

		// Convert a sol::function to a std::function<void()> that protects against
		// Lua errors and logs them via FURYE. Returns an empty std::function if the
		// sol::function is invalid.
		static std::function<void()> WrapVoid(sol::object obj)
		{
			if (!obj.valid() || obj.get_type() != sol::type::function)
				return {};
			sol::protected_function pf = obj.as<sol::protected_function>();
			return [pf]() {
				sol::protected_function_result r = pf();
				if (!r.valid())
				{
					sol::error err = r;
					FURYE << "Lua callback error: " << err.what();
				}
			};
		}

		static std::function<void(float)> WrapFloat(sol::object obj)
		{
			if (!obj.valid() || obj.get_type() != sol::type::function)
				return {};
			sol::protected_function pf = obj.as<sol::protected_function>();
			return [pf](float dt) {
				sol::protected_function_result r = pf(dt);
				if (!r.valid())
				{
					sol::error err = r;
					FURYE << "Lua callback error: " << err.what();
				}
			};
		}

		void Register(sol::state_view lua)
		{
			// --- Vector4 -------------------------------------------------------
			// We expose .x/.y/.z/.w via property getters/setters (sol2 has
			// trouble with raw member pointers in some compiler setups; the
			// property route works on every compiler we care about).
			lua.new_usertype<Vector4>("Vector4",
				sol::call_constructor,
				sol::constructors<
					Vector4(),
					Vector4(float),
					Vector4(float, float, float),
					Vector4(float, float, float, float)>(),
				"x", sol::property([](const Vector4& v) { return v.x; }, [](Vector4& v, float f) { v.x = f; }),
				"y", sol::property([](const Vector4& v) { return v.y; }, [](Vector4& v, float f) { v.y = f; }),
				"z", sol::property([](const Vector4& v) { return v.z; }, [](Vector4& v, float f) { v.z = f; }),
				"w", sol::property([](const Vector4& v) { return v.w; }, [](Vector4& v, float f) { v.w = f; }),
				"Length", &Vector4::Length,
				"SquareLength", &Vector4::SquareLength,
				"Normalize", &Vector4::Normalize,
				"Normalized", &Vector4::Normalized,
				sol::meta_function::addition,
					[](const Vector4 &a, const Vector4 &b) { return a + b; },
				sol::meta_function::subtraction,
					[](const Vector4 &a, const Vector4 &b) { return a - b; },
				sol::meta_function::multiplication,
					[](const Vector4 &a, float s) { return a * s; },
				sol::meta_function::unary_minus,
					[](const Vector4 &a) { return -a; });
			lua["Vector4"]["XAxis"] = Vector4::XAxis;
			lua["Vector4"]["YAxis"] = Vector4::YAxis;
			lua["Vector4"]["ZAxis"] = Vector4::ZAxis;

			// --- Quaternion ----------------------------------------------------
			lua.new_usertype<Quaternion>("Quaternion",
				sol::call_constructor,
				sol::constructors<
					Quaternion(),
					Quaternion(float, float, float, float)>(),
				"x", sol::property([](const Quaternion& q) { return q.x; }, [](Quaternion& q, float f) { q.x = f; }),
				"y", sol::property([](const Quaternion& q) { return q.y; }, [](Quaternion& q, float f) { q.y = f; }),
				"z", sol::property([](const Quaternion& q) { return q.z; }, [](Quaternion& q, float f) { q.z = f; }),
				"w", sol::property([](const Quaternion& q) { return q.w; }, [](Quaternion& q, float f) { q.w = f; }),
				"Identity", &Quaternion::Identity);

			// --- MathUtil ------------------------------------------------------
			sol::table math_tbl = lua.create_named_table("MathUtil");
			math_tbl["PI"] = MathUtil::PI;
			math_tbl["HalfPI"] = MathUtil::HalfPI;
			math_tbl["DegToRad"] = MathUtil::DegToRad;
			math_tbl["RadToDeg"] = MathUtil::RadToDeg;
			math_tbl["DegreeToRadian"] = &MathUtil::DegreeToRadian;
			math_tbl["RadianToDegree"] = &MathUtil::RadianToDegree;
			math_tbl["EulerRadToQuat"] = sol::overload(
				static_cast<Quaternion(*)(Vector4)>(&MathUtil::EulerRadToQuat),
				static_cast<Quaternion(*)(float, float, float)>(&MathUtil::EulerRadToQuat));

			// --- LogLevel ------------------------------------------------------
			lua.new_enum<LogLevel>("LogLevel", {
				{ "EROR", LogLevel::EROR },
				{ "WARN", LogLevel::WARN },
				{ "INFO", LogLevel::INFO },
				{ "DBUG", LogLevel::DBUG }});

			// --- SceneManager (base) ------------------------------------------
			lua.new_usertype<SceneManager>("SceneManager",
				sol::no_constructor,  // abstract; produced via OcTree::Create
				"AddSceneNodeRecursively", &SceneManager::AddSceneNodeRecursively);

			// --- Serializable (base — needed so FileUtil::Load* can accept Scene/Pipeline) -
			lua.new_usertype<Serializable>("Serializable",
				sol::no_constructor);

			// --- Entity (base for Scene, Pipeline) ----------------------------
			lua.new_usertype<Entity>("Entity",
				sol::no_constructor,
				sol::base_classes, sol::bases<Serializable>());

			// --- OcTree --------------------------------------------------------
			lua.new_usertype<OcTree>("OcTree",
				sol::no_constructor,
				sol::base_classes, sol::bases<SceneManager>(),
				"Create", sol::overload(
					[]() { return OcTree::Create(); },
					[](unsigned int maxDepth) { return OcTree::Create(maxDepth); },
					static_cast<OcTree::Ptr(*)(Vector4, Vector4, unsigned int)>(&OcTree::Create)
				));

			// --- Scene ---------------------------------------------------------
			lua.new_usertype<Scene>("Scene",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"Create", &Scene::Create,
				"Clear", &Scene::Clear,
				"GetRootNode", &Scene::GetRootNode,
				"GetSceneManager", &Scene::GetSceneManager,
				"GetEntityManager", &Scene::GetEntityManager,
				"GetWorkingDir", &Scene::GetWorkingDir,
				"SetWorkingDir", &Scene::SetWorkingDir);
			// Scene::Active static property — same convention as Pipeline.Active.
			lua["Scene"]["GetActive"] = []() -> Scene::Ptr { return Scene::Active; };
			lua["Scene"]["SetActive"] = [](Scene::Ptr p) { Scene::Active = p; };

			// --- Component (base) ---------------------------------------------
			lua.new_usertype<Component>("Component",
				sol::no_constructor,
				sol::base_classes, sol::bases<Serializable>());

			// --- Transform -----------------------------------------------------
			lua.new_usertype<Transform>("Transform",
				sol::no_constructor,
				sol::base_classes, sol::bases<Component, Serializable>(),
				"Create", sol::overload(
					static_cast<Transform::Ptr(*)()>(&Transform::Create),
					static_cast<Transform::Ptr(*)(Vector4, Quaternion, Vector4)>(&Transform::Create)));

			// --- Camera --------------------------------------------------------
			lua.new_usertype<Camera>("Camera",
				sol::no_constructor,
				sol::base_classes, sol::bases<Component, Serializable>(),
				"Create", &Camera::Create,
				"PerspectiveFov", &Camera::PerspectiveFov,
				"GetNear", &Camera::GetNear,
				"GetFar", &Camera::GetFar,
				"GetShadowFar", &Camera::GetShadowFar,
				"SetShadowFar", &Camera::SetShadowFar,
				"SetShadowBounds", &Camera::SetShadowBounds);

			// --- Color ---------------------------------------------------------
			lua.new_usertype<Color>("Color",
				sol::call_constructor,
				sol::constructors<Color(float, float, float, float)>());

			// --- LightType + Light --------------------------------------------
			// LightType enum exposed as a plain Lua table so scripts can write
			// `light:SetType(LightType.DIRECTIONAL)`.
			sol::table light_type_tbl = lua.create_named_table("LightType");
			light_type_tbl["DIRECTIONAL"] = static_cast<int>(LightType::DIRECTIONAL);
			light_type_tbl["POINT"]       = static_cast<int>(LightType::POINT);
			light_type_tbl["SPOT"]        = static_cast<int>(LightType::SPOT);

			lua.new_usertype<Light>("Light",
				sol::no_constructor,
				sol::base_classes, sol::bases<Component, Serializable>(),
				"Create", &Light::Create,
				"GetType",      &Light::GetType,
				"SetType",      [](Light &l, int t) { l.SetType(static_cast<LightType>(t)); },
				"GetColor",     &Light::GetColor,
				"SetColor",     &Light::SetColor,
				"GetIntensity", &Light::GetIntensity,
				"SetIntensity", &Light::SetIntensity,
				"GetRadius",    &Light::GetRadius,
				"SetRadius",    &Light::SetRadius,
				"SetCastShadows", &Light::SetCastShadows,
				"CalculateAABB",  &Light::CalculateAABB);

			// --- SceneNode -----------------------------------------------------
			lua.new_usertype<SceneNode>("SceneNode",
				sol::no_constructor,
				"Create", &SceneNode::Create,
				"GetWorldPosition", &SceneNode::GetWorldPosition,
				"GetLocalPosition", &SceneNode::GetLocalPosition,
				"SetLocalPosition", sol::overload(
					static_cast<void(SceneNode::*)(Vector4)>(&SceneNode::SetLocalPosition),
					static_cast<void(SceneNode::*)(float, float, float)>(&SceneNode::SetLocalPosition)),
				"SetLocalRoattion", sol::overload(
					static_cast<void(SceneNode::*)(Quaternion)>(&SceneNode::SetLocalRoattion),
					static_cast<void(SceneNode::*)(float, float, float)>(&SceneNode::SetLocalRoattion),
					static_cast<void(SceneNode::*)(Vector4, float)>(&SceneNode::SetLocalRoattion)),
				"SetLocalScale", sol::overload(
					static_cast<void(SceneNode::*)(Vector4)>(&SceneNode::SetLocalScale),
					static_cast<void(SceneNode::*)(float)>(&SceneNode::SetLocalScale)),
				"Recompose", &SceneNode::Recompose,
				"AddComponent", &SceneNode::AddComponent,
				"AddChild", &SceneNode::AddChild,
				"GetChildCount", &SceneNode::GetChildCount,
				"GetChildAt", &SceneNode::GetChildAt,
				// Convenience: pull the Light component (if any) so Lua can
				// inspect / mutate the light without needing template-style
				// GetComponent<T>() bindings. Returns nil when absent.
				"GetLight", [](SceneNode &n) -> std::shared_ptr<Light> {
					return n.GetComponent<Light>();
				});

			// --- Pipeline ------------------------------------------------------
			lua.new_usertype<Pipeline>("Pipeline",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"SetCurrentCamera", &Pipeline::SetCurrentCamera,
				"Execute", &Pipeline::Execute);
			// Static property — Lua scripts read/write Pipeline.Active. Use
			// explicit getter/setter functions because sol::property on the
			// usertype's class table doesn't reliably round-trip in sol2 v3.5.
			lua["Pipeline"]["GetActive"] = []() -> Pipeline::Ptr { return Pipeline::Active; };
			lua["Pipeline"]["SetActive"] = [](Pipeline::Ptr p) { Pipeline::Active = p; };

			// --- PrelightPipeline ---------------------------------------------
			lua.new_usertype<PrelightPipeline>("PrelightPipeline",
				sol::no_constructor,
				sol::base_classes, sol::bases<Pipeline, Entity, Serializable>(),
				"Create", &PrelightPipeline::Create);

			// --- FileUtil (free functions in a Lua table) ---------------------
			sol::table fu_tbl = lua.create_named_table("FileUtil");
			fu_tbl["GetAbsPath"] = sol::overload(
				static_cast<std::string(*)()>(&FileUtil::GetAbsPath),
				[](const std::string &source) { return FileUtil::GetAbsPath(source, false); },
				static_cast<std::string(*)(const std::string&, bool)>(&FileUtil::GetAbsPath));
			fu_tbl["FileExist"] = &FileUtil::FileExist;
			// Bind separate, distinctly-named functions for each Serializable subtype.
			// sol2's overload resolution from Lua usertype to a C++ shared_ptr<Base>
			// is fragile in v3.5; concrete signatures sidestep that.
			fu_tbl["LoadSceneFromCompressedFile"] = [](const std::shared_ptr<Scene> &s, const std::string &p) {
				return FileUtil::LoadCompressedFile(s, p);
			};
			fu_tbl["LoadPipelineFromFile"] = [](const std::shared_ptr<Pipeline> &p, const std::string &path) {
				return FileUtil::LoadFile(p, path);
			};
			// Save Scenes back to disk. Used by `Save Scene As` in Demo.lua's
			// File menu. SaveFile -> human-readable JSON; SaveCompressedFile -> LZ4 .bin.
			fu_tbl["SaveFile"] = [](const std::shared_ptr<Scene> &s, const std::string &p) {
				return FileUtil::SaveFile(s, p);
			};
			fu_tbl["SaveCompressedFile"] = [](const std::shared_ptr<Scene> &s, const std::string &p) {
				return FileUtil::SaveCompressedFile(s, p);
			};
			// Enumerate a directory, with an optional case-insensitive extension
			// filter (a Lua array of strings like {".gltf", ".fbx"}). Hidden
			// files (leading '.') and subdirectories are excluded. Returns
			// a Lua array of relative filenames (no path prefix). Empty array
			// on missing path (with a warning logged).
			fu_tbl["ListDirectory"] = [&lua](const std::string &path, sol::object filter_obj) {
				sol::table out = lua.create_table();
				std::vector<std::string> filters;
				if (filter_obj.valid() && filter_obj.get_type() == sol::type::table)
				{
					sol::table t = filter_obj;
					for (size_t i = 1; i <= t.size(); ++i)
					{
						auto e = t.get<std::string>(i);
						std::transform(e.begin(), e.end(), e.begin(),
							[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						filters.push_back(e);
					}
				}
				std::error_code ec;
				if (!std::filesystem::exists(path, ec) || ec)
				{
					FURYW << "FileUtil.ListDirectory: path '" << path << "' does not exist";
					return out;
				}
				int idx = 1;
				for (const auto &entry : std::filesystem::directory_iterator(path, ec))
				{
					if (!entry.is_regular_file(ec)) continue;
					std::string name = entry.path().filename().string();
					if (name.empty() || name[0] == '.') continue;
					if (!filters.empty())
					{
						std::string ext = entry.path().extension().string();
						std::transform(ext.begin(), ext.end(), ext.begin(),
							[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						bool match = false;
						for (const auto &f : filters)
							if (ext == f) { match = true; break; }
						if (!match) continue;
					}
					out[idx++] = name;
				}
				return out;
			};

			// --- Importer (runtime asset import: glTF / FBX / engine scene) -
			sol::table importer_tbl = lua.create_named_table("Importer");
			// Import a .gltf or .glb into a fresh Scene::Ptr. Returns nil on
			// error (logged via FURYE before return). The Scene's working_dir
			// is the input file's directory so relative texture URIs resolve.
			importer_tbl["LoadGltf"] = [](const std::string &path) -> std::shared_ptr<Scene> {
				try
				{
					auto slash = path.find_last_of("/\\");
					std::string working = (slash == std::string::npos) ? std::string{} : path.substr(0, slash + 1);
					return GltfImporter::Import(path, path, working, {});
				}
				catch (const std::exception &e)
				{
					FURYE << "Importer.LoadGltf threw: " << e.what();
					return nullptr;
				}
			};
			// Import an .fbx through the FBX2glTF subprocess + the glTF importer.
			// Cleans up the intermediate .glb in tempdir on success. Blocks for
			// the duration of the FBX2glTF run (no progress reporting in v1).
			importer_tbl["LoadFbx"] = [](const std::string &path) -> std::shared_ptr<Scene> {
				try
				{
					// FBX2glTF writes its intermediate .glb to a temp dir; the
					// directory is cleaned up after the importer consumes it.
					// Embedded image bytes flow through Texture::CreateFromMemory
					// (no on-disk extraction at import time).
					std::string tmpdir;
					try { tmpdir = (std::filesystem::temp_directory_path()
						/ "fury_runtime_fbx").string(); }
					catch (...) { tmpdir = "/tmp/fury_runtime_fbx"; }
					std::error_code ec;
					std::filesystem::create_directories(tmpdir, ec);
					auto res = FbxConverter::Convert(path, tmpdir);
					if (!res.ok())
					{
						FURYE << "Importer.LoadFbx: FBX2glTF failed (exit " << res.exit_code
							<< "): " << res.stderr_capture;
						return nullptr;
					}
					auto slash = path.find_last_of("/\\");
					std::string working = (slash == std::string::npos) ? std::string{} : path.substr(0, slash + 1);
					auto scene = GltfImporter::Import(res.output_path, path, working, {});
					std::filesystem::remove(res.output_path, ec);
					return scene;
				}
				catch (const std::exception &e)
				{
					FURYE << "Importer.LoadFbx threw: " << e.what();
					return nullptr;
				}
			};
			// Convenience: pick the right loader based on file extension.
			// Recognized: .json (LoadFile), .bin (LoadCompressedFile),
			// .gltf/.glb (LoadGltf), .fbx (LoadFbx). Anything else -> nil.
			importer_tbl["LoadScene"] = [&lua](const std::string &path) -> std::shared_ptr<Scene> {
				auto dot = path.find_last_of('.');
				std::string ext = (dot == std::string::npos) ? "" : path.substr(dot);
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (ext == ".json" || ext == ".bin")
				{
					// Resolve the scene's working_dir to the directory
					// containing the input scene file. This is what the
					// engine's relative-path resolution (Texture::CreateFromImage
					// -> Scene::Path) joins texture paths against. For scenes
					// produced by `convert fbx ... .json` the textures are
					// extracted as siblings (bare filenames), so the working
					// dir must be the scene file's directory for them to
					// resolve. For hand-authored scenes that use prefixed
					// paths (e.g. "Resource/Scene/foo.jpg") the working dir
					// can still be anything that combines correctly.
					std::string working;
					{
						auto slash = path.find_last_of("/\\");
						if (slash != std::string::npos)
							working = path.substr(0, slash + 1);
					}
					if (working.empty())
						working = FileUtil::GetAbsPath();
					auto scene = Scene::Create("imported", working);
					// MeshRender::Load and friends resolve mesh/material
					// references against Scene::Active->GetEntityManager()
					// (see Scene::Manager). When the demo is mid-edit (e.g.
					// after File -> New cleared the active scene), that
					// EntityManager is empty and the load would fail with
					// "Mesh ... not found!". Swap Scene::Active to the
					// import target for the duration of the load.
					auto prev_active = Scene::Active;
					Scene::Active = scene;
					bool ok = (ext == ".json")
						? FileUtil::LoadFile(scene, path)
						: FileUtil::LoadCompressedFile(scene, path);
					Scene::Active = prev_active;
					return ok ? scene : nullptr;
				}
				if (ext == ".gltf" || ext == ".glb")
					return lua["Importer"]["LoadGltf"](path);
				if (ext == ".fbx")
					return lua["Importer"]["LoadFbx"](path);
				FURYW << "Importer.LoadScene: unsupported extension '" << ext << "'";
				return nullptr;
			};
			// Merge source -> target: append source's root children to target's
			// root and transfer source's entities into target's EntityManager
			// (duplicate-by-hashcode entries are dropped with a warning). Then
			// re-register the new subtrees with the target's SceneManager so
			// they're visible to the renderer. Returns the count of merged
			// top-level children.
			importer_tbl["MergeInto"] = [](const std::shared_ptr<Scene> &target,
				const std::shared_ptr<Scene> &source) -> int
			{
				if (!target || !source) return 0;
				int merged = 0;
				auto target_root = target->GetRootNode();
				auto source_root = source->GetRootNode();
				// Detach children from source root and attach to target root.
				// We pop them in reverse so AddChild's indexing is stable.
				while (source_root->GetChildCount() > 0)
				{
					auto child = source_root->GetChildAt(source_root->GetChildCount() - 1);
					source_root->RemoveChild(child);
					target_root->AddChild(child);
					++merged;
				}
				// Transfer entities. EntityManager::Add dedupes by hash; we
				// just trust that and forward.
				auto target_em = target->GetEntityManager();
				auto source_em = source->GetEntityManager();
				source_em->ForEach<Material>([&](const std::shared_ptr<Material> &m) -> bool {
					target_em->Add(m); return true;
				});
				source_em->ForEach<Mesh>([&](const std::shared_ptr<Mesh> &m) -> bool {
					target_em->Add(m); return true;
				});
				source_em->ForEach<AnimationClip>([&](const std::shared_ptr<AnimationClip> &c) -> bool {
					target_em->Add(c); return true;
				});
				target->GetSceneManager()->AddSceneNodeRecursively(target_root);
				return merged;
			};

			// --- Gui (free functions in a Lua table) --------------------------
			sol::table gui_tbl = lua.create_named_table("Gui");
			gui_tbl["ShowDefault"]         = &Gui::ShowDefault;
			gui_tbl["Render"]              = &Gui::Render;
			gui_tbl["WantCaptureMouse"]    = &Gui::WantCaptureMouse;
			gui_tbl["WantCaptureKeyboard"] = &Gui::WantCaptureKeyboard;
			gui_tbl["Begin"]               = &Gui::Begin;
			gui_tbl["End"]                 = &Gui::End;
			gui_tbl["SliderFloat"]         = &Gui::SliderFloat;
			gui_tbl["Checkbox"]            = &Gui::Checkbox;
			gui_tbl["Button"]              = &Gui::Button;
			gui_tbl["Separator"]           = &Gui::Separator;
			gui_tbl["Text"]                = &Gui::Text;
			gui_tbl["BeginMenu"]           = &Gui::BeginMenu;
			gui_tbl["EndMenu"]             = &Gui::EndMenu;
			gui_tbl["MenuItem"]            = &Gui::MenuItem;
			// Text input. Returns the edited string (in/out shape matching
			// SliderFloat / Checkbox). The label doubles as the InputText id;
			// max_len bounds the user input.
			gui_tbl["InputText"]           = &Gui::InputText;
			// Register a Lua-side menu-bar callback. Pass nil to clear.
			gui_tbl["SetMenuBarCallback"] = [](sol::object obj) {
				if (!obj.valid() || obj.get_type() != sol::type::function)
				{
					Gui::SetMenuBarCallback({});
					return;
				}
				sol::protected_function pf = obj.as<sol::protected_function>();
				Gui::SetMenuBarCallback([pf]() {
					sol::protected_function_result r = pf();
					if (!r.valid())
					{
						sol::error err = r;
						FURYE << "Lua menu-bar callback error: " << err.what();
					}
				});
			};

			// --- Window (engine window control) ------------------------------
			// The engine no longer renders a built-in File -> Quit. Scripts
			// own the File menu and use Window.Close() to terminate the
			// engine. Idempotent.
			sol::table window_tbl = lua.create_named_table("Window");
			window_tbl["Close"] = &Gui::CloseWindow;

			// --- Editor (C++-owned editor shell, behind WITH_EDITOR) ---------
			// When WITH_EDITOR is on, scripts route File-menu policy, console
			// commands, scene tree, and camera settings through the editor.
			// When off (no -DWITH_EDITOR), every Editor.* call is a safe no-op
			// so the same script runs unchanged in both build modes.
			sol::table editor_tbl = lua.create_named_table("Editor");
#ifdef WITH_EDITOR
			editor_tbl["SetSceneIO"] = [](sol::table tbl) {
				Editor::SceneIO io;
				if (auto v = tbl["list_files"]; v.valid() && v.get_type() == sol::type::function)
				{
					sol::protected_function pf = v;
					io.list_files = [pf]() -> std::vector<std::string> {
						std::vector<std::string> out;
						sol::protected_function_result r = pf();
						if (!r.valid()) { sol::error e = r; FURYE << "Editor list_files error: " << e.what(); return out; }
						sol::object obj = r;
						if (obj.is<sol::table>())
						{
							sol::table t = obj;
							for (size_t i = 1; i <= t.size(); ++i) out.push_back(t.get<std::string>(i));
						}
						return out;
					};
				}
				if (auto v = tbl["on_new"]; v.valid() && v.get_type() == sol::type::function)
				{
					sol::protected_function pf = v;
					io.on_new = [pf]() {
						sol::protected_function_result r = pf();
						if (!r.valid()) { sol::error e = r; FURYE << "Editor on_new error: " << e.what(); }
					};
				}
				if (auto v = tbl["on_open"]; v.valid() && v.get_type() == sol::type::function)
				{
					sol::protected_function pf = v;
					io.on_open = [pf](const std::string& p) {
						sol::protected_function_result r = pf(p);
						if (!r.valid()) { sol::error e = r; FURYE << "Editor on_open error: " << e.what(); }
					};
				}
				if (auto v = tbl["on_import"]; v.valid() && v.get_type() == sol::type::function)
				{
					sol::protected_function pf = v;
					io.on_import = [pf](const std::string& p) {
						sol::protected_function_result r = pf(p);
						if (!r.valid()) { sol::error e = r; FURYE << "Editor on_import error: " << e.what(); }
					};
				}
				if (auto v = tbl["on_save_as"]; v.valid() && v.get_type() == sol::type::function)
				{
					sol::protected_function pf = v;
					io.on_save_as = [pf](const std::string& p) {
						sol::protected_function_result r = pf(p);
						if (!r.valid()) { sol::error e = r; FURYE << "Editor on_save_as error: " << e.what(); }
					};
				}
				if (auto v = tbl["scene_dir"]; v.valid() && v.get_type() == sol::type::function)
				{
					sol::protected_function pf = v;
					io.scene_dir = [pf]() -> std::string {
						sol::protected_function_result r = pf();
						if (!r.valid()) { sol::error e = r; FURYE << "Editor scene_dir error: " << e.what(); return {}; }
						sol::object obj = r;
						if (obj.is<std::string>()) return obj.as<std::string>();
						return {};
					};
				}
				Editor::SetSceneIO(std::move(io));
			};

			editor_tbl["SetSceneTreeProvider"] = [](sol::object obj) {
				if (!obj.valid() || obj.get_type() != sol::type::function)
				{
					Editor::ClearSceneTreeProvider();
					return;
				}
				sol::protected_function pf = obj.as<sol::protected_function>();
				Editor::SetSceneTreeProvider([pf]() -> Editor::TreeNode {
					Editor::TreeNode root;
					sol::protected_function_result r = pf();
					if (!r.valid()) { sol::error e = r; FURYE << "Editor tree provider error: " << e.what(); return root; }
					sol::object res = r;
					if (!res.is<sol::table>()) return root;
					std::function<void(const sol::table&, Editor::TreeNode&)> walk =
						[&](const sol::table& t, Editor::TreeNode& tn) {
							tn.name = t.get_or<std::string>("name", "");
							sol::object kids = t["children"];
							if (kids.is<sol::table>())
							{
								sol::table kt = kids;
								for (size_t i = 1; i <= kt.size(); ++i)
								{
									sol::object child = kt[i];
									if (child.is<sol::table>())
									{
										Editor::TreeNode c;
										walk(child.as<sol::table>(), c);
										tn.children.push_back(std::move(c));
									}
								}
							}
						};
					walk(res.as<sol::table>(), root);
					return root;
				});
			};

			editor_tbl["SetCommandHandler"] = [](sol::object obj) {
				if (!obj.valid() || obj.get_type() != sol::type::function)
				{
					Editor::ClearCommandHandler();
					return;
				}
				sol::protected_function pf = obj.as<sol::protected_function>();
				Editor::SetCommandHandler([pf](const std::string& line) {
					sol::protected_function_result r = pf(line);
					if (!r.valid()) { sol::error e = r; FURYE << "Editor command handler error: " << e.what(); }
				});
			};

			editor_tbl["SetCameraSettings"] = [](sol::table tbl) {
				std::vector<Editor::CameraControl> out;
				sol::object controls_obj = tbl["controls"];
				if (!controls_obj.is<sol::table>()) { Editor::ClearCameraControls(); return; }
				sol::table controls = controls_obj;
				for (size_t i = 1; i <= controls.size(); ++i)
				{
					sol::object e = controls[i];
					if (!e.is<sol::table>()) continue;
					sol::table ct = e;
					Editor::CameraControl cc;
					cc.label = ct.get_or<std::string>("label", "");
					cc.kind  = ct.get_or<std::string>("kind", "slider");
					{
						sol::object mn = ct["min"];
						sol::object mx = ct["max"];
						cc.vmin = (mn.valid() && mn.is<float>()) ? mn.as<float>() : 0.0f;
						cc.vmax = (mx.valid() && mx.is<float>()) ? mx.as<float>() : 1.0f;
					}
					if (cc.kind == "checkbox")
					{
						sol::object g = ct["get"], s = ct["set"];
						if (g.is<sol::protected_function>())
						{
							sol::protected_function pf = g;
							cc.get_b = [pf]() -> bool {
								sol::protected_function_result r = pf();
								if (!r.valid()) { sol::error e = r; FURYE << "camera get error: " << e.what(); return false; }
								return ((sol::object)r).as<bool>();
							};
						}
						if (s.is<sol::protected_function>())
						{
							sol::protected_function pf = s;
							cc.set_b = [pf](bool v) {
								sol::protected_function_result r = pf(v);
								if (!r.valid()) { sol::error e = r; FURYE << "camera set error: " << e.what(); }
							};
						}
					}
					else
					{
						sol::object g = ct["get"], s = ct["set"];
						if (g.is<sol::protected_function>())
						{
							sol::protected_function pf = g;
							cc.get_f = [pf]() -> float {
								sol::protected_function_result r = pf();
								if (!r.valid()) { sol::error e = r; FURYE << "camera get error: " << e.what(); return 0.0f; }
								return ((sol::object)r).as<float>();
							};
						}
						if (s.is<sol::protected_function>())
						{
							sol::protected_function pf = s;
							cc.set_f = [pf](float v) {
								sol::protected_function_result r = pf(v);
								if (!r.valid()) { sol::error e = r; FURYE << "camera set error: " << e.what(); }
							};
						}
					}
					out.push_back(std::move(cc));
				}
				Editor::SetCameraControls(std::move(out));
			};

			editor_tbl["Log"]                 = [](const std::string& level, const std::string& text) {
				Editor::Log(level.c_str(), text.c_str());
			};
			editor_tbl["GetSelectedSceneNode"] = []() -> SceneNode* { return Editor::GetSelectedSceneNode(); };
			editor_tbl["SetWindowVisible"]    = [](const std::string& name, bool v) { Editor::SetWindowVisible(name.c_str(), v); };
			editor_tbl["GetWindowVisible"]    = [](const std::string& name) -> bool { return Editor::GetWindowVisible(name.c_str()); };
			editor_tbl["SetImportFlag"]       = [](const std::string& name, bool v) { Editor::SetImportFlag(name.c_str(), v); };
			editor_tbl["GetImportFlag"]       = sol::overload(
				[](const std::string& name) -> bool { return Editor::GetImportFlag(name.c_str(), false); },
				[](const std::string& name, bool d) -> bool { return Editor::GetImportFlag(name.c_str(), d); });
#else
			// No-op stubs so user scripts that reference Editor.* compose
			// with both build modes. Each accepts and discards arguments.
			editor_tbl["SetSceneIO"]            = [](sol::object) {};
			editor_tbl["SetSceneTreeProvider"]  = [](sol::object) {};
			editor_tbl["SetCommandHandler"]     = [](sol::object) {};
			editor_tbl["SetCameraSettings"]     = [](sol::object) {};
			editor_tbl["Log"]                   = [](sol::object, sol::object) {};
			editor_tbl["GetSelectedSceneNode"]  = []() -> sol::object { return sol::nil; };
			editor_tbl["SetWindowVisible"]      = [](sol::object, sol::object) {};
			editor_tbl["GetWindowVisible"]      = [](sol::object) -> bool { return false; };
			editor_tbl["SetImportFlag"]         = [](sol::object, sol::object) {};
			editor_tbl["GetImportFlag"]         = sol::overload(
				[](sol::object) -> bool { return false; },
				[](sol::object, bool d) -> bool { return d; });
#endif

			// --- RenderUtil (singleton; no methods bound this round) ----------
			lua.new_usertype<RenderUtil>("RenderUtil",
				sol::no_constructor);
			lua["RenderUtil"]["Instance"] = []() { return RenderUtil::Instance(); };

			// --- InputUtil (singleton, polling-style accessors) ---------------
			// Enums are exposed as plain integer tables (Key, MouseButton) so
			// users can extend them by adding lua["Key"]["F11"] = ... lines
			// without touching sol2's typed-enum machinery.
			lua.new_usertype<InputUtil>("InputUtil",
				sol::no_constructor,
				"GetKeyDown",
					[](InputUtil &self, int key) {
						return self.GetKeyDown(static_cast<sf::Keyboard::Key>(key));
					},
				"GetMouseDown",
					sol::overload(
						[](InputUtil &self) { return self.GetMouseDown(); },
						[](InputUtil &self, int btn) {
							return self.GetMouseDown(static_cast<sf::Mouse::Button>(btn));
						}),
				"GetMousePosition",
					[](InputUtil &self) {
						auto p = self.GetMousePosition();
						return std::make_tuple(p.first, p.second);
					},
				"GetMouseWheel",    &InputUtil::GetMouseWheel,
				"GetWindowFocused", &InputUtil::GetWindowFocused,
				"GetWindowSize",
					[](InputUtil &self) {
						int w = 0, h = 0;
						self.GetWindowSize(w, h);
						return std::make_tuple(w, h);
					});
			lua["InputUtil"]["Instance"] = []() { return InputUtil::Instance(); };

			// --- Key / MouseButton enum tables --------------------------------
			sol::table key_tbl = lua.create_named_table("Key");
			key_tbl["A"] = static_cast<int>(sf::Keyboard::Key::A);
			key_tbl["B"] = static_cast<int>(sf::Keyboard::Key::B);
			key_tbl["C"] = static_cast<int>(sf::Keyboard::Key::C);
			key_tbl["D"] = static_cast<int>(sf::Keyboard::Key::D);
			key_tbl["E"] = static_cast<int>(sf::Keyboard::Key::E);
			key_tbl["F"] = static_cast<int>(sf::Keyboard::Key::F);
			key_tbl["G"] = static_cast<int>(sf::Keyboard::Key::G);
			key_tbl["H"] = static_cast<int>(sf::Keyboard::Key::H);
			key_tbl["I"] = static_cast<int>(sf::Keyboard::Key::I);
			key_tbl["J"] = static_cast<int>(sf::Keyboard::Key::J);
			key_tbl["K"] = static_cast<int>(sf::Keyboard::Key::K);
			key_tbl["L"] = static_cast<int>(sf::Keyboard::Key::L);
			key_tbl["M"] = static_cast<int>(sf::Keyboard::Key::M);
			key_tbl["N"] = static_cast<int>(sf::Keyboard::Key::N);
			key_tbl["O"] = static_cast<int>(sf::Keyboard::Key::O);
			key_tbl["P"] = static_cast<int>(sf::Keyboard::Key::P);
			key_tbl["Q"] = static_cast<int>(sf::Keyboard::Key::Q);
			key_tbl["R"] = static_cast<int>(sf::Keyboard::Key::R);
			key_tbl["S"] = static_cast<int>(sf::Keyboard::Key::S);
			key_tbl["T"] = static_cast<int>(sf::Keyboard::Key::T);
			key_tbl["U"] = static_cast<int>(sf::Keyboard::Key::U);
			key_tbl["V"] = static_cast<int>(sf::Keyboard::Key::V);
			key_tbl["W"] = static_cast<int>(sf::Keyboard::Key::W);
			key_tbl["X"] = static_cast<int>(sf::Keyboard::Key::X);
			key_tbl["Y"] = static_cast<int>(sf::Keyboard::Key::Y);
			key_tbl["Z"] = static_cast<int>(sf::Keyboard::Key::Z);
			key_tbl["Num0"] = static_cast<int>(sf::Keyboard::Key::Num0);
			key_tbl["Num1"] = static_cast<int>(sf::Keyboard::Key::Num1);
			key_tbl["Num2"] = static_cast<int>(sf::Keyboard::Key::Num2);
			key_tbl["Num3"] = static_cast<int>(sf::Keyboard::Key::Num3);
			key_tbl["Num4"] = static_cast<int>(sf::Keyboard::Key::Num4);
			key_tbl["Num5"] = static_cast<int>(sf::Keyboard::Key::Num5);
			key_tbl["Num6"] = static_cast<int>(sf::Keyboard::Key::Num6);
			key_tbl["Num7"] = static_cast<int>(sf::Keyboard::Key::Num7);
			key_tbl["Num8"] = static_cast<int>(sf::Keyboard::Key::Num8);
			key_tbl["Num9"] = static_cast<int>(sf::Keyboard::Key::Num9);
			key_tbl["Space"]     = static_cast<int>(sf::Keyboard::Key::Space);
			key_tbl["LShift"]    = static_cast<int>(sf::Keyboard::Key::LShift);
			key_tbl["RShift"]    = static_cast<int>(sf::Keyboard::Key::RShift);
			key_tbl["LControl"]  = static_cast<int>(sf::Keyboard::Key::LControl);
			key_tbl["RControl"]  = static_cast<int>(sf::Keyboard::Key::RControl);
			key_tbl["LAlt"]      = static_cast<int>(sf::Keyboard::Key::LAlt);
			key_tbl["RAlt"]      = static_cast<int>(sf::Keyboard::Key::RAlt);
			key_tbl["Up"]        = static_cast<int>(sf::Keyboard::Key::Up);
			key_tbl["Down"]      = static_cast<int>(sf::Keyboard::Key::Down);
			key_tbl["Left"]      = static_cast<int>(sf::Keyboard::Key::Left);
			key_tbl["Right"]     = static_cast<int>(sf::Keyboard::Key::Right);
			key_tbl["Escape"]    = static_cast<int>(sf::Keyboard::Key::Escape);
			key_tbl["Enter"]     = static_cast<int>(sf::Keyboard::Key::Enter);
			key_tbl["Tab"]       = static_cast<int>(sf::Keyboard::Key::Tab);
			key_tbl["Backspace"] = static_cast<int>(sf::Keyboard::Key::Backspace);

			sol::table mb_tbl = lua.create_named_table("MouseButton");
			mb_tbl["Left"]   = static_cast<int>(sf::Mouse::Button::Left);
			mb_tbl["Right"]  = static_cast<int>(sf::Mouse::Button::Right);
			mb_tbl["Middle"] = static_cast<int>(sf::Mouse::Button::Middle);

			// --- Engine.run ---------------------------------------------------
			// The launcher injects the active sf::Window into lua["__window"] as
			// a userdata pointer; we read it back here and dispatch to Engine::Run.
			//
			// Lua signature: Engine.run(callbacks [, options])
			//   callbacks: { on_init, on_update, on_fixed_update, on_shutdown }
			//   options:   { max_fps, gui_scale, gui_font_scale }  (all optional)
			sol::table engine_tbl = lua.create_named_table("Engine");
			engine_tbl["run"] = [&lua](sol::table cb_table, sol::optional<sol::table> opt_table) {
				sf::Window* window = lua["__window"].get<sf::Window*>();
				if (!window)
				{
					FURYE << "Engine.run: __window is not set; the launcher must inject it before calling Engine.run.";
					return;
				}
				EngineCallbacks cb;
				cb.OnInit         = WrapVoid(cb_table["on_init"]);
				cb.OnUpdate       = WrapFloat(cb_table["on_update"]);
				cb.OnFixedUpdate  = WrapVoid(cb_table["on_fixed_update"]);
				cb.OnShutdown     = WrapVoid(cb_table["on_shutdown"]);

				EngineOptions opts;
				if (opt_table)
				{
					sol::table o = *opt_table;
					// max_fps: accept number, or boolean false → 0.
					sol::object mf = o["max_fps"];
					if (mf.valid())
					{
						if (mf.get_type() == sol::type::boolean)
							opts.max_fps = mf.as<bool>() ? opts.max_fps : 0;
						else if (mf.get_type() == sol::type::number)
							opts.max_fps = mf.as<int>();
					}
					opts.gui_scale      = o.get_or("gui_scale", opts.gui_scale);
					opts.gui_font_scale = o.get_or("gui_font_scale", opts.gui_font_scale);
				}

				// Layer launcher-supplied options over the script's. Launcher
				// wins for the screenshot fields (those are user-facing flags
				// that the script has no business overriding); the script's
				// values for max_fps / gui_scale stay intact.
				if (s_launcher_options && !s_launcher_options->screenshot_path.empty())
				{
					opts.screenshot_path = s_launcher_options->screenshot_path;
					opts.screenshot_frame = s_launcher_options->screenshot_frame;
					opts.exit_code_out = &s_launcher_options->exit_code;
				}

				Engine::Run(*window, cb, opts);

				// Clear the menu-bar callback before sol::state destruction;
				// the bound sol::protected_function holds a Lua-state ref that
				// would dangle otherwise.
				Gui::SetMenuBarCallback({});
#ifdef WITH_EDITOR
				// Same hazard for Editor's Lua-captured callbacks.
				Editor::ClearSceneIO();
				Editor::ClearSceneTreeProvider();
				Editor::ClearCommandHandler();
				Editor::ClearCameraControls();
#endif
			};
		}
	}
}
