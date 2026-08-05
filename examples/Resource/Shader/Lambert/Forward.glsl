#version 330

// Forward pass for BLEND materials (transparent-rendering). Drawn
// after the deferred combine into the composite target: one
// ambient/emissive base draw (u_light_type = 0, blend ALPHA) plus one
// additive draw per light (u_light_type = 1/2/3 = dir/point/spot,
// blend SRC_ALPHA/ONE). Single-light uniforms mirror BindLight in the
// deferred light shaders. Defines: PBR (HDR pipeline), COLOR_ONLY
// (no diffuse texture), SKINNED_MESH.

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec2 vertex_uv;
in vec3 vertex_normal;

#ifdef SKINNED_MESH
in ivec4 bone_ids;
in vec4 bone_weights;
uniform mat4 bone_matrices[35];
#endif

out vec3 out_normal;
out vec2 out_uv;
out vec3 vs_pos;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
#ifdef SKINNED_MESH
	mat4 bone_matrix = bone_matrices[bone_ids[0]] * bone_weights[0];
	bone_matrix += bone_matrices[bone_ids[1]] * bone_weights[1];
	bone_matrix += bone_matrices[bone_ids[2]] * bone_weights[2];
	bone_matrix += bone_matrices[bone_ids[3]] * (1.0f - bone_weights[0] - bone_weights[1] - bone_weights[2]);
	vec4 worldPos = world_matrix * bone_matrix * vec4(vertex_position, 1.0);
	out_normal = normalize(invert_view_matrix * world_matrix * bone_matrix * vec4(vertex_normal, 0.0)).xyz;
#else
	vec4 worldPos = world_matrix * vec4(vertex_position, 1.0);
	out_normal = normalize(invert_view_matrix * world_matrix * vec4(vertex_normal, 0.0)).xyz;
#endif

	vec4 viewPos = invert_view_matrix * worldPos;
	vs_pos = viewPos.xyz;
	out_uv = vertex_uv;

	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 out_normal;
in vec2 out_uv;
in vec3 vs_pos;

out vec4 fragment_output;

uniform mat4 invert_view_matrix;

uniform float camera_far = 10000;

#ifndef COLOR_ONLY
uniform sampler2D diffuse_texture;
#else
uniform vec3 diffuse_color;
#endif

uniform vec3 ambient_color;
uniform vec3 emissive_color;

uniform float ambient_factor = 1;
uniform float diffuse_factor = 1;
uniform float emissive_factor = 1;

// alpha = base.a * (1 - transparency), matching the gbuffer shaders.
uniform float transparency = 0.0;
uniform float u_alpha_cutoff = -1.0;

// Ambient floor — same value as PbrCombine's u_ambient so transparent
// and opaque unlit regions match.
uniform float u_ambient = 0.01;

// 0 = base (ambient/emissive), 1 = directional, 2 = point, 3 = spot.
uniform int u_light_type = 0;

#ifdef SHADOW
// Shadow-receive for the per-light additive contribution
// (u_shadow_type: 0 none, 1 point cube, 2 dir-single 2D,
// 3 CSM 2DArray, 4 spot 2D). Compiled only into the *_shadow_shader
// variants (pipeline JSON); DrawUnit picks them when the draw's
// light casts shadows.
uniform samplerCube shadow_buffer;
uniform sampler2D shadow_map;
uniform sampler2DArray shadow_buffer_csm;
uniform int u_shadow_type = 0;
uniform mat4 shadow_matrix;
uniform mat4 shadow_matrix_csm[4];
uniform vec4 shadow_far;
#endif

uniform vec3 light_pos;
uniform vec3 light_dir;
uniform vec3 light_color;
uniform float light_intensity;
uniform float light_innerangle;
uniform float light_outterangle;
uniform float light_falloff;
uniform float light_radius;

#ifdef PBR
// See GBuffer.glsl — roughness_factor < 0 = legacy slot, derive from
// shininess with the exact inverse of the light-side conversion.
uniform float metallic_factor = 0.0;
uniform float roughness_factor = -1.0;
uniform float shininess = 32.0;
#endif

void main()
{
#ifdef COLOR_ONLY
	vec4 base = vec4(diffuse_color.rgb, 1.0);
#else
	vec4 base = texture(diffuse_texture, out_uv);
#endif

	float alpha = base.a * (1.0 - transparency);
	if (u_alpha_cutoff >= 0.0 && alpha < u_alpha_cutoff)
		discard;

	// The gbuffer stores linear view depth via gl_FragDepth; match it
	// or the depth test against gbuffer_depth compares unlike spaces.
	// Written before any early return — gl_FragDepth is undefined on
	// paths that don't write it.
	gl_FragDepth = -vs_pos.z / camera_far;

	vec3 albedo = base.rgb * diffuse_factor + ambient_color * ambient_factor;
	vec3 N = normalize(out_normal);

	if (u_light_type == 0)
	{
		vec3 col = albedo * u_ambient + emissive_color * emissive_factor;
		fragment_output = vec4(col, alpha);
		return;
	}

	// Per-light additive contribution.
	vec3 L;
	float attenuation = 1.0;

	if (u_light_type == 1)
	{
		L = normalize((invert_view_matrix * vec4(-light_dir, 0.0)).xyz);
	}
	else
	{
		vec3 vs_light_pos = (invert_view_matrix * vec4(light_pos, 1.0)).xyz;
		vec3 toLight = vs_light_pos - vs_pos;
		float dist = length(toLight);
		attenuation = pow(max(0.0, 1.0 - dist / light_radius), light_falloff + 1.0);
		L = normalize(toLight);

		if (u_light_type == 3)
		{
			float halfInner = light_innerangle * 0.5;
			float halfOutter = light_outterangle * 0.5;
			vec3 lightFwd = normalize((invert_view_matrix * vec4(light_dir, 0.0)).xyz);
			float theta = acos(clamp(dot(lightFwd, -L), -1.0, 1.0));
			if (theta >= halfOutter)
				attenuation = 0.0;
			else if (theta > halfInner)
				attenuation *= (halfOutter - theta) / (halfOutter - halfInner);
		}
	}

	float NdotL = max(0.0, dot(N, L));

#ifdef PBR
	float rough = roughness_factor >= 0.0
		? roughness_factor
		: clamp(pow(2.0 / (shininess + 2.0), 0.25), 0.0, 1.0);
	float metallic = metallic_factor;

	// GGX (Cook-Torrance) — same form as PointLight.glsl's PBR
	// block (light_color arrives premultiplied by 1/pi).
	float a = max(rough * rough, 1e-3);
	float a2 = a * a;

	vec3 V = normalize(-vs_pos);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);
	vec3 H = normalize(L + V);
	float NdotH = max(0.0, dot(N, H));
	float NdotV = max(1e-4, dot(N, V));
	float VdotH = max(0.0, dot(V, H));

	float ndfDenom = NdotH * NdotH * (a2 - 1.0) + 1.0;
	float D = a2 / (ndfDenom * ndfDenom);

	float k = (a + 1.0) * (a + 1.0) / 8.0;
	float visL = NdotL / (NdotL * (1.0 - k) + k);
	float visV = NdotV / (NdotV * (1.0 - k) + k);

	vec3 F = F0 + (vec3(1.0) - F0) * pow(1.0 - VdotH, 5.0);

	vec3 spec = F * (D * visL * visV) / max(4.0 * NdotL * NdotV, 1e-3);
	vec3 diffuse = albedo * (1.0 - metallic);

	// Glass blend: the additive pass blends ONE/ONE, so the shader
	// premultiplies diffuse by alpha but leaves specular untouched —
	// a fully transmissive surface (alpha 0) still shows highlights.
	vec3 radiance = light_color * NdotL * attenuation * light_intensity * (diffuse * alpha + spec);
#else
	vec3 radiance = albedo * light_color * NdotL * attenuation * light_intensity * alpha;
#endif

#ifdef SHADOW
	// Multiply THIS light's contribution by its shadow factor
	// (ambient/emissive live in the u_light_type == 0 base pass).
	// Compares mirror the deferred PointLight/SunLight/SpotLight
	// shaders; bias lives in the caster polygon offset (dir/spot)
	// or the slope/distance-scaled term (point).
	if (u_shadow_type == 1)
	{
		vec3 worldPos = (shadow_matrix * vec4(vs_pos, 1.0)).xyz;
		vec3 dir = worldPos - light_pos;
		float current = length(dir);
		// Outside the light's radius there's no shadow info (and no
		// light influence) — count as lit.
		if (current <= light_radius)
		{
			float closest = texture(shadow_buffer, dir).x * light_radius;
			vec3 worldN = normalize((shadow_matrix * vec4(out_normal, 0.0)).xyz);
			float ndl = max(dot(worldN, -normalize(dir)), 0.0);
			float bias = 0.002 * (light_radius / 10.0)
			           + (light_radius / 256.0) * 4.0 * (1.0 - ndl);
			radiance *= float(current - bias < closest);
		}
	}
	else if (u_shadow_type == 2 || u_shadow_type == 4)
	{
		vec4 sc = shadow_matrix * vec4(vs_pos, 1.0);
		sc = sc / sc.w;
		float f = sc.z > 1.0 ? 1.0 : float(sc.z < texture(shadow_map, sc.xy).x);
		radiance *= f;
	}
	else if (u_shadow_type == 3)
	{
		// CSM: cascade pick on linear view depth; shadow_far holds
		// NEGATIVE split thresholds, so `vz > shadow_far.i` walks
		// near -> far (the deferred shader's sign is flipped because
		// it compares projected clip-z instead).
		float vz = vs_pos.z;
		int index = 3;
		if (vz > shadow_far.x) index = 0;
		else if (vz > shadow_far.y) index = 1;
		else if (vz > shadow_far.z) index = 2;
		vec4 sc = shadow_matrix_csm[index] * vec4(vs_pos, 1.0);
		sc = sc / sc.w;
		float f = sc.z > 1.0 ? 1.0 : float(sc.z < texture(shadow_buffer_csm, vec3(sc.xy, float(index))).x);
		radiance *= f;
	}
#endif

	fragment_output = vec4(radiance, alpha);
}

#endif
