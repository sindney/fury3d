float LocalLightAttenuation(float distance, float radius, float falloff)
{
	return pow(max(0.0, 1.0 - distance / radius), falloff + 1.0);
}

float LocalSpotAttenuation(vec3 lightDir, vec3 L, float innerAngle, float outerAngle)
{
	float halfInner = innerAngle * 0.5;
	float halfOuter = outerAngle * 0.5;
	float theta = acos(dot(lightDir, L));
	if (theta < halfInner) return 1.0;
	if (theta < halfOuter) return (halfOuter - theta) / (halfOuter - halfInner);
	return 0.0;
}

vec3 LocalLightGGX(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness)
{
	float NdotL = max(0.0, dot(N, L));
	float a = max(roughness * roughness, 1e-3);
	float a2 = a * a;
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
	return diffuse + spec;
}

float LocalPointShadow(samplerCube shadowBuffer, mat4 shadowMatrix, vec3 surfacePos,
	vec3 surfaceNormal, vec3 lightPos, float lightRadius)
{
	vec4 pos = shadowMatrix * vec4(surfacePos, 1.0);
	vec3 dir = pos.xyz - lightPos;
	float closest = texture(shadowBuffer, dir).x * lightRadius;
	float current = length(dir);
	vec3 worldN = normalize((shadowMatrix * vec4(surfaceNormal, 0.0)).xyz);
	float ndl = max(dot(worldN, -normalize(dir)), 0.0);
	float bias = 0.002 * (lightRadius / 10.0)
		+ (lightRadius / 256.0) * 4.0 * (1.0 - ndl);
	return float(current - bias < closest);
}

float LocalSpotShadow(sampler2D shadowBuffer, mat4 shadowMatrix, vec3 surfacePos)
{
	vec4 shadowCoord = shadowMatrix * vec4(surfacePos, 1.0);
	shadowCoord = shadowCoord / shadowCoord.w;
	return float(shadowCoord.z < texture(shadowBuffer, shadowCoord.xy).x);
}
