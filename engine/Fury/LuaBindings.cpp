#include <sol/sol.hpp>

#include "Fury/LuaBindings.h"

#include "Fury/Camera.h"
#include "Fury/Component.h"
#include "Fury/Engine.h"
#include "Fury/Entity.h"
#include "Fury/EnumUtil.h"
#include "Fury/FileUtil.h"
#include "Fury/Gui.h"
#include "Fury/Log.h"
#include "Fury/MathUtil.h"
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

namespace fury
{
	namespace LuaBindings
	{
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
				sol::no_constructor);  // abstract; produced via OcTree::Create

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
				"Create", &OcTree::Create);

			// --- Scene ---------------------------------------------------------
			lua.new_usertype<Scene>("Scene",
				sol::no_constructor,
				sol::base_classes, sol::bases<Entity, Serializable>(),
				"Create", &Scene::Create,
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
				"GetChildCount", &SceneNode::GetChildCount,
				"GetChildAt", &SceneNode::GetChildAt);

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

			// --- Gui (free functions in a Lua table) --------------------------
			sol::table gui_tbl = lua.create_named_table("Gui");
			gui_tbl["ShowDefault"] = &Gui::ShowDefault;
			gui_tbl["Render"] = &Gui::Render;

			// --- RenderUtil (singleton; no methods bound this round) ----------
			lua.new_usertype<RenderUtil>("RenderUtil",
				sol::no_constructor);
			lua["RenderUtil"]["Instance"] = []() { return RenderUtil::Instance(); };

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

				Engine::Run(*window, cb, opts);
			};
		}
	}
}
