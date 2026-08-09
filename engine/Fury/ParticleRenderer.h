#ifndef _FURY_PARTICLE_RENDERER_H_
#define _FURY_PARTICLE_RENDERER_H_

#include <memory>
#include <string>

#include "Fury/Component.h"
#include "Fury/Matrix4.h"
#include "Fury/Mesh.h"
#include "Fury/ParticleModules.h"
#include "Fury/Vector4.h"

namespace fury { class Material; class SceneNode; class Texture; class Shader; class ParticleSystem; }

namespace fury
{
	// Shadow-receive inputs for one draw. The pipeline picks the
	// dominant casting light per-emitter (rankShadowSourcesFor) and
	// fills a single set of inputs -- `type` selects which compare to
	// run (matches the deferred shader conventions):
	//   0 = none
	//   1 = point cube   (texture = cube map, lightPos/lightRadius)
	//   2 = dir-single   (texture = 2D map, matrix = view->shadow UV)
	//   3 = CSM          (texture = 2DArray, csmMatrices[4] + shadowFar)
	//   4 = spot 2D      (texture = 2D map, matrix = spot view-proj,
	//                     lightPos/lightDir/coneHalfAngles for the
	//                     per-fragment cone test -- Particle.glsl is
	//                     emissive, so the shadow factor carries the
	//                     cone falloff the light loop applies elsewhere)
	// The "single dominant shadow per particle draw" matches the
	// previous working behavior. Multi-light (4 spot/point + 1 dir)
	// per draw is on the roadmap; for now it lets each emitter focus
	// on the single local shadow caster that matters most, while the
	// mesh transparent additive loop continues to evaluate every
	// light per unit (multi-light there already).
	struct ParticleShadowInfo
	{
		int type = 0;
		std::shared_ptr<Texture> texture;
		// True when any light casts AND has a live map this frame.
		// Without one the draw must stay full bright (nothing to
		// receive FROM); with one, type 0 means "no covering source"
		// and the shader floors to u_shadow_floor.
		bool anyCaster = false;
		Vector4 lightPos;
		float lightRadius = 1.0f;
		Matrix4 matrix;
		// Spot (type=4): world forward + (halfInner, halfOuter) rad.
		Vector4 lightDir = Vector4(0, -1, 0, 0);
		float coneHalfInner = 0.0f;
		float coneHalfOuter = 0.0f;
		// CSM (type=3) -- populated but unused for types 1/2/4.
		Matrix4 csmMatrices[4];
		Vector4 shadowFar;
	};
	// Companion to a ParticleSystem asset. The ParticleSystem lives in
	// the active Scene's EntityManager (just like Mesh / Material) and
	// is referenced by name here. The renderer mirrors the
	// MeshRender <-> Material pairing -- one component per scene node,
	// resolves the asset by name at Load time.
	//
	// Per-particle color is tracked on the CPU side (the editor inspector
	// reads it for the live-count header). v1 shader applies a single
	// per-emitter tint uniform over the diffuse texture.
	class FURY_API ParticleRenderer : public Component
	{
	public:
		typedef std::shared_ptr<ParticleRenderer> Ptr;

		static Ptr Create(const std::string &name = "ParticleRenderer");

		ParticleRenderer(const std::string &name = "ParticleRenderer");

		virtual bool Load(const void* wrapper, bool object = true) override;
		virtual void Save(void* wrapper, bool object = true) override;
		Component::Ptr Clone() const override;

		const std::string &GetName() const { return m_Name; }
		void SetName(const std::string &name) { m_Name = name; }

		// Name of the ParticleSystem asset to draw. Empty = unbound.
		const std::string &GetSystemName() const { return m_SystemName; }
		void SetSystemName(const std::string &name) { m_SystemName = name; m_System.reset(); }

		// Resolved weak_ptr<ParticleSystem> from the active scene's
		// EntityManager. May be expired (returns nullptr) if the
		// ParticleSystem asset isn't loaded yet -- the renderer's draw
		// path skips when expired.
		std::shared_ptr<ParticleSystem> GetSystem() const;

		// Texture binding (same DIFFUSE_TEXTURE slot as meshes).
		void SetMaterial(const std::shared_ptr<Material> &material);
		std::shared_ptr<Material> GetMaterial() const;

		ParticleBlend GetBlendMode() const { return m_BlendMode; }
		void SetBlendMode(ParticleBlend mode) { m_BlendMode = mode; }

		std::shared_ptr<Mesh> GetDynamicMesh() const { return m_DynamicMesh; }

		// Pack the bound system's live pool into the dynamic mesh as
		// camera-facing quads (CPU billboarding). camRight/camUp are the
		// camera's axes in WORLD space; they're transformed into
		// owner-local space here so the baked quads face the camera once
		// the owner's world matrix applies at draw time. Also applies
		// RotationOverLifetime (rotation around the view axis).
		// Returns the alive count.
		unsigned int UpdateMesh(const Vector4 &camRight, const Vector4 &camUp);

		// Render the dynamic mesh with the ParticleShader. The cameraNode
		// overload is the pipeline path (binds camera + owner world matrix).
		// The view/proj overload is the editor-preview path: identity world
		// (particles draw at the origin the preview frames on) and explicit
		// orbit-camera matrices. `shadow` (pipeline path only) carries the
		// frame's shadow-receive inputs; nullptr = no shadow sampling.
		void Draw(const std::shared_ptr<SceneNode> &cameraNode, const ParticleShadowInfo *shadow = nullptr);
		void Draw(const Matrix4 &view, const Matrix4 &proj);

	protected:
		virtual void OnAttaching(const std::shared_ptr<SceneNode> &node) override;
		virtual void OnDetaching(const std::shared_ptr<SceneNode> &node) override;

		// Lazy lookup of the ParticleSystem asset by name.
		void ResolveSystem();

		// Shared head of Draw: validates system/mesh/texture, binds the
		// shader + diffuse texture + mesh. Returns the shader (nullptr =
		// skip the draw; caller must not touch GL state further).
		std::shared_ptr<Shader> BindForDraw();

		// Shared tail of Draw: tint uniform + shadow uniforms +
		// glDrawElements + UnBind.
		void FinishDraw(const std::shared_ptr<Shader> &shader, const ParticleShadowInfo *shadow);

	private:
		std::string m_Name;
		std::string m_SystemName;
		mutable std::weak_ptr<ParticleSystem> m_System;
		std::weak_ptr<Material> m_Material;
		Mesh::Ptr m_DynamicMesh;
		ParticleBlend m_BlendMode = ParticleBlend::ALPHA;
		bool m_WarnedNoSystem = false;
		bool m_BoundsApplied = false;
	};
}

#endif // _FURY_PARTICLE_RENDERER_H_