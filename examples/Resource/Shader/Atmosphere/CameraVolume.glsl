#version 330

#ifdef VERTEX_SHADER

in vec3 vertex_position;
in vec2 vertex_uv;

out vec2 out_uv;

void main()
{
	out_uv = vertex_uv;
	gl_Position = vec4(vertex_position.xy, 0.0, 1.0);
}

#endif

#ifdef FRAGMENT_SHADER

#include "AtmosphereCommon.glsl"

in vec2 out_uv;

out vec4 fragment_output;

uniform sampler2D u_transmittance_lut;
uniform sampler2D u_multiscatter_lut;

uniform mat4 projection_matrix;
uniform mat4 invert_view_matrix;   // world -> view (engine misnomer)
uniform float u_slice_id;          // [0, 31]
uniform float u_ap_range;          // total volume range, km

// One slice of the 96x54x32 aerial-perspective volume: inscatter (rgb) and
// mean transmittance (a) along the camera ray out to the slice's far edge.
// Sample count scales with slice id (reference RenderCameraVolumePS).
void main()
{
	vec2 ndc = out_uv * 2.0 - 1.0;
	vec4 viewPos = inverse(projection_matrix) * vec4(ndc, 1.0, 1.0);
	vec3 worldDir = normalize((inverse(invert_view_matrix) * vec4(normalize(viewPos.xyz), 0.0)).xyz);

	vec3 p = vec3(0.0, u_bottom_radius + u_view_height, 0.0);
	float tEnd = u_ap_range * pow((u_slice_id + 1.0) / 32.0, 2.0);
	float tGround = atm_ray_ground(p, worldDir);
	if (tGround > 0.0 && tGround < tEnd) tEnd = tGround;

	int samples = clamp(int((u_slice_id + 1.0) * 2.0), 2, 64);
	vec3 T;
	vec3 L = atm_march(u_transmittance_lut, u_multiscatter_lut, p, worldDir, tEnd, samples, T);

	float tMean = dot(T, vec3(0.2126, 0.7152, 0.0722));
	fragment_output = vec4(L, tMean);
}

#endif
