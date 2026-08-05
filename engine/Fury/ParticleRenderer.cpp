#include "Fury/ParticleRenderer.h"

#include <cmath>

#include "Fury/EntityManager.h"
#include "Fury/GLLoader.h"
#include "Fury/Log.h"
#include "Fury/Material.h"
#include "Fury/Mesh.h"
#include "Fury/ParticleSystem.h"
#include "Fury/RenderUtil.h"
#include "Fury/Scene.h"
#include "Fury/SceneManager.h"
#include "Fury/SceneNode.h"
#include "Fury/Shader.h"
#include "Fury/Texture.h"
#include "Fury/Vector4.h"

namespace fury
{
	ParticleRenderer::Ptr ParticleRenderer::Create(const std::string &name)
	{
		return std::make_shared<ParticleRenderer>(name);
	}

	ParticleRenderer::ParticleRenderer(const std::string &name)
		: m_Name(name)
	{
		m_TypeIndex = typeid(ParticleRenderer);

		m_DynamicMesh = Mesh::Create("ParticleDynamicMesh:" + m_Name);
		m_DynamicMesh->Positions.SetBufferUsage(GL_DYNAMIC_DRAW);
		m_DynamicMesh->UVs.SetBufferUsage(GL_DYNAMIC_DRAW);
		m_DynamicMesh->Indices.SetBufferUsage(GL_DYNAMIC_DRAW);
	}

	bool ParticleRenderer::Load(const void *wrapper, bool object)
	{
		if (object && !IsObject(wrapper))
		{
			FURYE << "ParticleRenderer: json node is not an object!";
			return false;
		}

		std::string str;
		if (!LoadMemberValue(wrapper, "type", str) || str != "ParticleRenderer")
		{
			FURYE << "ParticleRenderer: invalid type " << str;
			return false;
		}

		LoadMemberValue(wrapper, "name", m_Name);

		// ParticleSystem asset reference (name; resolved at first draw).
		std::string sysName;
		LoadMemberValue(wrapper, "system", sysName);
		m_SystemName = sysName;
		m_System.reset();

		// Optional inline material binding (legacy + convenience).
		std::string matName;
		if (LoadMemberValue(wrapper, "material", matName) && !matName.empty())
		{
			if (auto mat = Scene::Manager()->Get<Material>(matName))
				SetMaterial(mat);
		}

		// Blend mode (RendererModule mirror for the transparent pass).
		unsigned int bm = 0;
		LoadMemberValue(wrapper, "blendMode", bm);
		m_BlendMode = static_cast<ParticleBlend>(bm);

		return true;
	}

	void ParticleRenderer::Save(void *wrapper, bool object)
	{
		if (object) StartObject(wrapper);

		SaveKey(wrapper, "type"); SaveValue(wrapper, "ParticleRenderer");
		SaveKey(wrapper, "name"); SaveValue(wrapper, m_Name);
		SaveKey(wrapper, "system"); SaveValue(wrapper, m_SystemName);

		if (auto mat = m_Material.lock())
		{
			SaveKey(wrapper, "material");
			SaveValue(wrapper, mat->GetName());
		}
		else
		{
			SaveKey(wrapper, "material");
			SaveValue(wrapper, std::string());
		}

		SaveKey(wrapper, "blendMode");
		SaveValue(wrapper, static_cast<unsigned int>(m_BlendMode));

		if (object) EndObject(wrapper);
	}

	Component::Ptr ParticleRenderer::Clone() const
	{
		auto clone = ParticleRenderer::Create(m_Name);
		clone->m_SystemName = m_SystemName;
		clone->m_Material = m_Material;
		clone->m_BlendMode = m_BlendMode;
		return clone;
	}

	void ParticleRenderer::SetMaterial(const std::shared_ptr<Material> &material)
	{
		m_Material = material;
	}

	std::shared_ptr<Material> ParticleRenderer::GetMaterial() const
	{
		return m_Material.lock();
	}

	void ParticleRenderer::ResolveSystem()
	{
		if (!m_System.expired()) return;
		if (m_SystemName.empty()) return;
		if (!Scene::Active) return;
		auto em = Scene::Active->GetEntityManager();
		if (!em) return;
		if (auto sys = em->Get<ParticleSystem>(m_SystemName))
		{
			m_System = sys;

			// Frustum culling tests the owner node's AABB, which only
			// covers the spawn point — particles drifting/rising beyond
			// it vanished when the camera orbited (the "particles
			// disappear on camera move" symptom). Expand the node's
			// AABB to the system's conservative extent, once, and
			// re-register with the scene manager so the octree sees
			// the new bounds.
			if (!m_BoundsApplied)
			{
				m_BoundsApplied = true;
				if (auto owner = m_Owner.lock())
				{
					BoxBounds bounds = owner->GetModelAABB();
					bounds.Encapsulate(sys->GetLocalBounds());
					owner->SetModelAABB(bounds);
					if (Scene::Active->GetSceneManager())
						Scene::Active->GetSceneManager()->UpdateSceneNode(owner);
				}
			}
		}
	}

	std::shared_ptr<ParticleSystem> ParticleRenderer::GetSystem() const
	{
		const_cast<ParticleRenderer *>(this)->ResolveSystem();
		return m_System.lock();
	}

	unsigned int ParticleRenderer::UpdateMesh(const Vector4 &camRight, const Vector4 &camUp)
	{
		ResolveSystem();
		auto system = m_System.lock();
		if (!system || !m_DynamicMesh) return 0;

		const auto &particles = system->GetParticles();

		// Camera axes in owner-local space: the baked quads face the
		// camera once the owner's world matrix applies at draw time.
		// Normalized so the owner's inverse scale doesn't shrink the
		// quad (size lives in `half`; forward scale still applies).
		Vector4 rightL = camRight;
		Vector4 upL = camUp;
		if (auto owner = m_Owner.lock())
		{
			Matrix4 inv = owner->GetInvertWorldMatrix();
			rightL = inv.Multiply(Vector4(camRight.x, camRight.y, camRight.z, 0.0f));
			upL = inv.Multiply(Vector4(camUp.x, camUp.y, camUp.z, 0.0f));
			rightL.Normalize();
			upL.Normalize();
		}
		rightL.w = 0.0f;
		upL.w = 0.0f;

		std::vector<float> positions;
		std::vector<float> uvs;
		std::vector<unsigned int> indices;
		positions.reserve(particles.size() * 4 * 3);
		uvs.reserve(particles.size() * 4 * 2);
		indices.reserve(particles.size() * 6);

		unsigned int alive = 0;
		for (const auto &p : particles)
		{
			if (!p.alive) continue;

			const float half = p.size * 0.5f;
			const float cs = std::cos(p.rotation);
			const float sn = std::sin(p.rotation);

			// View-plane corners, rotated by RotationOverLifetime.
			static const float kCorners[4][2] = {
				{ -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f }
			};
			for (int c = 0; c < 4; ++c)
			{
				const float rx = kCorners[c][0] * cs - kCorners[c][1] * sn;
				const float ry = kCorners[c][0] * sn + kCorners[c][1] * cs;
				positions.push_back(p.position.x + (rightL.x * rx + upL.x * ry) * half);
				positions.push_back(p.position.y + (rightL.y * rx + upL.y * ry) * half);
				positions.push_back(p.position.z + (rightL.z * rx + upL.z * ry) * half);
			}

			uvs.push_back(0.0f); uvs.push_back(0.0f);
			uvs.push_back(1.0f); uvs.push_back(0.0f);
			uvs.push_back(1.0f); uvs.push_back(1.0f);
			uvs.push_back(0.0f); uvs.push_back(1.0f);

			const unsigned int base = alive * 4;
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 0);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
			++alive;
		}

		m_DynamicMesh->Positions.Data = std::move(positions);
		m_DynamicMesh->UVs.Data = std::move(uvs);
		m_DynamicMesh->Indices.Data = std::move(indices);
		m_DynamicMesh->Positions.SetDirty();
		m_DynamicMesh->UVs.SetDirty();
		m_DynamicMesh->Indices.SetDirty();
		// Mesh-level dirty flag (inherited Buffer::m_Dirty) is what
		// actually triggers Shader::BindMesh's UpdateBuffer — without
		// it, per-buffer flags make BindMesh's post-upload check fail
		// and the draw silently reuses a stale/zero VAO.
		m_DynamicMesh->SetDirty();

		// Sync blend mode from the system's RendererModule.
		m_BlendMode = system->GetRenderer().blendMode;

		return alive;
	}

	std::shared_ptr<Shader> ParticleRenderer::BindForDraw()
	{
		ResolveSystem();
		auto system = m_System.lock();
		if (!system)
		{
			if (!m_WarnedNoSystem)
			{
				FURYW << "ParticleRenderer '" << m_Name << "' draw: no ParticleSystem '"
					  << m_SystemName << "' bound";
				m_WarnedNoSystem = true;
			}
			return nullptr;
		}
		if (!m_DynamicMesh) return nullptr;
		if (!m_DynamicMesh->Indices.Data.size())
		{
			if (!m_WarnedNoSystem)
			{
				FURYW << "ParticleRenderer '" << m_Name << "' draw: 0 live particles";
				m_WarnedNoSystem = true;
			}
			return nullptr;
		}

		// Shadow-receive systems (ALPHA + module flag) get the SHADOW
		// variant; everyone else compiles no shadow code at all.
		auto shader = GetParticleShader(m_BlendMode == ParticleBlend::ALPHA
			&& system->GetRenderer().receiveShadows);
		if (!shader) return nullptr;

		// Material resolution: the bound system's RendererModule is the
		// authoring source of truth (the particle editor edits it; the
		// blend mode already syncs from there in UpdateMesh). The
		// component's own slot is the fallback for renderers whose
		// system names no material.
		auto material = m_Material.lock();
		if (system && Scene::Active && Scene::Active->GetEntityManager())
		{
			const auto &matName = system->GetRenderer().materialName;
			if (!matName.empty())
				if (auto sysMat = Scene::Active->GetEntityManager()->Get<Material>(matName))
					material = sysMat;
		}
		auto diffuse = material ? material->GetTexture(Material::DIFFUSE_TEXTURE) : nullptr;
		auto diffuseTex = std::dynamic_pointer_cast<Texture>(diffuse);
		if (!diffuseTex) return nullptr;

		shader->Bind();
		// Named bind: sets the sampler uniform AND advances the texture
		// unit, so FinishDraw's shadow-map binds land on units 1+
		// (the unit-only overload would leave them overwriting unit 0).
		shader->BindTexture("diffuse", diffuseTex);
		shader->BindMesh(m_DynamicMesh);
		return shader;
	}

	void ParticleRenderer::FinishDraw(const std::shared_ptr<Shader> &shader, const ParticleShadowInfo *shadow)
	{
		// v1 single per-emitter tint: average of the first few live
		// particles' colors (per-particle color is CPU-side only).
		auto system = m_System.lock();
		Color tint(0.0f, 0.0f, 0.0f, 0.0f);
		unsigned int n = 0;
		if (system)
		{
			for (const auto &p : system->GetParticles())
			{
				if (!p.alive) continue;
				tint.r += p.color.r;
				tint.g += p.color.g;
				tint.b += p.color.b;
				tint.a += p.color.a;
				if (++n >= 16) break;
			}
		}
		if (n > 0)
		{
			tint.r /= static_cast<float>(n);
			tint.g /= static_cast<float>(n);
			tint.b /= static_cast<float>(n);
			tint.a /= static_cast<float>(n);
		}
		else
		{
			tint = Color::White;
		}
		shader->BindFloat("u_Tint", tint.r, tint.g, tint.b, tint.a);

		// Shadow binds — only meaningful on the SHADOW variant picked
		// in BindForDraw (the plain variant declares no shadow
		// uniforms; every bind below is a silent no-op there).
		// anyCaster = some light casts AND has a live map this frame:
		// without one there's nothing to receive from (stay full
		// bright); with one but no COVERING source, u_shadow_type 0
		// makes the shader floor to u_shadow_floor.
		if (shader == GetParticleShader(true))
		{
			const float receive = (shadow && shadow->anyCaster) ? 1.0f : 0.0f;
			int shadowType = 0;
			Texture::Ptr shadowCube, shadowTex2D, shadowTexCSM;
			if (receive == 1.0f && shadow->type > 0 && shadow->texture)
			{
				shadowType = shadow->type;
				if (shadowType == 1) shadowCube = shadow->texture;
				else if (shadowType == 2 || shadowType == 4) shadowTex2D = shadow->texture;
				else if (shadowType == 3) shadowTexCSM = shadow->texture;
			}
			shader->BindInt("u_shadow_type", shadowType);
			// Always-bind every shadow sampler — dummies must match
			// the declared sampler TYPE (gl-sampler-target-mismatch-trap).
			shader->BindTexture("shadow_buffer",
				shadowCube ? shadowCube : GetDummyCubeTexture());
			shader->BindTexture("shadow_map",
				shadowTex2D ? shadowTex2D : GetDummyTexture2D());
			shader->BindTexture("shadow_buffer_csm",
				shadowTexCSM ? shadowTexCSM : GetDummyTexture2DArray());
			// Each compare branch reads only the uniforms its type binds.
			if (shadowType == 1 || shadowType == 4)
			{
				shader->BindFloat("u_shadow_light_pos",
					shadow->lightPos.x, shadow->lightPos.y, shadow->lightPos.z);
				shader->BindFloat("u_shadow_light_radius", shadow->lightRadius);
			}
			if (shadowType == 4)
			{
				shader->BindFloat("u_shadow_light_dir",
					shadow->lightDir.x, shadow->lightDir.y, shadow->lightDir.z);
				shader->BindFloat("u_shadow_half_angles",
					shadow->coneHalfInner, shadow->coneHalfOuter);
			}
			if (shadowType == 2 || shadowType == 4)
				shader->BindMatrix("shadow_matrix", &shadow->matrix.Raw[0]);
			if (shadowType == 3)
			{
				shader->BindMatrices("shadow_matrix_csm", 4, &shadow->csmMatrices[0].Raw[0]);
				shader->BindFloat("shadow_far",
					shadow->shadowFar.x, shadow->shadowFar.y,
					shadow->shadowFar.z, shadow->shadowFar.w);
			}
			shader->BindFloat("u_receive_shadows", receive);
		}

		glDrawElements(GL_TRIANGLES,
			static_cast<GLsizei>(m_DynamicMesh->Indices.Data.size()),
			GL_UNSIGNED_INT, 0);

		RenderUtil::Instance()->IncreaseDrawCall();
		RenderUtil::Instance()->IncreaseTriangleCount(m_DynamicMesh->Indices.Data.size());

		shader->UnBind();
	}

	void ParticleRenderer::Draw(const std::shared_ptr<SceneNode> &cameraNode, const ParticleShadowInfo *shadow)
	{
		auto shader = BindForDraw();
		if (!shader) return;

		if (cameraNode)
			shader->BindCamera(cameraNode);
		if (auto owner = m_Owner.lock())
			shader->BindMatrix(Matrix4::WORLD_MATRIX, owner->GetWorldMatrix());
		else
		{
			Matrix4 id;
			id.Identity();
			shader->BindMatrix(Matrix4::WORLD_MATRIX, id);
		}

		FinishDraw(shader, shadow);
	}

	void ParticleRenderer::Draw(const Matrix4 &view, const Matrix4 &proj)
	{
		auto shader = BindForDraw();
		if (!shader) return;

		// Editor-preview path: particles draw at the origin the preview
		// frames on (owner world transform stays in the scene).
		Matrix4 id;
		id.Identity();
		shader->BindMatrix(Matrix4::WORLD_MATRIX, id);
		shader->BindMatrix(Matrix4::INVERT_VIEW_MATRIX, view);
		shader->BindMatrix(Matrix4::PROJECTION_MATRIX, proj);

		FinishDraw(shader, nullptr);
	}

	void ParticleRenderer::OnAttaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnAttaching(node);
		ResolveSystem();
	}

	void ParticleRenderer::OnDetaching(const std::shared_ptr<SceneNode> &node)
	{
		Component::OnDetaching(node);
		m_System.reset();
	}
}