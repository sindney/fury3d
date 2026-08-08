#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;

out vec3 vs_dir;
out vec3 vs_pos;
out vec4 ss_pos;

uniform float camera_far = 10000;

uniform vec3 light_dir;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;

void main()
{
	vs_dir = normalize(invert_view_matrix * vec4(-light_dir, 0)).xyz;
	vs_pos = (inverse(projection_matrix) * vec4(vertex_position.xy, 1.0, 1.0) * camera_far).xyz;
	ss_pos = vec4(vertex_position.xyz, 1.0);
	gl_Position = ss_pos;
}

#endif

#ifdef FRAGMENT_SHADER

out vec4 fragment_output;

in vec3 vs_dir;
in vec3 vs_pos;
in vec4 ss_pos;

uniform vec3 light_color;
uniform float light_intensity;

// linear depth
uniform sampler2D gbuffer_depth;
// normal (view space *0.5+0.5); PBR: roughness in alpha
uniform sampler2D gbuffer_normal;

#ifdef PBR
// albedo rgb, metallic in alpha. Read at light time so the BRDF can
// tint F0 and attenuate diffuse per-pixel — the HDR pipeline feeds
// gbuffer_diffuse into the light pass for exactly this.
uniform sampler2D gbuffer_diffuse;
#endif

#ifdef CSM

uniform mat4 projection_matrix;
uniform mat4 shadow_matrix[4];
uniform sampler2DArray shadow_buffer;

uniform float bias = 0.002;
uniform vec4 shadow_far;

#endif

#ifdef SHADOW

uniform sampler2D shadow_buffer;
uniform mat4 shadow_matrix;

#endif

vec3 pos_from_depth(const in vec2 screenUV)
{
	float depth = texture(gbuffer_depth, screenUV).r;
	vec3 view_ray = vs_pos.xyz;
	return view_ray * depth;
}

vec4 apply_lighting(const in vec3 normal, const in vec3 surface_pos)
{
	vec3 L = normalize(vs_dir);
	vec3 N = normalize(normal);

	float NdotL = max(0.0, dot(N, L));

	return vec4(
		vec3(1) * light_color * NdotL * light_intensity, 1.0
	);
}

#ifdef PBR
// GGX (Cook-Torrance) BRDF, metallic workflow. light_color arrives
// premultiplied by 1/pi (Shader::BindLight), so the NDF below omits
// its own 1/pi factor — units match the diffuse term and the
// Lambert pipeline. GGX's long NDF tail + Smith visibility give the
// elongated grazing-angle highlights Blinn-Phong can't produce
// (its (N·H)^n falloff is radially symmetric → circular dots).
vec4 apply_lighting_pbr(const in vec3 normal, const in vec3 surface_pos,
	const in vec3 albedo, const in float metallic, const in float roughness)
{
	vec3 L = normalize(vs_dir);
	vec3 N = normalize(normal);
	vec3 V = normalize(-surface_pos); // view space: camera at origin

	float NdotL = max(0.0, dot(N, L));

	// a = r^2 (Disney reparam — perceptually linear roughness). The
	// gbuffer stores perceptual roughness directly (legacy shininess
	// maps through r = (2/(n+2))^0.25, tuned to match the old
	// Blinn-Phong exponent — close, not an exact round-trip).
	float a = max(roughness * roughness, 1e-3);
	float a2 = a * a;

	vec3 F0 = mix(vec3(0.04), albedo, metallic);
	vec3 H = normalize(L + V);
	float NdotH = max(0.0, dot(N, H));
	float NdotV = max(1e-4, dot(N, V));
	float VdotH = max(0.0, dot(V, H));

	// Trowbridge-Reitz NDF (pi-less — see header).
	float ndfDenom = NdotH * NdotH * (a2 - 1.0) + 1.0;
	float D = a2 / (ndfDenom * ndfDenom);

	// Smith height-correlated visibility, Schlick-GGX k = (a+1)^2/8.
	float k = (a + 1.0) * (a + 1.0) / 8.0;
	float visL = NdotL / (NdotL * (1.0 - k) + k);
	float visV = NdotV / (NdotV * (1.0 - k) + k);

	// Schlick Fresnel.
	vec3 F = F0 + (vec3(1.0) - F0) * pow(1.0 - VdotH, 5.0);

	vec3 spec = F * (D * visL * visV) / max(4.0 * NdotL * NdotV, 1e-3);

	vec3 diffuse = albedo * (1.0 - metallic);

	return vec4(
		light_color * NdotL * light_intensity * (diffuse + spec), 1.0
	);
}
#endif

void main()
{
	vec2 screenUV = ss_pos.xy * 0.5 + 0.5;

#ifdef PBR
	// Sky mask: cleared far-depth pixels have no geometry — emit
	// black so the additive accumulate leaves the ambient floor
	// (PbrCombine) as the only contribution. Without this the sun
	// would light the sky (normals there are clear-color garbage).
	if (texture(gbuffer_depth, screenUV).r >= 1.0)
	{
		fragment_output = vec4(0.0);
		return;
	}
#endif

	vec3 vs_surface_pos = pos_from_depth(screenUV);

	vec4 raw_normal = texture(gbuffer_normal, screenUV);
	vec3 vs_normal = raw_normal.xyz * 2.0 - 1.0;

#ifdef PBR
	vec4 diffuse = texture(gbuffer_diffuse, screenUV);
	fragment_output = apply_lighting_pbr(vs_normal, vs_surface_pos,
		diffuse.rgb, diffuse.a, raw_normal.a);
#else
	fragment_output = apply_lighting(vs_normal, vs_surface_pos);
#endif

#ifdef CSM
	
	vec4 pos = projection_matrix * vec4(vs_surface_pos, 1.0);

	int index = 3;
	if (pos.z < shadow_far.x)
		index = 0;
	else if(pos.z < shadow_far.y)
		index = 1;
	else if(pos.z < shadow_far.z)
		index = 2;

	vec4 shadowCoord = shadow_matrix[index] * vec4(vs_surface_pos, 1.0);
	shadowCoord = shadowCoord / shadowCoord.w;
	vec3 crood = vec3(shadowCoord.x, shadowCoord.y, float(index));
	fragment_output *= shadowCoord.z > 1.0 ? 1.0 : float(shadowCoord.z < texture(shadow_buffer, crood).x);

#endif

#ifdef SHADOW

	vec4 shadowCoord = shadow_matrix * vec4(vs_surface_pos, 1.0);
	shadowCoord = shadowCoord / shadowCoord.w;
	fragment_output *= shadowCoord.z > 1.0 ? 1.0 : float(shadowCoord.z < texture(shadow_buffer, shadowCoord.xy).x);

#endif
}

#endif