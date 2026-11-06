#version 330

#include "../GBufferCommon.glsl"
#include "LocalLightCommon.glsl"

#ifdef VERTEX_SHADER

in vec3 vertex_position;

out vec3 vs_dir;
out vec3 vs_pos;
out vec4 ss_pos;

uniform vec3 light_dir;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
	vs_dir = normalize(invert_view_matrix * vec4(-light_dir, 0)).xyz;
	vs_pos = (invert_view_matrix * world_matrix * vec4(vertex_position, 1.0)).xyz;
	ss_pos = projection_matrix * vec4(vs_pos, 1);
	gl_Position = ss_pos;
}

#endif

#ifdef FRAGMENT_SHADER

out vec4 fragment_output;

in vec3 vs_dir;
in vec3 vs_pos;
in vec4 ss_pos;

uniform float camera_far = 10000;

uniform mat4 invert_view_matrix;

uniform vec3 light_pos;
uniform vec3 light_color;
uniform float light_falloff;
uniform float light_radius;
uniform float light_intensity;
uniform float light_innerangle;
uniform float light_outterangle;

// linear depth
uniform sampler2D gbuffer_depth;
// normal (view space *0.5+0.5); PBR: roughness in alpha
uniform sampler2D gbuffer_normal;

#ifdef PBR
// albedo rgb, metallic in alpha (see SunLight.glsl PBR block).
uniform sampler2D gbuffer_diffuse;
#endif

#ifdef SHADOW
uniform mat4 shadow_matrix;
uniform sampler2D shadow_buffer;
#endif

vec3 pos_from_depth(const in vec2 screenUV) 
{
 	float depth = texture(gbuffer_depth, screenUV).r;
	vec3 view_ray = vec3(vs_pos.xy * (camera_far / vs_pos.z), camera_far);
	return -view_ray * depth;
}

vec4 apply_lighting(const in vec3 normal, const in vec3 surface_pos)
{
	vec3 vs_light_pos = (invert_view_matrix * vec4(light_pos, 1)).xyz;

	vec3 L = vs_light_pos - surface_pos;

	float dist = length(L);
	float attenuation = LocalLightAttenuation(dist, light_radius, light_falloff);

	L = normalize(L);

	attenuation *= LocalSpotAttenuation(vs_dir, L, light_innerangle, light_outterangle);

	vec3 N = normalize(normal);

	float NdotL = max(0.0, dot(N, L));

	return vec4(
		vec3(1) * light_color * NdotL * attenuation * light_intensity, 1.0
	);
}

#ifdef PBR
// GGX (Cook-Torrance) BRDF — see SunLight.glsl for the units /
// normalization rationale. Shares the cone-attenuation
// shape with apply_lighting above.
vec4 apply_lighting_pbr(const in vec3 normal, const in vec3 surface_pos,
	const in vec3 albedo, const in float metallic, const in float roughness)
{
	vec3 vs_light_pos = (invert_view_matrix * vec4(light_pos, 1)).xyz;

	vec3 L = vs_light_pos - surface_pos;

	float dist = length(L);
	float attenuation = LocalLightAttenuation(dist, light_radius, light_falloff);

	L = normalize(L);

	attenuation *= LocalSpotAttenuation(vs_dir, L, light_innerangle, light_outterangle);

	vec3 N = normalize(normal);
	vec3 V = normalize(-surface_pos); // view space: camera at origin

	float NdotL = max(0.0, dot(N, L));

	vec3 brdf = LocalLightGGX(N, V, L, albedo, metallic, roughness);

	return vec4(
		light_color * NdotL * attenuation * light_intensity * brdf, 1.0
	);
}
#endif

void main()
{
	vec2 screenUV = (ss_pos.xy / ss_pos.w) * 0.5 + 0.5;

#ifdef PBR
	// Sky mask — see SunLight.glsl.
	if (GBufferIsFarDepth(texture(gbuffer_depth, screenUV).r))
	{
		fragment_output = vec4(0.0);
		return;
	}
#endif

	vec3 vs_surface_pos = pos_from_depth(screenUV);

	vec4 raw_normal = texture(gbuffer_normal, screenUV);
	vec3 vs_normal = GBufferDecodeNormal(raw_normal);

#ifdef PBR
	vec4 diffuse = texture(gbuffer_diffuse, screenUV);
	fragment_output = apply_lighting_pbr(vs_normal, vs_surface_pos,
		GBufferDecodeDiffuse(diffuse), GBufferDecodeMetallic(diffuse),
		GBufferDecodeRoughness(raw_normal));
#else
	fragment_output = apply_lighting(vs_normal, vs_surface_pos);
#endif

#ifdef SHADOW
	fragment_output *= LocalSpotShadow(shadow_buffer, shadow_matrix, vs_surface_pos);
#endif
}

#endif