#version 330

// Particle billboard shader. Samples the bound Material's diffuse
// texture and multiplies by u_Tint — particles are emissive in v1
// (no scene-light sampling); see the particle-system spec.
//
// Shadow-receive: ONE shadow source per draw (single light picked
// per-renderer by the pipeline's rankShadowSourcesFor). u_shadow_type:
//   0 = none          (no covering source; sf = u_shadow_floor)
//   1 = point cube    (cube radial compare, deferred PointLight.glsl)
//   2 = dir-single 2D (z>1 lit else z<tex, deferred SunLight.glsl)
//   3 = CSM 2DArray   (same compare, cascade pick on linear view depth)
//   4 = spot 2D       (same z compare + CONE TEST: particles are
//                       emissive with no light loop, so the shadow
//                       factor must carry the cone falloff that the
//                       deferred/forward paths get from their spot
//                       attenuation term)
//
// Invariants (do not break):
// - gl_FragDepth MUST be the gbuffer's linear view depth
//   (-viewZ / camera_far) or pass_transparent's depth test compares
//   unlike spaces and particles vanish against opaque surfaces.
// - Every declared sampler MUST be bound every draw (dummies allowed):
//   core GL rejects glDrawElements when a sampler's texture target
//   mismatches (unbound samplerCube defaults to unit 0's 2D texture).

uniform mat4 world_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 projection_matrix;

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec2 vertex_uv;

out vec2 v_uv;
out float v_view_z;
out vec3 v_world_pos;

void main()
{
	v_uv = vertex_uv;
	vec4 worldPos = world_matrix * vec4(vertex_position, 1.0);
	v_world_pos = worldPos.xyz;
	vec4 viewPos = invert_view_matrix * worldPos;
	v_view_z = viewPos.z;
	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

in vec2 v_uv;
in float v_view_z;
in vec3 v_world_pos;

uniform sampler2D diffuse;
uniform vec4 u_Tint;
// Bound by Shader::BindCamera; default covers the editor-preview path
// which binds matrices only.
uniform float camera_far = 10000;

#ifdef SHADOW
// Shadow-receive block (compiled into the shadow variant only —
// RenderUtil::GetParticleShader(shadow=true); ParticleRenderer picks
// it when the system has receiveShadows + ALPHA blend). Three
// samplers so the dummy-bind rule is uniform: shadow_buffer (cube —
// point), shadow_map (2D — dir/spot), shadow_buffer_csm (2DArray —
// CSM). The pipeline binds the matching one for the active type and
// dummies for the others.
uniform samplerCube shadow_buffer;
uniform sampler2D shadow_map;
uniform sampler2DArray shadow_buffer_csm;
uniform int u_shadow_type = 0;
// u_shadow_type 1 (point): light world pos + effective radius.
// u_shadow_type 4 (spot): light world pos + forward + (halfInner,
// halfOuter) radians for the cone test.
uniform vec3 u_shadow_light_pos;
uniform float u_shadow_light_radius = 1.0;
uniform vec3 u_shadow_light_dir;
uniform vec2 u_shadow_half_angles;
// u_shadow_type 2/4 (dir-single, spot): view->shadow UV matrix.
uniform mat4 shadow_matrix;
// u_shadow_type 3 (CSM): 4 cascade view->shadow UV matrices + quarter-far split.
uniform mat4 shadow_matrix_csm[4];
uniform vec4 shadow_far;
uniform float u_shadow_floor = 0.25;
uniform float u_receive_shadows = 0.0;
#endif

out vec4 fragment_output;

#ifdef SHADOW
float ShadowFactor()
{
	if (u_shadow_type == 1)
	{
		// Point cube: radial distance vs tex*radius (deferred
		// PointLight.glsl convention). No geometric normal here
		// (billboards) so a distance-scaled bias, no slope term.
		vec3 dir = v_world_pos - u_shadow_light_pos;
		float current = length(dir);
		if (current > u_shadow_light_radius) return 1.0;
		float closest = texture(shadow_buffer, dir).x * u_shadow_light_radius;
		float bias = 0.002 * (u_shadow_light_radius / 10.0)
		           + u_shadow_light_radius / 128.0;
		return float(current - bias < closest);
	}
	if (u_shadow_type == 2)
	{
		// Dir-single: z>1 lit else z<tex (deferred SunLight.glsl).
		// Bias lives in the caster polygon offset.
		vec4 sc = shadow_matrix * vec4(v_world_pos, 1.0);
		sc = sc / sc.w;
		if (sc.z > 1.0) return 1.0;
		return float(sc.z < texture(shadow_map, sc.xy).x);
	}
	if (u_shadow_type == 4)
	{
		// Spot: same z compare PLUS the cone test — particles are
		// emissive (no light loop to zero out-of-cone radiance like
		// the deferred/forward spot), so the cone shape must live in
		// the shadow factor. Outside the cone or past the light
		// radius -> 0.0 (sf drops to u_shadow_floor); unguarded, the
		// map's CLAMP_TO_BORDER white compared as fully lit.
		vec3 toFrag = v_world_pos - u_shadow_light_pos;
		float fragDist = length(toFrag);
		if (fragDist < 1e-3) return 1.0; // at the light apex
		float theta = acos(clamp(dot(toFrag / fragDist, u_shadow_light_dir), -1.0, 1.0));
		if (theta >= u_shadow_half_angles.y) return 0.0;
		vec4 sc = shadow_matrix * vec4(v_world_pos, 1.0);
		sc = sc / sc.w;
		if (sc.z > 1.0) return 0.0; // past the light's radius: unlit
		float vis = float(sc.z < texture(shadow_map, sc.xy).x);
		// Penumbra ramp (deferred SpotLight.glsl): 1 at inner, 0 at outer.
		float cone = theta <= u_shadow_half_angles.x ? 1.0
			: (u_shadow_half_angles.y - theta)
			/ (u_shadow_half_angles.y - u_shadow_half_angles.x);
		return vis * cone;
	}
	if (u_shadow_type == 3)
	{
		// CSM: cascade pick on linear view depth; shadow_far holds
		// NEGATIVE split thresholds, so `vz > shadow_far.i` walks
		// near -> far (the deferred SunLight's sign is flipped
		// because it compares projected clip-z instead).
		float vz = v_view_z;
		int index = 3;
		if (vz > shadow_far.x) index = 0;
		else if (vz > shadow_far.y) index = 1;
		else if (vz > shadow_far.z) index = 2;
		vec4 sc = shadow_matrix_csm[index] * vec4(v_world_pos, 1.0);
		sc = sc / sc.w;
		if (sc.z > 1.0) return 1.0;
		return float(sc.z < texture(shadow_buffer_csm, vec3(sc.xy, float(index))).x);
	}
	return 1.0;
}
#endif

void main()
{
	// Linear depth — see the header comment.
	gl_FragDepth = -v_view_z / camera_far;
	vec4 tex = texture(diffuse, v_uv);
#ifdef SHADOW
	// no source covering this emitter (type 0) -> unlit floor;
	// otherwise mix(floor, lit) by the shadow factor.
	float sf;
	if (u_receive_shadows <= 0.0)
		sf = 1.0;
	else if (u_shadow_type == 0)
		sf = u_shadow_floor;
	else
		sf = mix(u_shadow_floor, 1.0, ShadowFactor());
#else
	float sf = 1.0;
#endif
	fragment_output = vec4(tex.rgb * u_Tint.rgb * sf, tex.a * u_Tint.a);
}

#endif
