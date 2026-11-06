#version 430 core

#include "../GBufferCommon.glsl"
#include "../ClusterCommon.glsl"
#include "../ClusterLightCommon.glsl"
#include "LocalLightCommon.glsl"

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
layout(std430, binding = 0) readonly buffer ClusterLightBuffer { ClusterLight clusterLights[]; };
layout(std430, binding = 1) readonly buffer ClusterCountBuffer { uint clusterCounts[]; };
layout(std430, binding = 2) readonly buffer ClusterIndexBuffer { uint clusterLightIndices[]; };
uniform sampler2D gbuffer_depth;
uniform sampler2D gbuffer_normal;
uniform sampler2D gbuffer_diffuse;
uniform samplerCube u_point_shadow[6];
uniform sampler2D u_spot_shadow[6];
uniform mat4 u_spot_shadow_matrix[6];
uniform mat4 projection_matrix;
uniform mat4 camera_world;
uniform float camera_far;
uniform vec2 u_cluster_viewport;
uniform vec2 u_cluster_near_far;

vec3 ClusterViewPosition(vec2 uv, float depth)
{
	float viewZ = -depth * camera_far;
	vec2 ndc = uv * 2.0 - 1.0;
	return vec3(ndc.x * -viewZ / projection_matrix[0][0],
		ndc.y * -viewZ / projection_matrix[1][1], viewZ);
}

float ClusterPointShadow(int slot, mat4 worldMatrix, vec3 surfacePos,
	vec3 surfaceNormal, vec3 lightPos, float lightRadius)
{
	if (slot == 0) return LocalPointShadow(u_point_shadow[0], worldMatrix, surfacePos, surfaceNormal, lightPos, lightRadius);
	if (slot == 1) return LocalPointShadow(u_point_shadow[1], worldMatrix, surfacePos, surfaceNormal, lightPos, lightRadius);
	if (slot == 2) return LocalPointShadow(u_point_shadow[2], worldMatrix, surfacePos, surfaceNormal, lightPos, lightRadius);
	if (slot == 3) return LocalPointShadow(u_point_shadow[3], worldMatrix, surfacePos, surfaceNormal, lightPos, lightRadius);
	if (slot == 4) return LocalPointShadow(u_point_shadow[4], worldMatrix, surfacePos, surfaceNormal, lightPos, lightRadius);
	if (slot == 5) return LocalPointShadow(u_point_shadow[5], worldMatrix, surfacePos, surfaceNormal, lightPos, lightRadius);
	return LocalPointShadow(u_point_shadow[5], worldMatrix, surfacePos, surfaceNormal, lightPos, lightRadius);
}

float ClusterSpotShadow(int slot, vec3 surfacePos)
{
	if (slot == 0) return LocalSpotShadow(u_spot_shadow[0], u_spot_shadow_matrix[0], surfacePos);
	if (slot == 1) return LocalSpotShadow(u_spot_shadow[1], u_spot_shadow_matrix[1], surfacePos);
	if (slot == 2) return LocalSpotShadow(u_spot_shadow[2], u_spot_shadow_matrix[2], surfacePos);
	if (slot == 3) return LocalSpotShadow(u_spot_shadow[3], u_spot_shadow_matrix[3], surfacePos);
	if (slot == 4) return LocalSpotShadow(u_spot_shadow[4], u_spot_shadow_matrix[4], surfacePos);
	if (slot == 5) return LocalSpotShadow(u_spot_shadow[5], u_spot_shadow_matrix[5], surfacePos);
	return LocalSpotShadow(u_spot_shadow[5], u_spot_shadow_matrix[5], surfacePos);
}

void main()
{
	float depth = texture(gbuffer_depth, out_uv).r;
	if (GBufferIsFarDepth(depth))
	{
		fragment_output = vec4(0.0);
		return;
	}
	vec2 pixel = out_uv * u_cluster_viewport;
	int cluster = ClusterIndexFromLinearDepth(pixel, depth, u_cluster_viewport, u_cluster_near_far);
	uint count = min(clusterCounts[cluster], uint(CLUSTER_MAX_LIGHTS));
	if (count == 0u)
	{
		fragment_output = vec4(0.0);
		return;
	}

	vec4 packedNormal = texture(gbuffer_normal, out_uv);
	vec4 packedDiffuse = texture(gbuffer_diffuse, out_uv);
	vec3 surfacePos = ClusterViewPosition(out_uv, depth);
	vec3 N = normalize(GBufferDecodeNormal(packedNormal));
	vec3 V = normalize(-surfacePos);
	vec3 albedo = GBufferDecodeDiffuse(packedDiffuse);
	float metallic = GBufferDecodeMetallic(packedDiffuse);
	float roughness = GBufferDecodeRoughness(packedNormal);
	vec3 result = vec3(0.0);
	for (uint i = 0u; i < count; ++i)
	{
		ClusterLight light = clusterLights[clusterLightIndices[cluster * CLUSTER_MAX_LIGHTS + int(i)]];
		vec3 L = light.positionRange.xyz - surfacePos;
		float dist = length(L);
		float attenuation = LocalLightAttenuation(dist, light.positionRange.w, light.params.y);
		L = normalize(L);
		if (light.params.w >= 1.5)
		{
			float innerAngle = acos(clamp(light.params.x, -1.0, 1.0)) * 2.0;
			float outerAngle = acos(clamp(light.directionCosOuter.w, -1.0, 1.0)) * 2.0;
			attenuation *= LocalSpotAttenuation(normalize(-light.directionCosOuter.xyz), L, innerAngle, outerAngle);
		}
		float NdotL = max(0.0, dot(N, L));
		vec3 brdf = vec3(1.0);
#ifdef PBR
		brdf = LocalLightGGX(N, V, L, albedo, metallic, roughness);
#endif
		float shadow = 1.0;
		int shadowSlot = int(light.params.z);
		if (shadowSlot >= 0 && light.params.w < 1.5)
		{
			vec3 worldLightPos = (camera_world * vec4(light.positionRange.xyz, 1.0)).xyz;
			shadow = ClusterPointShadow(shadowSlot, camera_world, surfacePos, N, worldLightPos, light.positionRange.w);
		}
		else if (shadowSlot >= 0)
			shadow = ClusterSpotShadow(shadowSlot, surfacePos);
		result += light.colorIntensity.rgb * NdotL * attenuation * light.colorIntensity.w * brdf * shadow;
	}
	fragment_output = vec4(result, 1.0);
}
#endif
