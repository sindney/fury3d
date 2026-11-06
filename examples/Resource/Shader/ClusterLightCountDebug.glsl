#version 430 core

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
layout(std430, binding = 1) readonly buffer ClusterCountBuffer { uint clusterCounts[]; };
uniform sampler2D gbuffer_depth;
uniform vec2 u_cluster_viewport;
uniform vec2 u_cluster_near_far;

vec3 ClusterCountColor(uint count)
{
	float t = clamp(float(count) / 16.0, 0.0, 1.0);
	if (t < 1.0 / 3.0) return mix(vec3(0.0, 0.1, 0.8), vec3(0.0, 0.8, 0.2), t * 3.0);
	if (t < 2.0 / 3.0) return mix(vec3(0.0, 0.8, 0.2), vec3(0.9, 0.9, 0.0), (t - 1.0 / 3.0) * 3.0);
	return mix(vec3(0.9, 0.9, 0.0), vec3(0.9, 0.0, 0.0), (t - 2.0 / 3.0) * 3.0);
}

void main()
{
	float depth = texture(gbuffer_depth, out_uv).r;
	if (GBufferIsFarDepth(depth))
	{
		fragment_output = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	vec2 pixel = out_uv * u_cluster_viewport;
	int cluster = ClusterIndexFromLinearDepth(pixel, depth, u_cluster_viewport, u_cluster_near_far);
	fragment_output = vec4(ClusterCountColor(clusterCounts[cluster]), 1.0);
}
#endif
