#ifndef _FURY_POSTPROCESS_EFFECT_H_
#define _FURY_POSTPROCESS_EFFECT_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Fury/Entity.h"
#include "Fury/EnumUtil.h"
#include "Fury/Uniform.h"

namespace fury
{
	// Optional per-uniform editor metadata, declared in the effect
	// JSON next to the default value: "tip" (hover help, shown in
	// the Edit dialog -- include tuning guidance like world units)
	// plus "min"/"max" (a rough adjustment range: values are clamped
	// to it in the dialog and the range shows in the tooltip).
	struct UniformMeta
	{
		std::string tip;
		float min = 0.0f;
		float max = 0.0f;
		bool hasRange = false;
	};

	// Data-driven postprocess effect: name + shader path + declared
	// inputs/output format + uniform defaults. Mirrors the pipeline's
	// JSON shape so the same Serializable / LoadArray machinery that
	// loads textures/shaders/passes can load effects (see FileUtil +
	// the engine's existing pipeline-loader flow). Loaded effects are
	// resolved by name from a shared `Resource/PostProcess/*.json`
	// registry; render settings hold {name, enabled} references that
	// the pipeline expands at render time.
	class FURY_API PostProcessEffect : public Entity
	{
	public:

		typedef std::shared_ptr<PostProcessEffect> Ptr;

		static Ptr Create(const std::string &name);

	protected:

		// Path to the .glsl file (relative to working dir, like Shader
		// shaders do in DefferedLightingLambert.json).
		std::string m_ShaderPath;

		// Optional #defines passed to the shader compiler (mirrors
		// Shader::m_Defines).
		std::vector<std::string> m_ShaderDefines;

		// Declared input texture names. Each is bound to a sampler2D
		// of the same name in the shader. Order matters: chain slot
		// i reads the output of slot i-1.
		std::vector<std::string> m_Inputs;

		// Output target format. ACES needs rgba16f to receive HDR
		// input; FXAA / CRT write LDR rgba8. Defaults to RGBA8.
		TextureFormat m_OutputFormat = TextureFormat::RGBA8;

		// Chain placement (engine-owned order -- users toggle on/off
		// only, never reorder). PRE_TONEMAP effects run on linear
		// scene color pre-ACES, TONEMAP is the HDR->LDR pivot (auto
		// injected/stripped with the HDR switch), POST_TONEMAP runs
		// last on LDR. JSON "stage"; default infers PRE_TONEMAP when
		// any declared input is $gbuffer_*, else POST_TONEMAP.
		PostProcessStage m_Stage = PostProcessStage::POST_TONEMAP;

		// Tie-breaker within a stage (JSON "order", default 0); the
		// chain sorts by (stage, order, name).
		int m_Order = 0;


		// 0 = match the active render target (the screen or the
		// editor's offscreen RT). Otherwise the effect renders into a
		// temporary at this size. Currently only the 0 path is wired
		// up -- chain slots share the active target's size via the
		// ping-pong Texture::GetTemporary helpers.
		unsigned int m_OutputWidth = 0;
		unsigned int m_OutputHeight = 0;

		// Default uniform values applied before the effect runs. The
		// editor can override these per-effect from the chain editor;
		// here we carry the values declared in the JSON descriptor.
		std::unordered_map<std::string, std::shared_ptr<UniformBase>> m_Uniforms;

		// Optional editor metadata per uniform (see UniformMeta).
		std::unordered_map<std::string, UniformMeta> m_UniformMeta;

		// Optional effect-level description (JSON "description"),
		// shown as the chain row's hover tooltip in the editor.
		std::string m_Description;

	public:

		PostProcessEffect(const std::string &name);

		virtual ~PostProcessEffect();

		virtual bool Load(const void* wrapper, bool object = true) override;

		virtual void Save(void* wrapper, bool object = true) override;

		const std::string &GetShaderPath() const;

		void SetShaderPath(const std::string &path);

		const std::vector<std::string> &GetShaderDefines() const;

		void AddShaderDefine(const std::string &define);

		const std::vector<std::string> &GetInputs() const;

		void AddInput(const std::string &name);

		TextureFormat GetOutputFormat() const;

		void SetOutputFormat(TextureFormat fmt);

		PostProcessStage GetStage() const;

		void SetStage(PostProcessStage stage);

		int GetOrder() const;

		void SetOrder(int order);

		unsigned int GetOutputWidth() const;

		unsigned int GetOutputHeight() const;

		void SetOutputSize(unsigned int w, unsigned int h);

		const std::unordered_map<std::string, std::shared_ptr<UniformBase>> &GetUniforms() const;

		void SetUniform(const std::string &name, const std::shared_ptr<UniformBase> &ptr);

		std::shared_ptr<UniformBase> GetUniform(const std::string &name) const;

		// Editor metadata for a uniform; nullptr when the descriptor
		// declares none.
		const UniformMeta *GetUniformMeta(const std::string &name) const;

		const std::string &GetDescription() const;
	};
}

#endif // _FURY_POSTPROCESS_EFFECT_H_