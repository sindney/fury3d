#version 330

#include "ClusterCommon.glsl"
#include "GBufferCommon.glsl"

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
in vec2 out_uv;
out vec4 fragment_output;
uniform sampler2D gbuffer_depth;
uniform vec2 u_cluster_viewport;
uniform vec2 u_cluster_near_far;

void main()
{
	float depth = texture(gbuffer_depth, out_uv).r;
	if (GBufferIsFarDepth(depth))
	{
		fragment_output = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	vec2 pixel = out_uv * u_cluster_viewport;
	uvec3 coord = ClusterCoordFromLinearDepth(pixel, depth, u_cluster_viewport, u_cluster_near_far);
	float slice = float(coord.z) / float(CLUSTER_SLICES_Z - 1);
	vec3 color = mix(vec3(0.08, 0.35, 0.85), vec3(0.9, 0.35, 0.08), slice);
	vec2 tile = pixel / u_cluster_viewport * vec2(CLUSTER_TILES_X, CLUSTER_TILES_Y);
	vec2 edge = min(fract(tile), 1.0 - fract(tile));
	float line = step(min(edge.x, edge.y), 0.025);
	fragment_output = vec4(mix(color, vec3(1.0), line), 1.0);
}
#endif
