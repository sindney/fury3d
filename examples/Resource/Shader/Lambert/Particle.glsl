#version 330

// Particle billboard shader. Samples the bound Material's diffuse
// texture and multiplies by u_Tint — particles are emissive in v1
// (no scene-light sampling); see the particle-system spec.
//
// Invariants (do not break — see tasks.md 8.1/11.5):
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

// Shadow-receive block (u_shadow_type: 0 none, 1 point cube,
// 2 directional 2D). Conventions copied from the deferred shaders:
// point = radial distance vs texture*radius (PointLight.glsl); dir =
// z>1 lit else z<tex (SunLight.glsl, bias lives in the caster pass's
// polygon offset).
uniform samplerCube shadow_buffer;
uniform sampler2D shadow_map;
uniform int u_shadow_type = 0;
uniform vec3 u_shadow_light_pos;
uniform float u_shadow_light_radius = 1.0;
uniform mat4 shadow_matrix;
uniform float u_shadow_floor = 0.25;
uniform float u_receive_shadows = 0.0;

out vec4 fragment_output;

float ShadowFactor()
{
	if (u_shadow_type == 1)
	{
		vec3 dir = v_world_pos - u_shadow_light_pos;
		float current = length(dir);
		// Outside the light's radius there's no shadow info (and no
		// light influence) — count as lit.
		if (current > u_shadow_light_radius) return 1.0;
		float closest = texture(shadow_buffer, dir).x * u_shadow_light_radius;
		// Billboards have no geometric normal for the slope term —
		// distance-scaled bias only (PointLight.glsl's first term + a
		// radius-fraction constant).
		float bias = 0.002 * (u_shadow_light_radius / 10.0) + u_shadow_light_radius / 128.0;
		return float(current - bias < closest);
	}
	if (u_shadow_type == 2)
	{
		vec4 sc = shadow_matrix * vec4(v_world_pos, 1.0);
		sc = sc / sc.w;
		if (sc.z > 1.0) return 1.0;
		return float(sc.z < texture(shadow_map, sc.xy).x);
	}
	return 1.0;
}

void main()
{
	// Linear depth — see the header comment.
	gl_FragDepth = -v_view_z / camera_far;
	vec4 tex = texture(diffuse, v_uv);
	float sf = mix(1.0, mix(u_shadow_floor, 1.0, ShadowFactor()), u_receive_shadows);
	fragment_output = vec4(tex.rgb * u_Tint.rgb * sf, tex.a * u_Tint.a);
}

#endif
