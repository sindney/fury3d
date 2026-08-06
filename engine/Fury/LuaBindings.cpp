#include <sol/sol.hpp>

#include "Fury/LuaBindings.h"

#include "Fury/AnimationClip.h"
#include "Fury/AnimationPlayer.h"
#include "Fury/AnimationState.h"
#include "Fury/AnimationUtil.h"
#include "Fury/Camera.h"
#include "Fury/Color.h"
#include "Fury/Component.h"
#include "Fury/Editor/Editor.h"
#include "Fury/Editor/EditorAssetWindows.h"
#include "Fury/Editor/EditorConfirmDialog.h"
#include "Fury/Editor/EditorParticleWindow.h"
#include "Fury/Engine.h"
#include "Fury/Entity.h"
#include "Fury/EntityManager.h"
#include "Fury/EnumUtil.h"
#include "Fury/FbxConverter.h"
#include "Fury/FileUtil.h"
#include "Fury/GltfImporter.h"
#include "Fury/PostProcessEffect.h"
#include "Fury/PostProcessRegistry.h"
#include "Fury/RenderSettings.h"

#include "Fury/Gui.h"
#include "Fury/InputUtil.h"
#include "Fury/Joint.h"
#include "Fury/Light.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/MeshRender.h"
#include "Fury/ParticleRenderer.h"
#include "Fury/ParticleSystem.h"
#include "Fury/MeshSimplifier.h"
#include "Fury/MeshUtil.h"
#include "Fury/OcTree.h"
#include "Fury/Pipeline.h"
#include "Fury/PrelightPipeline.h"
#include "Fury/Quaternion.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Serializable.h"
#include "Fury/Texture.h"
#include "Fury/Uniform.h"
#include "Fury/Transform.h"
#include "Fury/TypeComparable.h"
#include "Fury/Vector4.h"

#ifdef WITH_EDITOR
// nativefiledialog-extended backs the Editor.OpenDialog / Editor.SaveDialog
// Lua bindings. Lives under engine/ThirdParty/nfd as a git submodule; its
// CMake target nfd::nfd is linked into `fury` only on WITH_EDITOR=ON.
#include "nfd.h"
#endif

#include <algorithm>
#include <filesystem>
#include <vector>

namespace fury
{
	namespace LuaBindings
	{
		// Launcher options -- populated by main.cpp before the script runs.
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

			// --- Serializable (base -- needed so FileUtil::Load* can accept Scene/Pipeline) -
			lua.new_usertype<Serializable>("Serializable",
				sol::no_constructor);

			// --- Entity (base for Scene, Pipeline) ----------------------------
			lua.new_usertype<Entity>("Entity",
				sol::no_constructor,
				sol::base_classes, sol::bases<Serializable>());

			// --- OcTree --------------------------------------------------------
			// Static-cast all three overloads to function pointers. With
			// the lambda form, sol2 returns a raw `std::shared_ptr<OcTree>`
			// from the lambda's auto-deduced return type and never registers
			// the upcast registry that lets `sol::bases<SceneManager>` map
			// `shared_ptr<OcTree>` -> `shared_ptr<SceneManager>` -- Scene.Create
			// then rejects the userdata with "unrecognized userdata".
			lua.new_usertype<OcTree>("OcTree",
				sol::no_constructor,
				sol::base_classes, sol::bases<SceneManager>(),
				"Create", sol::overload(
					static_cast<OcTree::Ptr(*)()>(&OcTree::Create),
					static_cast<OcTree::Ptr(*)(unsigned int)>(&OcTree::Create),
					static_cast<OcTree::Ptr(*)(Vector4, Vector4, unsigned int)>(&OcTree::Create)
				));

			// --- Scene ---------------------------------------------------------
			// Wrap `Create` so the third argument is taken as the concrete
			// `OcTree::Ptr` (the only `SceneManager` derivative lua ever
			// holds) and explicitly upcast in C++. Going through
			// `&Scene::Create` directly relies on sol2's
			// `sol::bases<SceneManager>` upcast registry to translate the
			// OcTree userdata into a `shared_ptr<SceneManager>` -- which
			// fails at runtime with "unrecognized userdata" against the
			// vendored sol2 we ship. The explicit upcast restores the
			// `Scene.Create(name, dir, octree)` call shape lua expects.
			lua.new_usertype<Scene>("Scene",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"Create", sol::overload(
					[](const std::string &name, const std::string &dir) {
						return Scene::Create(name, dir);
					},
					[](const std::string &name, const std::string &dir, OcTree::Ptr octree) {
						return Scene::Create(name, dir, std::static_pointer_cast<SceneManager>(octree));
					}),
				"Clear", &Scene::Clear,
				"GetRootNode", &Scene::GetRootNode,
				"GetSceneManager", &Scene::GetSceneManager,
				"GetEntityManager", &Scene::GetEntityManager,
				// Register a script-created Material so it saves with the
				// scene (imports register their own; without this a
				// Material.Create'd material is GC'd and missing on reload).
				"AddMaterial", [](Scene &s, const Material::Ptr &m) {
					if (auto em = s.GetEntityManager()) em->Add(m);
				},
				"AddMesh", [](Scene &s, const Mesh::Ptr &m) {
					if (auto em = s.GetEntityManager()) em->Add(m);
				},
				// Name lookup for particle system assets -- smoke tests and
				// automation assert against these (there is no generic
				// EntityManager.Get exposed to Lua).
				"GetParticleSystem", [](Scene &s, const std::string &name) -> ParticleSystem::Ptr {
					if (auto em = s.GetEntityManager()) return em->Get<ParticleSystem>(name);
					return nullptr;
				},
				"AddParticleSystem", [](Scene &s, const ParticleSystem::Ptr &p) {
					if (auto em = s.GetEntityManager()) em->Add(p);
				},
				"GetRenderSettings", &Scene::GetRenderSettings,
				"GetWorkingDir", &Scene::GetWorkingDir,
				"SetWorkingDir", &Scene::SetWorkingDir,
				// Union of every mesh-bearing node's world AABB -- the
				// editor's import unit-scale detection reads this. Returns
				// `min, max` (Vector4) or nil when the scene has no finite
				// mesh bounds (empty / lights-only).
				"ComputeWorldAABB", [](Scene &self, sol::this_state ts) {
					sol::state_view lua(ts);
					auto none = std::make_tuple(sol::make_object(lua, sol::nil),
												sol::make_object(lua, sol::nil));
					auto root = self.GetRootNode();
					if (!root) return none;
					BoxBounds total(true);
					bool any = false;
					std::function<void(const SceneNode::Ptr &)> walk =
						[&](const SceneNode::Ptr &n) {
							if (!n) return;
							if (n->GetComponent<MeshRender>()) {
								BoxBounds wb = n->GetWorldAABB();
								if (!wb.GetInfinite()) {
									total.Encapsulate(wb);
									any = true;
								}
							}
							for (unsigned int i = 0; i < n->GetChildCount(); ++i)
								walk(n->GetChildAt(i));
						};
					walk(root);
					if (!any) return none;
					return std::make_tuple(sol::make_object(lua, total.GetMin()),
										   sol::make_object(lua, total.GetMax()));
				});
			// Scene::Active static property -- same convention as Pipeline.Active.
			lua["Scene"]["GetActive"] = []() -> Scene::Ptr { return Scene::Active; };
			lua["Scene"]["SetActive"] = [](Scene::Ptr p) { Scene::Active = p; };
			// Convenience alias: matches the existing
			// `Scene.SetActive(scene)` style and the FileUtil SaveByExtension
			// Lua entry point that callers use right after mutation.
			lua["Scene"]["SaveActive"] = [](const std::string &path) -> bool {
				return Scene::Active && FileUtil::SaveByExtension(Scene::Active, path);
			};
			// LoadActive replaces the active scene with the contents
			// of `path` (.json / .bin). Returns false on failure.
			lua["Scene"]["LoadActive"] = [](const std::string &path) -> bool {
				if (!Scene::Active) return false;
				return FileUtil::LoadByExtension(Scene::Active, path);
			};

			// Iteration helpers -- wrap the existing EntityManager::ForEach<T>
			// and add a recursive node walk. A non-nil return from the Lua
			// callback short-circuits (matches EntityManager::ForEach's
			// convention). `Scene.ForEachNode` walks pre-order from the root
			// so a parent's name always appears before its children's.
			//
			// Return semantics: the C++ EntityManager::ForEach takes a
			// `std::function<bool(...)>` -- return `false` from the closure
			// to break out of the loop. We map the Lua side as follows:
			//   nil / no return value   -> continue iterating
			//   anything else (true, a  -> short-circuit (stop iterating)
			//     table, etc.)
			// This matches the spec ("A non-nil return from fn SHALL
			// short-circuit the iteration") and the EntityManager convention.
			lua["Scene"]["ForEachMesh"] = sol::overload(
				[](Scene &s, sol::function fn) {
					auto em = s.GetEntityManager();
					if (!em) return;
					em->ForEach<Mesh>([&fn](const Mesh::Ptr &m) -> bool {
						sol::protected_function pf = fn;
						sol::protected_function_result r = pf(m);
						if (!r.valid()) return true;
						// No return value (return_count == 0) or a nil
						// return -> keep iterating. Any other return ->
						// short-circuit (matches the spec's
						// "non-nil return short-circuits" rule).
						if (r.return_count() == 0) return true;
						sol::object obj = r;
						return obj.get_type() == sol::type::lua_nil;
					});
				},
				[](Scene &s, sol::protected_function fn) {
					auto em = s.GetEntityManager();
					if (!em) return;
					em->ForEach<Mesh>([&fn](const Mesh::Ptr &m) -> bool {
						sol::protected_function_result r = fn(m);
						if (!r.valid()) return true;
						if (r.return_count() == 0) return true;
						sol::object obj = r;
						return obj.get_type() == sol::type::lua_nil;
					});
				});
		lua["Scene"]["ForEachMaterial"] = sol::overload(
			[](Scene &s, sol::function fn) {
				auto em = s.GetEntityManager();
				if (!em) return;
				em->ForEach<Material>([&fn](const Material::Ptr &m) -> bool {
					sol::protected_function pf = fn;
					sol::protected_function_result r = pf(m);
					if (!r.valid()) return true;
					if (r.return_count() == 0) return true;
					sol::object obj = r;
					return obj.get_type() == sol::type::lua_nil;
				});
			},
			[](Scene &s, sol::protected_function fn) {
				auto em = s.GetEntityManager();
				if (!em) return;
				em->ForEach<Material>([&fn](const Material::Ptr &m) -> bool {
					sol::protected_function_result r = fn(m);
					if (!r.valid()) return true;
					if (r.return_count() == 0) return true;
					sol::object obj = r;
					return obj.get_type() == sol::type::lua_nil;
				});
			});
		lua["Scene"]["ForEachAnimationClip"] = sol::overload(
			[](Scene &s, sol::function fn) {
				auto em = s.GetEntityManager();
				if (!em) return;
				em->ForEach<AnimationClip>([&fn](const std::shared_ptr<AnimationClip> &c) -> bool {
					sol::protected_function pf = fn;
					sol::protected_function_result r = pf(c);
					if (!r.valid()) return true;
					if (r.return_count() == 0) return true;
					sol::object obj = r;
					return obj.get_type() == sol::type::lua_nil;
				});
			},
			[](Scene &s, sol::protected_function fn) {
				auto em = s.GetEntityManager();
				if (!em) return;
				em->ForEach<AnimationClip>([&fn](const std::shared_ptr<AnimationClip> &c) -> bool {
					sol::protected_function_result r = fn(c);
					if (!r.valid()) return true;
					if (r.return_count() == 0) return true;
					sol::object obj = r;
					return obj.get_type() == sol::type::lua_nil;
				});
			});
			lua["Scene"]["ForEachNode"] = sol::overload(
				[](Scene &s, sol::function fn) {
					std::function<void(const std::shared_ptr<SceneNode>&)> walk =
						[&](const std::shared_ptr<SceneNode> &node) {
							if (!node) return;
							sol::protected_function pf = fn;
							sol::protected_function_result r = pf(node);
							if (r.valid() && r.return_count() > 0) {
								sol::object obj = r;
								// Non-nil return -> short-circuit this subtree.
								if (obj.get_type() != sol::type::lua_nil)
									return;
							}
							for (unsigned int i = 0; i < node->GetChildCount(); ++i)
								walk(node->GetChildAt(i));
						};
					walk(s.GetRootNode());
				},
				[](Scene &s, sol::protected_function fn) {
					std::function<void(const std::shared_ptr<SceneNode>&)> walk =
						[&](const std::shared_ptr<SceneNode> &node) {
							if (!node) return;
							sol::protected_function_result r = fn(node);
							if (r.valid() && r.return_count() > 0) {
								sol::object obj = r;
								// Non-nil return -> short-circuit this subtree.
								if (obj.get_type() != sol::type::lua_nil)
									return;
							}
							for (unsigned int i = 0; i < node->GetChildCount(); ++i)
								walk(node->GetChildAt(i));
						};
					walk(s.GetRootNode());
				});

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

			// --- MeshRender ---------------------------------------------------
			// Inspector and editor scripts need to read mesh / material slots
			// off a MeshRender::Ptr; without these accessors the MeshRender
			// surface is opaque from Lua and duplication / material editing
			// can't be verified end-to-end.
			lua.new_usertype<MeshRender>("MeshRender",
				sol::no_constructor,
				sol::base_classes, sol::bases<Component, Serializable>(),
				"Create",          &MeshRender::Create,
				"GetMesh",         &MeshRender::GetMesh,
				"SetMesh",         &MeshRender::SetMesh,
				"GetMaterialCount",&MeshRender::GetMaterialCount,
				"GetMaterial",     &MeshRender::GetMaterial,
				"SetMaterial",     &MeshRender::SetMaterial,
				"GetRenderable",   &MeshRender::GetRenderable);

			// --- BoxBounds -----------------------------------------------------
			// Minimal binding so the Editor.lua frame-selection handler can
			// read a SceneNode's world-space AABB (center / min / max /
			// validity) without re-implementing the math in C++. Returned
			// by value from SceneNode:GetWorldAABB.
			lua.new_usertype<BoxBounds>("BoxBounds",
				sol::no_constructor,
				"GetCenter",   &BoxBounds::GetCenter,
				"GetMin",      &BoxBounds::GetMin,
				"GetMax",      &BoxBounds::GetMax,
				"GetExtents",  &BoxBounds::GetExtents,
				"GetSize",     &BoxBounds::GetSize,
				"Valid",       &BoxBounds::Valid,
				"GetInfinite", &BoxBounds::GetInfinite);

			// --- Animation enums ----------------------------------------------
			// Exposed as plain Lua tables so scripts can write
			// `state.wrapMode = WrapMode.Loop`. C++ name is AnimWrapMode to
			// avoid colliding with the texture WrapMode; the Lua table is
			// `WrapMode` since the texture WrapMode is not Lua-exposed.
			sol::table wrap_mode_tbl = lua.create_named_table("WrapMode");
			wrap_mode_tbl["Default"]      = static_cast<int>(AnimWrapMode::Default);
			wrap_mode_tbl["Once"]         = static_cast<int>(AnimWrapMode::Once);
			wrap_mode_tbl["Loop"]         = static_cast<int>(AnimWrapMode::Loop);
			wrap_mode_tbl["ClampForever"] = static_cast<int>(AnimWrapMode::ClampForever);
			wrap_mode_tbl["PingPong"]     = static_cast<int>(AnimWrapMode::PingPong);

			sol::table play_mode_tbl = lua.create_named_table("PlayMode");
			play_mode_tbl["StopSameLayer"] = static_cast<int>(PlayMode::StopSameLayer);
			play_mode_tbl["StopAll"]       = static_cast<int>(PlayMode::StopAll);

			// --- AnimationClip ------------------------------------------------
			lua.new_usertype<AnimationClip>("AnimationClip",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"Create", sol::overload(
					[](const std::string &n) { return AnimationClip::Create(n); },
					[](const std::string &n, int tps) { return AnimationClip::Create(n, tps); }),
				"GetName",           &AnimationClip::GetName,
				"SetName",           &AnimationClip::SetName,
				"GetDuration",       &AnimationClip::GetDuration,
				"GetTicksPerSecond", &AnimationClip::GetTicksPerSecond,
				"GetSpeed",          &AnimationClip::GetSpeed,
				"SetSpeed",          &AnimationClip::SetSpeed,
				"GetLoop",           &AnimationClip::GetLoop,
				"SetLoop",           &AnimationClip::SetLoop,
				"GetChannelCount",   &AnimationClip::GetChannelCount,
				"AddChannel", sol::overload(
					static_cast<AnimationClip::ChannelPtr(AnimationClip::*)(const std::string&)>(&AnimationClip::AddChannel),
					static_cast<void(AnimationClip::*)(const AnimationClip::ChannelPtr&)>(&AnimationClip::AddChannel)),
				"RemoveChannel",     &AnimationClip::RemoveChannel,
				"GetChannel",        &AnimationClip::GetChannel,
				"GetChannelAt",      &AnimationClip::GetChannelAt,
				"CalculateDuration", &AnimationClip::CalculateDuration);

			// --- AnimationState ------------------------------------------------
			lua.new_usertype<AnimationState>("AnimationState",
				sol::no_constructor,
				"GetName",      &AnimationState::GetName,
				"GetClip",      &AnimationState::GetClip,
				"GetEnabled",   &AnimationState::IsEnabled,
				"SetEnabled",   &AnimationState::SetEnabled,
				"GetWeight",    &AnimationState::GetWeight,
				"SetWeight",    &AnimationState::SetWeight,
				"GetSpeed",     &AnimationState::GetSpeed,
				"SetSpeed",     &AnimationState::SetSpeed,
				"GetLayer",     &AnimationState::GetLayer,
				"SetLayer",     &AnimationState::SetLayer,
				"GetTime",      &AnimationState::GetTime,
				"SetTime",      &AnimationState::SetTime,
				"GetNormalizedTime", &AnimationState::GetNormalizedTime,
				"SetNormalizedTime", &AnimationState::SetNormalizedTime,
				"GetLength",    &AnimationState::GetLength,
				"GetWrapMode",  &AnimationState::GetWrapMode,
				"SetWrapMode",  [](AnimationState &s, int m) { s.SetWrapMode(static_cast<AnimWrapMode>(m)); });

			// --- Joint ---------------------------------------------------------
			lua.new_usertype<Joint>("Joint",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity>(),
				"GetName",          &Joint::GetName,
				"GetParent",        &Joint::GetParent,
				"GetFirstChild",    &Joint::GetFirstChild,
				"GetSibling",       &Joint::GetSibling,
			"GetLocalMatrix",   &Joint::GetLocalMatrix,
			"GetFinalMatrix",   &Joint::GetFinalMatrix,
			"GetOffsetMatrix",  &Joint::GetOffsetMatrix);

			// --- Animator ------------------------------------------------------
			lua.new_usertype<Animator>("Animator",
				sol::no_constructor,
				sol::base_classes, sol::bases<Component, Serializable>(),
				"Create", sol::overload(
					[]() { return Animator::Create(); },
					[](const std::string &n) { return Animator::Create(n); }),
				"GetName",         &Animator::GetName,
				"SetName",         &Animator::SetName,
				"Play", sol::overload(
					[](Animator &a, const std::string &n) { return a.Play(n); },
					[](Animator &a, const std::string &n, int m) { return a.Play(n, static_cast<PlayMode>(m)); }),
				"Stop", sol::overload(
					[](Animator &a) { a.Stop(); },
					[](Animator &a, const std::string &n) { a.Stop(n); }),
				"Rewind", sol::overload(
					[](Animator &a) { a.Rewind(); },
					[](Animator &a, const std::string &n) { a.Rewind(n); }),
				"CrossFade", sol::overload(
					[](Animator &a, const std::string &n, float f) { a.CrossFade(n, f); },
					[](Animator &a, const std::string &n, float f, int m) { a.CrossFade(n, f, static_cast<PlayMode>(m)); }),
				"IsPlaying",       &Animator::IsPlaying,
				"GetState",        &Animator::GetState,
				"GetStateCount",   &Animator::GetStateCount,
				"GetStateAt",      &Animator::GetStateAt,
				"SetClip",         &Animator::SetClip,
				"RemoveClip",      &Animator::RemoveClip,
				"GetAnimatePhysics",  &Animator::GetAnimatePhysics,
				"SetAnimatePhysics",  &Animator::SetAnimatePhysics,
				"GetClip",            &Animator::GetClip,
				"GetDefaultWrapMode", &Animator::GetDefaultWrapMode,
				"SetDefaultWrapMode", [](Animator &a, int m) { a.SetDefaultWrapMode(static_cast<AnimWrapMode>(m)); });

			// --- ParticleSystem ----------------------------------------------
			lua.new_usertype<ParticleSystem>("ParticleSystem",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable, TypeComparable>(),
				"Create",          &ParticleSystem::Create,
				"GetName",         &ParticleSystem::GetName,
				"SetName",         &ParticleSystem::SetName,
				"GetAliveCount",   &ParticleSystem::GetAliveCount,
				"GetMaxParticles", &ParticleSystem::GetMaxParticles,
				"SetMaxParticles", &ParticleSystem::SetMaxParticles,
				"GetLifetime",     &ParticleSystem::GetLifetime,
				"SetLifetime",     &ParticleSystem::SetLifetime,
				"GetStartSize",    &ParticleSystem::GetStartSize,
				"SetStartSize",    &ParticleSystem::SetStartSize,
				"Emit",            &ParticleSystem::Emit,
				"IsAlive",         &ParticleSystem::IsAlive,
				"Update",          &ParticleSystem::Update,
				"Reset",           &ParticleSystem::Reset);

			// --- ParticleRenderer --------------------------------------------
			lua.new_usertype<ParticleRenderer>("ParticleRenderer",
				sol::no_constructor,
				sol::base_classes, sol::bases<Component, Serializable>(),
				"Create",          &ParticleRenderer::Create,
				"GetName",         &ParticleRenderer::GetName,
				"SetName",         &ParticleRenderer::SetName,
				// Name-based system reference (resolved lazily against
				// the active scene's EntityManager -- GetSystem triggers
				// the resolve).
				"GetSystemName",   &ParticleRenderer::GetSystemName,
				"SetSystemName",   &ParticleRenderer::SetSystemName,
				"GetSystem",       &ParticleRenderer::GetSystem,
				"GetBlendMode",    &ParticleRenderer::GetBlendMode,
				"SetBlendMode",    &ParticleRenderer::SetBlendMode,
				"GetDynamicMesh",  &ParticleRenderer::GetDynamicMesh);

			// --- AnimationUtil (table namespace) ------------------------------
			sol::table anim_util_tbl = lua.create_named_table("AnimationUtil");
			anim_util_tbl["OptimizeAnimClip"] = [](const std::shared_ptr<AnimationClip> &clip, float quality) {
				AnimationUtil::OptimizeAnimClip(clip, quality);
			};

			// --- SceneNode -----------------------------------------------------
			// AddComponent is overloaded per-derived-type so sol2 doesn't
			// have to upcast the lua userdata into `shared_ptr<Component>`
			// itself -- it returns "unrecognized userdata" against the
			// vendored sol2 we ship even though `sol::bases<Component>` is
			// declared on each derived. Same workaround pattern as the
			// `Scene.Create(name, dir, octree)` binding above. RemoveComponent
			// uses the same per-type overload set so Lua can drop a specific
			// component without falling back to the (unbound) std::type_index
			// overload on the C++ side.
			lua.new_usertype<SceneNode>("SceneNode",
				sol::no_constructor,
				"Create", &SceneNode::Create,
				"GetName", &SceneNode::GetName,
				"SetName", &SceneNode::SetName,
				"GetWorldPosition", &SceneNode::GetWorldPosition,
				"GetWorldAABB", &SceneNode::GetWorldAABB,
				"GetLocalPosition", &SceneNode::GetLocalPosition,
				"GetLocalScale",    &SceneNode::GetLocalScale,
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
				"AddComponent", sol::overload(
					[](SceneNode &n, Transform::Ptr c) {
						return n.AddComponent(std::static_pointer_cast<Component>(c));
					},
					[](SceneNode &n, Camera::Ptr c) {
						return n.AddComponent(std::static_pointer_cast<Component>(c));
					},
					[](SceneNode &n, Light::Ptr c) {
						return n.AddComponent(std::static_pointer_cast<Component>(c));
					},
					[](SceneNode &n, MeshRender::Ptr c) {
						return n.AddComponent(std::static_pointer_cast<Component>(c));
					},
					[](SceneNode &n, Animator::Ptr c) {
						return n.AddComponent(std::static_pointer_cast<Component>(c));
					}),
				"RemoveComponent", sol::overload(
					[](SceneNode &n, Transform::Ptr) { return n.RemoveComponent(typeid(Transform)); },
					[](SceneNode &n, Camera::Ptr)    { return n.RemoveComponent(typeid(Camera)); },
					[](SceneNode &n, Light::Ptr)     { return n.RemoveComponent(typeid(Light)); },
					[](SceneNode &n, MeshRender::Ptr){ return n.RemoveComponent(typeid(MeshRender)); },
					[](SceneNode &n, Animator::Ptr)  { return n.RemoveComponent(typeid(Animator)); }),
				"GetComponent", sol::overload(
					// Typed overloads FIRST so a Lua-side
					// `n:GetComponent(ParticleSystem)` resolves to the
					// ParticleSystem::Ptr overload instead of the
					// generic `sol::type` fallback (which returns nil).
					[](SceneNode &n, Transform::Ptr) -> std::shared_ptr<Transform> { return n.GetComponent<Transform>(); },
					[](SceneNode &n, Camera::Ptr)    -> std::shared_ptr<Camera>    { return n.GetComponent<Camera>(); },
					[](SceneNode &n, Light::Ptr)     -> std::shared_ptr<Light>     { return n.GetComponent<Light>(); },
					[](SceneNode &n, MeshRender::Ptr)-> std::shared_ptr<MeshRender>{ return n.GetComponent<MeshRender>(); },
					[](SceneNode &n, Animator::Ptr)  -> std::shared_ptr<Animator>  { return n.GetComponent<Animator>(); },
					[](SceneNode &n, ParticleRenderer::Ptr) -> std::shared_ptr<ParticleRenderer> { return n.GetComponent<ParticleRenderer>(); },
					[](SceneNode &n, sol::type t) -> sol::object {
						// Forward a Lua-side `GetComponent(SceneNode.Light)`-style
						// call (when registered as a table) by name lookup. This
						// is the simplest path for the inspector's generic
						// component loop: pass the registry key and get back the
						// shared_ptr to the actual derived component, or nil.
						// Return type is sol::object so a nil for "not present"
						// round-trips cleanly.
						(void)n;
						(void)t;
						return sol::nil;
					}),
							"GetTransform", [](SceneNode &n) -> std::shared_ptr<Transform> { return n.GetComponent<Transform>(); },
				"GetCamera",    [](SceneNode &n) -> std::shared_ptr<Camera>    { return n.GetComponent<Camera>(); },
				"GetLight",     [](SceneNode &n) -> std::shared_ptr<Light>     { return n.GetComponent<Light>(); },
				"GetMeshRender",[](SceneNode &n) -> std::shared_ptr<MeshRender>{ return n.GetComponent<MeshRender>(); },
				"GetAnimator",  [](SceneNode &n) -> std::shared_ptr<Animator>  { return n.GetComponent<Animator>(); },
				"GetParticleRenderer", [](SceneNode &n) -> std::shared_ptr<ParticleRenderer> { return n.GetComponent<ParticleRenderer>(); },
				"AddChild", &SceneNode::AddChild,
				"RemoveChild", &SceneNode::RemoveChild,
				"RemoveFromParent", &SceneNode::RemoveFromParent,
				"GetChildCount", &SceneNode::GetChildCount,
				"GetChildAt", &SceneNode::GetChildAt,
				"GetParent", &SceneNode::GetParent,
				"Clone", &SceneNode::Clone,
				"CloneTree", &SceneNode::CloneTree,
				// Convenience: pull the Light component (if any) so Lua can
				// inspect / mutate the light without needing template-style
				// GetComponent<T>() bindings. Returns nil when absent.
				"GetLight", [](SceneNode &n) -> std::shared_ptr<Light> {
					return n.GetComponent<Light>();
				});

			// --- Pipeline ------------------------------------------------------
			// `Execute` takes `shared_ptr<SceneManager>`; lua passes the
			// concrete `OcTree::Ptr`. Wrap to upcast in C++ -- same sol2
			// upcast workaround as Scene.Create / SceneNode:AddComponent /
			// Pipeline.SetActive.
			lua.new_usertype<Pipeline>("Pipeline",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"SetCurrentCamera", &Pipeline::SetCurrentCamera,
				// Switches take the PipelineSwitch ordinal (int) -- e.g.
				// CASCADED_SHADOW_MAP=0 ... EDITOR_GRID=6.
				"SetSwitch", [](Pipeline &p, int sw, bool v) { p.SetSwitch(static_cast<PipelineSwitch>(sw), v); },
				"IsSwitchOn", [](Pipeline &p, int sw) -> bool { return p.IsSwitchOn(static_cast<PipelineSwitch>(sw)); },
				"Execute", sol::overload(
					[](Pipeline &p, std::shared_ptr<SceneManager> sm) { p.Execute(sm); },
					[](Pipeline &p, OcTree::Ptr octree) {
						p.Execute(std::static_pointer_cast<SceneManager>(octree));
					}));
			// Static property -- Lua scripts read/write Pipeline.Active. Use
			// explicit getter/setter functions because sol::property on the
			// usertype's class table doesn't reliably round-trip in sol2 v3.5.
			lua["Pipeline"]["GetActive"] = []() -> Pipeline::Ptr { return Pipeline::Active; };
			// Take the concrete `PrelightPipeline::Ptr` (the only Pipeline
			// derivative lua produces) and upcast in C++ -- sol2 fails to
			// unwrap `shared_ptr<PrelightPipeline>` into `shared_ptr<Pipeline>`
			// even with `sol::bases<Pipeline>` declared. Same workaround as
			// Scene.Create / SceneNode.AddComponent above.
			lua["Pipeline"]["SetActive"] = sol::overload(
				[](Pipeline::Ptr p) { Pipeline::Active = p; },
				[](PrelightPipeline::Ptr p) {
					Pipeline::Active = std::static_pointer_cast<Pipeline>(p);
				});

			// --- PrelightPipeline ---------------------------------------------
			lua.new_usertype<PrelightPipeline>("PrelightPipeline",
				sol::no_constructor,
				sol::base_classes, sol::bases<Pipeline, Entity, Serializable>(),
				"Create", &PrelightPipeline::Create);

			// Pipeline HDR / chain bindings. The editor toggles HDR
			// via Scene::RenderSettings; these Lua hooks let a
			// startup script do the same. ApplyRenderSettings
			// rebuilds the active chain from a RenderSettings
			// (resolving effect names against the registry).
			lua["Pipeline"]["SetHDRMode"] = [](Pipeline::Ptr p, bool value) {
				if (p) p->SetHDRMode(value);
			};
			lua["Pipeline"]["IsHDRMode"] = [](Pipeline::Ptr p) -> bool {
				return p && p->IsHDRMode();
			};
			lua["Pipeline"]["ApplyRenderSettings"] = [](Pipeline::Ptr p, RenderSettings &rs) {
				if (p) p->ApplyRenderSettings(rs);
			};

			// --- PostProcessEffect (usertype for chain editor / scripts) -----
			lua.new_usertype<PostProcessEffect>("PostProcessEffect",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"GetName", &PostProcessEffect::GetName,
				"GetShaderPath", &PostProcessEffect::GetShaderPath);

			// --- RenderSettings -----------------------------------------------
			// Per-scene render config: pipeline path, HDR / CSM flags,
			// ordered postprocess chain. The editor's Engine settings
			// panel + the postprocess chain editor drive these; the
			// scene's `renderSettings` block serializes them.
			lua.new_usertype<RenderSettings>("RenderSettings",
				sol::no_constructor,
				"GetPipelinePath", &RenderSettings::GetPipelinePath,
				"SetPipelinePath", &RenderSettings::SetPipelinePath,
				"IsHDR", &RenderSettings::IsHDR,
				"SetHDR", &RenderSettings::SetHDR,
				"IsCascadedShadowMap", &RenderSettings::IsCascadedShadowMap,
				"SetCascadedShadowMap", &RenderSettings::SetCascadedShadowMap,
				"GetChain", [&lua](RenderSettings &self) {
					// Hand back a Lua table of { effectName, enabled }
					// entries. sol2's binding for std::vector<struct>
					// doesn't expose field access cleanly, so we
					// marshal manually.
					const auto &c = self.GetChain();
					sol::table out = lua.create_table();
					for (size_t i = 0; i < c.size(); ++i) {
						sol::table e = lua.create_table();
						e["effectName"] = c[i].effectName;
						e["enabled"] = c[i].enabled;
						out[static_cast<int>(i + 1)] = e;
					}
					return out;
				},
				"ClearChain", &RenderSettings::ClearChain,
				"AddEffect", [](RenderSettings &self, const std::string &name, sol::optional<bool> enabled) {
					self.AddEffect(name, enabled.value_or(true));
				},
				"RemoveEffect", &RenderSettings::RemoveEffect,
				"MoveEffect", &RenderSettings::MoveEffect,
				"CopyChainFrom", &RenderSettings::CopyChainFrom,
				"SetEffectEnabled", &RenderSettings::SetEffectEnabled);

			// --- PostProcessRegistry -----------------------------------------
			// Exposed as a Lua table so scripts can call
			//   PostProcess.LoadFromDirectory("Resource/PostProcess")
			// once at startup. The C++ side owns the actual map;
			// Lua only calls into it.
			// --- Launcher flags ------------------------------------------------
			// Read-only view of the launcher's --flags for scripts:
			//   Launcher.GetFlag("auto_confirm") / ("auto_focus")
			sol::table launcher_tbl = lua.create_named_table("Launcher");
			launcher_tbl["GetFlag"] = [&lua](const std::string &name) -> sol::object {
				if (!s_launcher_options) return sol::nil;
				if (name == "auto_confirm") return sol::make_object(lua, s_launcher_options->auto_confirm);
				if (name == "auto_focus") return sol::make_object(lua, s_launcher_options->auto_focus);
				return sol::nil;
			};

			sol::table pp_tbl = lua.create_named_table("PostProcess");
			pp_tbl["LoadFromDirectory"] = [](const std::string &dir) {
				return fury::PostProcessRegistry::LoadFromDirectory(dir);
			};
			pp_tbl["Clear"] = []() {
				fury::PostProcessRegistry::Clear();
			};
			pp_tbl["GetAll"] = [&lua]() {
				std::vector<fury::PostProcessEffect::Ptr> all =
					fury::PostProcessRegistry::GetAll();
				// Marshal to a Lua table; capture by ref so the
				// closure can reach the lua_State.
				sol::table out = lua.create_table();
				int i = 1;
				for (auto &e : all) {
					out[i++] = e;
				}
				return out;
			};

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
			// Pick the underlying serializer by lowercased path
			// extension (.json -> SaveFile, .bin -> SaveCompressedFile).
			// The single dispatch point used by the editor and the CLI.
			fu_tbl["SaveByExtension"] = [](const std::shared_ptr<Scene> &s, const std::string &p) {
				return FileUtil::SaveByExtension(s, p);
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
			// Maps the optional `normal_mode` arg ("smooth" | "flat",
			// default smooth) onto GltfImporter::Options.
			auto make_import_opts = [](const sol::optional<std::string> &normal_mode) {
				GltfImporter::Options opts;
				if (normal_mode && *normal_mode == "flat")
					opts.normal_gen = GltfImporter::Options::NormalGen::Flat;
				else if (normal_mode && *normal_mode != "smooth")
					FURYW << "Importer: unknown normal_mode '" << *normal_mode << "' (expected smooth|flat); using smooth";
				// Mesh weld/dedup: on by default, opt-out via the editor's
				// "Optimize Mesh" import flag. In the headless/CLI build the
				// Editor stub returns the default (true), so CLI imports
				// always optimize.
				opts.optimize_mesh = Editor::GetImportFlag("optimize_mesh", true);
				// HDR-aware material translation (opsx camera-postprocess-hdr
				// task 5.2): when the active pipeline is in HDR mode, the
				// importer maps glTF metallic/roughness/normal/occlusion onto
				// the material's PBR slots instead of discarding them.
				opts.hdr_target = Pipeline::Active && Pipeline::Active->IsHDRMode();
				return opts;
			};
			// Import a .gltf or .glb into a fresh Scene::Ptr. Returns nil on
			// error (logged via FURYE before return). The Scene's working_dir
			// is the input file's directory so relative texture URIs resolve.
			// Optional 2nd arg: normal generation mode "smooth" (default) or "flat".
			importer_tbl["LoadGltf"] = [make_import_opts](const std::string &path, sol::optional<std::string> normal_mode) -> std::shared_ptr<Scene> {
				try
				{
					auto slash = path.find_last_of("/\\");
					std::string working = (slash == std::string::npos) ? std::string{} : path.substr(0, slash + 1);
					return GltfImporter::Import(path, path, working, make_import_opts(normal_mode));
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
			// Optional 2nd arg: normal generation mode "smooth" (default) or "flat".
			importer_tbl["LoadFbx"] = [make_import_opts](const std::string &path, sol::optional<std::string> normal_mode) -> std::shared_ptr<Scene> {
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
					auto scene = GltfImporter::Import(res.output_path, path, working, make_import_opts(normal_mode));
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
			// Optional 2nd arg: normal generation mode "smooth" (default) or
			// "flat" (gltf/fbx only; native scenes ignore it).
			importer_tbl["LoadScene"] = [&lua](const std::string &path, sol::optional<std::string> normal_mode) -> std::shared_ptr<Scene> {
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
					return lua["Importer"]["LoadGltf"](path, normal_mode);
				if (ext == ".fbx")
					return lua["Importer"]["LoadFbx"](path, normal_mode);
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
					// Drop from the source scene manager FIRST (imported
					// scenes self-register now): otherwise the discarded
					// source tree's Clear() would wipe the back-pointer
					// after the node is re-registered into the target tree.
					child->RemoveFromOcTree(true);
					source_root->RemoveChild(child);
					target_root->AddChild(child);
					++merged;
				}
				// Transfer entities. EntityManager::Add dedupes by hash; we
				// just trust that and forward.
				// Textures must be transferred explicitly: Materials only
				// hold shared_ptrs to their Textures, so without this
				// transfer the imported textures are invisible to the
				// active scene's EM, and the save path's memory-backed
				// texture extraction (which iterates the EM) skips them.
				auto target_em = target->GetEntityManager();
				auto source_em = source->GetEntityManager();
				source_em->ForEach<Texture>([&](const std::shared_ptr<Texture> &t) -> bool {
					target_em->Add(t); return true;
				});
				source_em->ForEach<Material>([&](const std::shared_ptr<Material> &m) -> bool {
					target_em->Add(m); return true;
				});
				source_em->ForEach<Mesh>([&](const std::shared_ptr<Mesh> &m) -> bool {
					target_em->Add(m); return true;
				});
				source_em->ForEach<AnimationClip>([&](const std::shared_ptr<AnimationClip> &c) -> bool {
					target_em->Add(c); return true;
				});
				// ParticleSystem is a top-level asset like AnimationClip --
				// skipping it here would strand the systems in the discarded
				// source scene (content browser misses them, renderers can't
				// resolve, and a re-save drops particleSystems[]).
				source_em->ForEach<ParticleSystem>([&](const std::shared_ptr<ParticleSystem> &p) -> bool {
					target_em->Add(p); return true;
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
				if (auto v = tbl["on_save"]; v.valid() && v.get_type() == sol::type::function)
				{
					sol::protected_function pf = v;
					io.on_save = [pf](const std::string& p) {
						sol::protected_function_result r = pf(p);
						if (!r.valid()) { sol::error e = r; FURYE << "Editor on_save error: " << e.what(); }
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

			editor_tbl["SetFrameSelectionHandler"] = [](sol::object obj) {
				if (!obj.valid() || obj.get_type() != sol::type::function)
				{
					Editor::SetFrameSelectionHandler(nullptr);
					return;
				}
				sol::protected_function pf = obj.as<sol::protected_function>();
				Editor::SetFrameSelectionHandler([pf](SceneNode* node) {
					sol::protected_function_result r = pf(node);
					if (!r.valid()) { sol::error e = r; FURYE << "Editor frame handler error: " << e.what(); }
				});
			};

			// Editor.OpenDialog({filter=..., default_path=..., multi=...})
			//   -> string | table<string> | nil
			// Editor.SaveDialog({filter=..., default_path=..., default_name=...})
			//   -> string | nil
			//
			// `filter` is a single nfd filter spec string ("png,jpg,jpeg"
			// comma-separated extensions without leading dots, or "All"
			// for no filter). It is forwarded as one filter item whose
			// display name is "Files". On cancel or nfd error, returns
			// nil; nfd errors are logged via FURYE (no throw).
			editor_tbl["OpenDialog"] = [](sol::table opts) -> sol::object {
				sol::state_view lua = opts.lua_state();
				const std::string filter      = opts.get_or<std::string>("filter", "All");
				const std::string default_path = opts.get_or<std::string>("default_path", "");
				// `get_or<bool>` is ambiguous against sol2's two overloads
				// (T=bool/D=bool); do the lookup manually like the
				// SetCameraSettings binding.
				sol::object multi_obj = opts["multi"];
				const bool multi = (multi_obj.valid() && multi_obj.is<bool>())
								   ? multi_obj.as<bool>() : false;

				const std::string filter_name = "Files";
				nfdu8filteritem_t filter_item;
				filter_item.name = filter_name.c_str();
				filter_item.spec = filter.c_str();
				const nfdu8char_t* default_path_c = default_path.empty() ? nullptr : default_path.c_str();

				if (multi)
				{
					const nfdpathset_t* path_set = nullptr;
					nfdresult_t r = NFD_OpenDialogMultipleU8(&path_set, &filter_item, 1, default_path_c);
					if (r == NFD_CANCEL) return sol::nil;
					if (r != NFD_OKAY)
					{
						FURYE << "NFD_OpenDialogMultipleU8 error: "
							  << (NFD_GetError() ? NFD_GetError() : "(unknown)");
						return sol::nil;
					}
					sol::table out = lua.create_table();
					nfdpathsetsize_t count = 0;
					if (NFD_PathSet_GetCount(path_set, &count) == NFD_OKAY)
					{
						for (nfdpathsetsize_t i = 0; i < count; ++i)
						{
							nfdu8char_t* path = nullptr;
							if (NFD_PathSet_GetPathU8(path_set, i, &path) == NFD_OKAY && path)
							{
								out[i + 1] = std::string(path); // 1-indexed
								NFD_PathSet_FreePathU8(path);
							}
						}
					}
					NFD_PathSet_Free(path_set);
					return out;
				}
				else
				{
					nfdu8char_t* out_path = nullptr;
					nfdresult_t r = NFD_OpenDialogU8(&out_path, &filter_item, 1, default_path_c);
					if (r == NFD_CANCEL) return sol::nil;
					if (r != NFD_OKAY)
					{
						FURYE << "NFD_OpenDialogU8 error: "
							  << (NFD_GetError() ? NFD_GetError() : "(unknown)");
						return sol::nil;
					}
					std::string path(out_path);
					NFD_FreePathU8(out_path);
					return sol::make_object(lua, path);
				}
			};

			editor_tbl["SaveDialog"] = [](sol::table opts) -> sol::object {
				sol::state_view lua = opts.lua_state();
				const std::string filter        = opts.get_or<std::string>("filter", "All");
				const std::string default_path = opts.get_or<std::string>("default_path", "");
				const std::string default_name = opts.get_or<std::string>("default_name", "");

				const std::string filter_name = "Files";
				nfdu8filteritem_t filter_item;
				filter_item.name = filter_name.c_str();
				filter_item.spec = filter.c_str();
				const nfdu8char_t* default_path_c = default_path.empty() ? nullptr : default_path.c_str();
				const nfdu8char_t* default_name_c = default_name.empty() ? nullptr : default_name.c_str();

				nfdu8char_t* out_path = nullptr;
				nfdresult_t r = NFD_SaveDialogU8(&out_path, &filter_item, 1, default_path_c, default_name_c);
				if (r == NFD_CANCEL) return sol::nil;
				if (r != NFD_OKAY)
				{
					FURYE << "NFD_SaveDialogU8 error: "
						  << (NFD_GetError() ? NFD_GetError() : "(unknown)");
					return sol::nil;
				}
				std::string path(out_path);
				NFD_FreePathU8(out_path);
				return sol::make_object(lua, path);
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
		editor_tbl["SetSelectedSceneNode"] = [](sol::object node_obj) {
			if (!node_obj.valid() || node_obj.get_type() == sol::type::nil
				|| node_obj.get_type() == sol::type::lua_nil)
				Editor::SetSelectedSceneNode(nullptr);
			else
				Editor::SetSelectedSceneNode(node_obj.as<SceneNode*>());
		};
		editor_tbl["SetWindowVisible"]    = [](const std::string &name, bool v) { Editor::SetWindowVisible(name.c_str(), v); };
		editor_tbl["GetWindowVisible"]    = [](const std::string& name) -> bool { return Editor::GetWindowVisible(name.c_str()); };
		editor_tbl["IsPickInFlight"]      = []() -> bool { return Editor::IsPickInFlight(); };
		editor_tbl["IsViewportHovered"]   = []() -> bool { return Editor::IsViewportHovered(); };
		editor_tbl["IsViewportContentHovered"] = []() -> bool { return Editor::IsViewportContentHovered(); };
			editor_tbl["SetImportFlag"]       = [](const std::string& name, bool v) { Editor::SetImportFlag(name.c_str(), v); };
			editor_tbl["GetImportFlag"]       = sol::overload(
				[](const std::string& name) -> bool { return Editor::GetImportFlag(name.c_str(), false); },
				[](const std::string& name, bool d) -> bool { return Editor::GetImportFlag(name.c_str(), d); });
			// Queued Yes/No modal (EditorConfirmDialog.h) -- the Lua
			// callback fires exactly once with true (Yes) / false (No/Esc).
			editor_tbl["RequestConfirmDialog"] = [](const std::string& title, const std::string& message,
													sol::protected_function cb) {
				auto invoke = [cb](bool yes) mutable {
					if (!cb.valid()) return;
					sol::protected_function_result r = cb(yes);
					if (!r.valid()) { sol::error e = r; FURYE << "confirm dialog callback error: " << e.what(); }
				};
				// --auto-confirm: fire the default (Yes) selection
				// immediately so headless runs never block on a modal.
				if (s_launcher_options && s_launcher_options->auto_confirm)
				{
					invoke(true);
					return;
				}
				Editor::RequestConfirmDialog(title, message, std::move(invoke));
			};
			editor_tbl["SetCurrentScene"]     = [](const std::string& path, bool is_native) {
				Editor::SetCurrentScene(path, is_native);
			};
			editor_tbl["ClearCurrentScene"]   = []() { Editor::ClearCurrentScene(); };
			editor_tbl["GetCurrentScenePath"] = []() -> std::string { return Editor::GetCurrentScenePath(); };
			// Scene dirty tracking. Marked by every inspector mutation
			// and cleared by the save path on success.
			editor_tbl["MarkSceneDirty"]      = []() { Editor::MarkSceneDirty(); };
			editor_tbl["ClearSceneDirty"]     = []() { Editor::ClearSceneDirty(); };

			// Gizmo controls -- optional power-user surface; the editor
			// works without scripts touching these. Unknown name strings
			// are silently ignored at the C++ layer.
			editor_tbl["SetGizmoMode"]        = [](const std::string& name) { Editor::SetGizmoMode(name.c_str()); };
			editor_tbl["SetGizmoSpace"]       = [](const std::string& name) { Editor::SetGizmoSpace(name.c_str()); };
			editor_tbl["SetSnapEnabled"]      = [](bool v) { Editor::SetSnapEnabled(v); };
			editor_tbl["GetGizmoMode"]        = []() -> std::string { return Editor::GetGizmoMode(); };
			editor_tbl["GetGizmoSpace"]       = []() -> std::string { return Editor::GetGizmoSpace(); };
			editor_tbl["GetSnapEnabled"]      = []() -> bool { return Editor::GetSnapEnabled(); };

			// Automation hooks: open per-asset editor windows by asset
			// name -- verification scripts use these to screenshot the
			// editors headlessly (no window-picker plumbing needed).
			editor_tbl["OpenParticleEditor"]  = [](const std::string& name) {
				if (!Scene::Active) return;
				if (auto em = Scene::Active->GetEntityManager())
					if (auto ps = em->Get<ParticleSystem>(name))
						Editor::OpenParticleEditor(ps);
			};
			editor_tbl["OpenMeshEditor"]      = [](const std::string& name) {
				if (!Scene::Active) return;
				if (auto em = Scene::Active->GetEntityManager())
					if (auto mesh = em->Get<Mesh>(name))
						Editor::OpenMeshEditor(mesh);
			};
#else
			// No-op stubs so user scripts that reference Editor.* compose
			// with both build modes. Each accepts and discards arguments.
			editor_tbl["SetSceneIO"]            = [](sol::object) {};
			editor_tbl["SetSceneTreeProvider"]  = [](sol::object) {};
			editor_tbl["SetCommandHandler"]     = [](sol::object) {};
			editor_tbl["SetFrameSelectionHandler"] = [](sol::object) {};
			editor_tbl["OpenDialog"]            = [](sol::object) -> sol::object { return sol::nil; };
			editor_tbl["SaveDialog"]            = [](sol::object) -> sol::object { return sol::nil; };
			editor_tbl["SetCameraSettings"]     = [](sol::object) {};
			editor_tbl["Log"]                   = [](sol::object, sol::object) {};
		editor_tbl["GetSelectedSceneNode"]  = []() -> sol::object { return sol::nil; };
		editor_tbl["SetSelectedSceneNode"]  = [](sol::object) {};
		editor_tbl["SetWindowVisible"]      = [](sol::object, sol::object) {};
		editor_tbl["GetWindowVisible"]      = [](sol::object) -> bool { return false; };
		editor_tbl["IsPickInFlight"]        = []() -> bool { return false; };
		editor_tbl["IsViewportHovered"]     = []() -> bool { return false; };
		editor_tbl["IsViewportContentHovered"] = []() -> bool { return false; };
			editor_tbl["SetImportFlag"]         = [](sol::object, sol::object) {};
			editor_tbl["GetImportFlag"]         = sol::overload(
				[](sol::object) -> bool { return false; },
				[](sol::object, bool d) -> bool { return d; });
			editor_tbl["RequestConfirmDialog"]  = [](sol::object, sol::object, sol::object) {};
			editor_tbl["SetCurrentScene"]       = [](sol::object, sol::object) {};
			editor_tbl["ClearCurrentScene"]     = []() {};
			editor_tbl["GetCurrentScenePath"]   = []() -> std::string { return {}; };
			editor_tbl["MarkSceneDirty"]        = [](sol::object) {};
			editor_tbl["ClearSceneDirty"]       = []() {};

			// Gizmo no-ops for non-editor builds: same surface as the
			// editor path so user scripts compose without #ifdefs.
			editor_tbl["SetGizmoMode"]          = [](sol::object) {};
			editor_tbl["SetGizmoSpace"]         = [](sol::object) {};
			editor_tbl["SetSnapEnabled"]        = [](bool) {};
			editor_tbl["GetGizmoMode"]          = []() -> std::string { return "translate"; };
			editor_tbl["GetGizmoSpace"]         = []() -> std::string { return "world"; };
			editor_tbl["GetSnapEnabled"]        = []() -> bool { return false; };
			editor_tbl["OpenParticleEditor"]    = [](sol::object) {};
			editor_tbl["OpenMeshEditor"]        = [](sol::object) {};
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
			// The launcher injects the active sf::Window into lua["__window"];
			// we read it back and dispatch to Engine::Run.
			//
			// Lua signature: Engine.run(callbacks [, options])
			//   callbacks: { on_init, on_update, on_fixed_update, on_shutdown }
			//   options:   { max_fps, gui_scale, gui_font_scale, dpi_aware_override }
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
					// max_fps: accept number, or boolean false -> 0.
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
					opts.dpi_aware_override = o.get_or("dpi_aware_override", opts.dpi_aware_override);
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

			// --- Mesh --------------------------------------------------------
			// Buffer accessors round-trip through flat Lua tables
			// (`{x0,y0,z0,x1,y1,z1,...}`) per design decision D6. The
			// `sol::as_container` route was considered but it complicates the
			// agent-facing API with ownership / lifetime semantics that an
			// LLM is unlikely to write correctly on the first try. Plain
			// tables round-trip cleanly and are easy to reason about.
			lua.new_usertype<Mesh>("Mesh",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"GetName", &Mesh::GetName,
				"SetName", &Mesh::SetName,
				"GetAABB", &Mesh::GetAABB,
				"IsSkinnedMesh", &Mesh::IsSkinnedMesh,
				"GetCastShadows", &Mesh::GetCastShadows,
				"SetCastShadows", &Mesh::SetCastShadows,
				// Submesh accessors.
				"GetSubmeshCount", &Mesh::GetSubMeshCount,
				// LOD chain accessors -- direct mirror of Mesh.h:227-236.
				"GetLodCount", &Mesh::GetLodCount,
				"GetLodMesh",  &Mesh::GetLodMesh,
				"ClearLodChain", &Mesh::ClearLodChain);

			// Flat-table round-trip helpers. Done outside the usertype literal
			// because the getters need access to the `lua` state_view to build
			// the result table. The setters accept the same flat shape.
			lua["Mesh"]["GetPositions"] = [](const Mesh &m, sol::this_state s) -> sol::table {
				sol::state_view lua(s);
				sol::table t = lua.create_table();
				const auto &data = m.Positions.Data;
				for (size_t i = 0; i < data.size(); ++i)
					t[i + 1] = data[i];
				return t;
			};
			lua["Mesh"]["SetPositions"] = [](Mesh &m, sol::table t) {
				m.Positions.Data.clear();
				m.Positions.Data.reserve(t.size());
				for (size_t i = 1; i <= t.size(); ++i)
					m.Positions.Data.push_back(t.get<float>(i));
			};
			lua["Mesh"]["GetNormals"] = [](const Mesh &m, sol::this_state s) -> sol::table {
				sol::state_view lua(s);
				sol::table t = lua.create_table();
				const auto &data = m.Normals.Data;
				for (size_t i = 0; i < data.size(); ++i)
					t[i + 1] = data[i];
				return t;
			};
			lua["Mesh"]["SetNormals"] = [](Mesh &m, sol::table t) {
				m.Normals.Data.clear();
				m.Normals.Data.reserve(t.size());
				for (size_t i = 1; i <= t.size(); ++i)
					m.Normals.Data.push_back(t.get<float>(i));
			};
			lua["Mesh"]["GetUVs"] = [](const Mesh &m, sol::this_state s) -> sol::table {
				sol::state_view lua(s);
				sol::table t = lua.create_table();
				const auto &data = m.UVs.Data;
				for (size_t i = 0; i < data.size(); ++i)
					t[i + 1] = data[i];
				return t;
			};
			lua["Mesh"]["SetUVs"] = [](Mesh &m, sol::table t) {
				m.UVs.Data.clear();
				m.UVs.Data.reserve(t.size());
				for (size_t i = 1; i <= t.size(); ++i)
					m.UVs.Data.push_back(t.get<float>(i));
			};
			lua["Mesh"]["GetTangents"] = [](const Mesh &m, sol::this_state s) -> sol::table {
				sol::state_view lua(s);
				sol::table t = lua.create_table();
				const auto &data = m.Tangents.Data;
				for (size_t i = 0; i < data.size(); ++i)
					t[i + 1] = data[i];
				return t;
			};
			lua["Mesh"]["SetTangents"] = [](Mesh &m, sol::table t) {
				m.Tangents.Data.clear();
				m.Tangents.Data.reserve(t.size());
				for (size_t i = 1; i <= t.size(); ++i)
					m.Tangents.Data.push_back(t.get<float>(i));
			};
			lua["Mesh"]["GetBoneIds"] = [](const Mesh &m, sol::this_state s) -> sol::table {
				sol::state_view lua(s);
				sol::table t = lua.create_table();
				const auto &data = m.IDs.Data;
				for (size_t i = 0; i < data.size(); ++i)
					t[i + 1] = data[i];
				return t;
			};
			lua["Mesh"]["SetBoneIds"] = [](Mesh &m, sol::table t) {
				m.IDs.Data.clear();
				m.IDs.Data.reserve(t.size());
				for (size_t i = 1; i <= t.size(); ++i)
					m.IDs.Data.push_back(t.get<unsigned int>(i));
			};
			lua["Mesh"]["GetBoneWeights"] = [](const Mesh &m, sol::this_state s) -> sol::table {
				sol::state_view lua(s);
				sol::table t = lua.create_table();
				const auto &data = m.Weights.Data;
				for (size_t i = 0; i < data.size(); ++i)
					t[i + 1] = data[i];
				return t;
			};
			lua["Mesh"]["SetBoneWeights"] = [](Mesh &m, sol::table t) {
				m.Weights.Data.clear();
				m.Weights.Data.reserve(t.size());
				for (size_t i = 1; i <= t.size(); ++i)
					m.Weights.Data.push_back(t.get<float>(i));
			};
			lua["Mesh"]["GetIndices"] = [](const Mesh &m, sol::this_state s) -> sol::table {
				sol::state_view lua(s);
				sol::table t = lua.create_table();
				const auto &data = m.Indices.Data;
				for (size_t i = 0; i < data.size(); ++i)
					t[i + 1] = data[i];
				return t;
			};
			lua["Mesh"]["SetIndices"] = [](Mesh &m, sol::table t) {
				m.Indices.Data.clear();
				m.Indices.Data.reserve(t.size());
				for (size_t i = 1; i <= t.size(); ++i)
					m.Indices.Data.push_back(t.get<unsigned int>(i));
			};
			lua["Mesh"]["GetSubmeshIndices"] = [](Mesh &m, unsigned int i, sol::this_state s) -> sol::table {
				sol::state_view lua(s);
				sol::table t = lua.create_table();
				auto sub = m.GetSubMeshAt(i);
				if (!sub) return t;
				const auto &data = sub->Indices.Data;
				for (size_t k = 0; k < data.size(); ++k)
					t[k + 1] = data[k];
				return t;
			};
			// LOD chain setter: accepts parallel arrays (meshes, thresholds).
			// Mirrors Mesh::SetLodMeshes' validation: sizes must match and
			// thresholds must be non-increasing -- the C++ side enforces and
			// logs FURYE on mismatch, so we forward and trust.
			lua["Mesh"]["SetLodMeshes"] = [](Mesh &m, sol::table meshes_tbl, sol::table thresholds_tbl) {
				std::vector<std::shared_ptr<Mesh>> meshes;
				std::vector<float> thresholds;
				meshes.reserve(meshes_tbl.size());
				for (size_t i = 1; i <= meshes_tbl.size(); ++i)
					meshes.push_back(meshes_tbl.get<std::shared_ptr<Mesh>>(i));
				thresholds.reserve(thresholds_tbl.size());
				for (size_t i = 1; i <= thresholds_tbl.size(); ++i)
					thresholds.push_back(thresholds_tbl.get<float>(i));
				m.SetLodMeshes(meshes, thresholds);
			};

			// --- Material ----------------------------------------------------
			// GetUniform returns number/table/nil based on the underlying
			// uniform type. SetUniform infers the type from the Lua value's
			// shape: number -> Uniform1f, integer-valued number -> Uniform1ui,
			// 3-element table -> Uniform3f, 4-element table -> Uniform4f.
			lua.new_usertype<Material>("Material",
				// Material.Create(name) -- for scripts that build test/demo
				// content programmatically (scene imports own materials
				// otherwise).
				"Create", &Material::Create,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"GetName", &Material::GetName,
				"SetName", &Material::SetName,
				"IsOpaque", &Material::GetOpaque,
				"SetOpaque", &Material::SetOpaque,
				"GetTextureCount", &Material::GetTextureCount,
				"GetTexture", [](const Material &mat, const std::string &key, sol::this_state s) -> sol::object {
					auto tex = mat.GetTexture(key);
					if (!tex) return sol::nil;
					return sol::make_object(sol::state_view(s), tex->GetFilePath());
				},
				"SetTexture", [](Material &mat, const std::string &key, const std::string &path) {
					auto tex = Texture::Create(key);
					tex->SetFilePathAndSRGB(path, true);
					mat.SetTexture(key, tex);
				},
				"GetUniform", [](Material &mat, const std::string &key, sol::this_state s) -> sol::object {
					auto u = mat.GetUniform(key);
					if (!u) return sol::nil;
					sol::state_view lua(s);
					// Uniform<int,1> -> integer; Uniform<unsigned int,1> -> uint;
					// Uniform<float,N> -> number for N==1, table for N>1.
					if (auto p = std::dynamic_pointer_cast<Uniform<int, 1>>(u))
						return sol::make_object(lua, p->GetDataAt(0));
					if (auto p = std::dynamic_pointer_cast<Uniform<unsigned int, 1>>(u))
						return sol::make_object(lua, p->GetDataAt(0));
					if (auto p = std::dynamic_pointer_cast<Uniform<float, 1>>(u))
						return sol::make_object(lua, p->GetDataAt(0));
					if (auto p = std::dynamic_pointer_cast<Uniform<float, 2>>(u)) {
						sol::table t = lua.create_table();
						t[1] = p->GetDataAt(0);
						t[2] = p->GetDataAt(1);
						return sol::object(t);
					}
					if (auto p = std::dynamic_pointer_cast<Uniform<float, 3>>(u)) {
						sol::table t = lua.create_table();
						t[1] = p->GetDataAt(0);
						t[2] = p->GetDataAt(1);
						t[3] = p->GetDataAt(2);
						return sol::object(t);
					}
					if (auto p = std::dynamic_pointer_cast<Uniform<float, 4>>(u)) {
						sol::table t = lua.create_table();
						t[1] = p->GetDataAt(0);
						t[2] = p->GetDataAt(1);
						t[3] = p->GetDataAt(2);
						t[4] = p->GetDataAt(3);
						return sol::object(t);
					}
					return sol::nil;
				},
				"SetUniform", [](Material &mat, const std::string &key, sol::object value) {
					if (value.is<float>() || value.is<double>())
					{
						auto u = Uniform<float, 1>::Create({ static_cast<float>(value.as<double>()) });
						mat.SetUniform(key, u);
						return;
					}
					if (value.is<int>() || value.is<unsigned int>())
					{
						auto u = Uniform<unsigned int, 1>::Create({ static_cast<unsigned int>(value.as<int>()) });
						mat.SetUniform(key, u);
						return;
					}
					if (value.is<sol::table>())
					{
						sol::table t = value;
						size_t n = t.size();
						if (n == 3)
						{
							auto u = Uniform<float, 3>::Create({
								t.get<float>(1), t.get<float>(2), t.get<float>(3) });
							mat.SetUniform(key, u);
						}
						else if (n == 4)
						{
							auto u = Uniform<float, 4>::Create({
								t.get<float>(1), t.get<float>(2),
								t.get<float>(3), t.get<float>(4) });
							mat.SetUniform(key, u);
						}
						else if (n == 2)
						{
							auto u = Uniform<float, 2>::Create({
								t.get<float>(1), t.get<float>(2) });
							mat.SetUniform(key, u);
						}
					}
				});

			// --- MeshUtil namespace table ------------------------------------
			// Primitive factories + mesh-processing utilities. Mirrors
			// MeshUtil.h:46-67. The "CreateCube / CreateQuad" calls below
			// produce unit primitives (centered at origin, +/-0.5 extents)
			// via the named-variants CreateCube(name, min, max). The
			// variants with min/max are exposed as CreateBox / CreateQuadBox
			// so the unit-vs-named split is unambiguous to Lua callers.
			sol::table mesh_util_tbl = lua.create_named_table("MeshUtil");
			mesh_util_tbl["CreateCube"] = [](sol::this_state s) -> std::shared_ptr<Mesh> {
				(void)s;
				return MeshUtil::CreateCube("cube",
					Vector4(-0.5f, -0.5f, -0.5f, 1.0f),
					Vector4( 0.5f,  0.5f,  0.5f, 1.0f));
			};
			mesh_util_tbl["CreateQuad"] = [](sol::this_state s) -> std::shared_ptr<Mesh> {
				(void)s;
				return MeshUtil::CreateQuad("quad",
					Vector4(-0.5f, -0.5f, 0.0f, 1.0f),
					Vector4( 0.5f,  0.5f, 0.0f, 1.0f));
			};
			mesh_util_tbl["CreateSphere"] = [](int segments) -> std::shared_ptr<Mesh> {
				int seg = segments > 2 ? segments : 16;
				return MeshUtil::CreateSphere("sphere", 0.5f, seg, seg);
			};
			mesh_util_tbl["CreateIcoSphere"] = [](int subdivisions) -> std::shared_ptr<Mesh> {
				int sub = subdivisions >= 0 ? subdivisions : 1;
				return MeshUtil::CreateIcoSphere("icosphere", 0.5f, sub);
			};
			mesh_util_tbl["CreateCylinder"] = [](int segments) -> std::shared_ptr<Mesh> {
				int seg = segments > 2 ? segments : 16;
				return MeshUtil::CreateCylinder("cylinder", 0.5f, 0.5f, 1.0f, seg, 1);
			};
			// Matrix is a flat 16-element Lua table, row-major (matches the
			// engine's Matrix4 layout: m00..m33).
			mesh_util_tbl["TransformMesh"] = [](const std::shared_ptr<Mesh> &mesh, sol::table m) {
				Matrix4 mat;
				for (size_t i = 0; i < 16; ++i)
					mat.Raw[i] = m.get<float>(i + 1);
				MeshUtil::TransformMesh(mesh, mat);
			};
			mesh_util_tbl["OptimizeMesh"] = [](const std::shared_ptr<Mesh> &mesh) {
				MeshUtil::OptimizeMesh(mesh);
			};
			mesh_util_tbl["CalculateNormal"] = [](const std::shared_ptr<Mesh> &mesh) {
				MeshUtil::CalculateNormal(mesh);
			};
			mesh_util_tbl["CalculateTangent"] = [](const std::shared_ptr<Mesh> &mesh) {
				MeshUtil::CalculateTangent(mesh);
			};

			// --- MeshSimplifier namespace table ------------------------------
			// Single entry point: SimplifyMesh(mesh, opts). `opts` is a
			// plain Lua table with optional lod_count / reduction_ratio /
			// target_error / lock_borders; nil/omitted means use defaults.
			// Returns { lod_meshes = {...}, thresholds = {...} } so the
			// caller can attach via mesh:SetLodMeshes(r.lod_meshes,
			// r.thresholds).
			sol::table mesh_simplifier_tbl = lua.create_named_table("MeshSimplifier");
			mesh_simplifier_tbl["SimplifyMesh"] = [](sol::this_state s,
				const std::shared_ptr<Mesh> &mesh, sol::object opts_obj) -> sol::table
			{
				sol::state_view lua(s);
				sol::table out = lua.create_table();
				if (!mesh) return out;

				MeshSimplifyOptions opts;
				if (opts_obj.valid() && opts_obj.is<sol::table>())
				{
					sol::table t = opts_obj;
					sol::object lc = t["lod_count"];
					if (lc.valid() && lc.is<int>())       opts.lod_count       = lc.as<int>();
					sol::object rr = t["reduction_ratio"];
					if (rr.valid() && rr.is<float>())     opts.reduction_ratio = rr.as<float>();
					sol::object te = t["target_error"];
					if (te.valid() && te.is<float>())     opts.target_error    = te.as<float>();
					sol::object lb = t["lock_borders"];
					if (lb.valid() && lb.is<bool>())      opts.lock_borders    = lb.as<bool>();
					// Method: 0 = Quadric (default), 1 = Sloppy, 2 = QuadricLegacy.
					sol::object m = t["method"];
					if (m.valid() && m.is<int>())
					{
						int mi = m.as<int>();
						if (mi == 1) opts.method = MeshSimplifyOptions::Method::Sloppy;
						else if (mi == 2) opts.method = MeshSimplifyOptions::Method::QuadricLegacy;
						else opts.method = MeshSimplifyOptions::Method::Quadric;
					}
				}

				MeshSimplifyResult res = SimplifyMesh(mesh, opts);

				sol::table lods_tbl = lua.create_table();
				for (size_t i = 0; i < res.lod_meshes.size(); ++i)
					lods_tbl[i + 1] = res.lod_meshes[i];
				sol::table thr_tbl = lua.create_table();
				for (size_t i = 0; i < res.thresholds.size(); ++i)
					thr_tbl[i + 1] = res.thresholds[i];
				out["lod_meshes"] = lods_tbl;
				out["thresholds"] = thr_tbl;
				return out;
			};
		}
	}
}
