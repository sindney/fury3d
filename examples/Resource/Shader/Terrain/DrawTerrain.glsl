#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec2 vertex_uv;
in vec3 vertex_normal;

out vec3 out_normal;
out vec2 out_uv;          // 0..1 across the whole terrain (splatmap lookup)
out vec3 out_world_pos;   // for per-layer world tiling
out float out_depth;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;
uniform mat4 world_matrix;

void main()
{
	vec4 worldPos = world_matrix * vec4(vertex_position, 1.0);
	out_normal = normalize(invert_view_matrix * world_matrix * vec4(vertex_normal, 0.0)).xyz;
	out_world_pos = worldPos.xyz;

	vec4 viewPos = invert_view_matrix * worldPos;
	out_depth = -viewPos.z;
	out_uv = vertex_uv;

	gl_Position = projection_matrix * viewPos;
}

#endif

#ifdef FRAGMENT_SHADER

in vec3 out_normal;
in vec2 out_uv;
in vec3 out_world_pos;
in float out_depth;

uniform float camera_far = 10000;

// splat weights: R grass, G rock, B mud, A snow (rows sum to 1)
uniform sampler2D u_splat_map;
// per-layer albedo (rgb) + roughness variation (a)
uniform sampler2D u_layer0;
uniform sampler2D u_layer1;
uniform sampler2D u_layer2;
uniform sampler2D u_layer3;
// per-layer tile world size in cm
uniform vec4 u_layer_tiling = vec4(800.0, 800.0, 800.0, 800.0);

#ifdef WITH_EDITOR
uniform vec4 lod_debug_color = vec4(0.0, 0.0, 0.0, 0.0);
#endif

layout (location = 0) out vec4 rt0;
layout (location = 1) out vec4 rt1;

void main()
{
	vec4 w = texture(u_splat_map, out_uv);
	float wsum = max(w.r + w.g + w.b + w.a, 1e-4);
	w /= wsum;

	vec2 tuv0 = out_world_pos.xz / u_layer_tiling.x;
	vec2 tuv1 = out_world_pos.xz / u_layer_tiling.y;
	vec2 tuv2 = out_world_pos.xz / u_layer_tiling.z;
	vec2 tuv3 = out_world_pos.xz / u_layer_tiling.w;

	vec4 l0 = texture(u_layer0, tuv0);
	vec4 l1 = texture(u_layer1, tuv1);
	vec4 l2 = texture(u_layer2, tuv2);
	vec4 l3 = texture(u_layer3, tuv3);

	vec3 albedo = l0.rgb * w.r + l1.rgb * w.g + l2.rgb * w.b + l3.rgb * w.a;
	float rough = clamp(l0.a * w.r + l1.a * w.g + l2.a * w.b + l3.a * w.a, 0.05, 1.0);

#ifdef WITH_EDITOR
	if (lod_debug_color.a > 0.0)
		albedo *= lod_debug_color.rgb;
#endif

	rt0.rgb = (out_normal + 1.0) * 0.5;
	rt0.a = rough;
	rt1.rgb = albedo;
	rt1.a = 0.0;   // metallic: terrain is dielectric

	gl_FragDepth = out_depth / camera_far;
}

#endif
