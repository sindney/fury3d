#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec3 vertex_normal;

#ifdef SKINNED_MESH
in ivec4 bone_ids;
in vec3 bone_weights;
uniform mat4 bone_matrices[35];
#endif

out vec3 out_normal;
out float out_depth;

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
	out_depth = -viewPos.z;
	
	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 out_normal;
in float out_depth;

uniform float camera_far = 10000;

uniform vec3 ambient_color;
uniform vec3 diffuse_color;

uniform float ambient_factor = 1;
uniform float diffuse_factor = 1;

// Color-only path: alpha-test still gates discard against (1 - transparency). See GBuffer.glsl for variable semantics.
uniform float transparency = 0.0;
#ifdef ALPHA_TEST
uniform float u_alpha_cutoff = 0.5;
#endif

#ifdef PBR
// See GBuffer.glsl — same metallic-roughness packing for
// color-only materials.
uniform float metallic_factor = 0.0;
uniform float roughness_factor = -1.0;
uniform float shininess = 32.0;
#endif

#ifdef WITH_EDITOR
// Editor LOD-debug tint. See GBuffer.glsl.
uniform vec4 lod_debug_color = vec4(0.0, 0.0, 0.0, 0.0);
#endif

// normal.xyz, (PBR: roughness)
layout (location = 0) out vec4 rt0;
// diffuse rgb, (PBR: metallic)
layout (location = 1) out vec4 rt1;

void main()
{
#ifdef ALPHA_TEST
	if ((1.0 - transparency) < u_alpha_cutoff)
		discard;
#endif

	rt0.rgb = (out_normal.rgb + 1) * 0.5;

	vec3 finalDiffuse = diffuse_color.rgb * diffuse_factor + ambient_color * ambient_factor;
#ifdef WITH_EDITOR
	if (lod_debug_color.a > 0.0)
		finalDiffuse *= lod_debug_color.rgb;
#endif
	rt1.rgb = finalDiffuse;

#ifdef PBR
	float rough = roughness_factor >= 0.0
		? roughness_factor
		: clamp(pow(2.0 / (shininess + 2.0), 0.25), 0.0, 1.0);
	rt0.a = rough;
	rt1.a = metallic_factor;
#else
	rt0.a = 1.0;
	rt1.a = 1.0;
#endif

	gl_FragDepth = out_depth / camera_far;
}

#endif