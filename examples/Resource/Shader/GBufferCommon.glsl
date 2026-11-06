vec4 GBufferPackDiffuse(vec3 color, float metallic)
{
	return vec4(color, metallic);
}

vec3 GBufferDecodeDiffuse(vec4 packed)
{
	return packed.rgb;
}

float GBufferDecodeMetallic(vec4 packed)
{
	return packed.a;
}

vec2 GBufferOctEncode(vec3 normal)
{
	float l1 = dot(abs(normal), vec3(1.0));
	vec3 n = normal / max(l1, 1e-8);
	vec2 encoded = n.xy;
	if (n.z < 0.0)
	{
		vec2 signNotZero = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
		encoded = (1.0 - abs(n.yx)) * signNotZero;
	}
	return encoded * 0.5 + 0.5;
}

vec3 GBufferOctDecode(vec2 packed)
{
	vec2 encoded = packed * 2.0 - 1.0;
	vec3 n = vec3(encoded, 1.0 - abs(encoded.x) - abs(encoded.y));
	float t = max(-n.z, 0.0);
	n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
	return normalize(n);
}

vec4 GBufferPackNormal(vec3 normal, float roughness)
{
	return vec4(GBufferOctEncode(normal), roughness, 1.0);
}

vec3 GBufferDecodeNormal(vec4 packed)
{
	return GBufferOctDecode(packed.rg);
}

float GBufferDecodeRoughness(vec4 packed)
{
	return packed.b;
}

float GBufferLinearDepth(float viewDepth, float cameraFar)
{
	return viewDepth / cameraFar;
}

bool GBufferIsFarDepth(float depth)
{
	return depth >= 1.0;
}
