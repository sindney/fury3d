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

in vec2 out_uv;

out vec4 fragment_output;

uniform sampler2D scene;
uniform sampler2D gbuffer_depth;   // linear view depth / camera_far
uniform sampler2D gbuffer_normal;  // view normal *0.5+0.5, roughness in a (HDR)

uniform mat4 projection_matrix;
uniform float camera_far = 10000;

// Float uniforms (effect JSON binds float1-4); steps truncates to int.
uniform float u_steps = 48.0;
uniform float u_thickness = 25.0;
uniform float u_max_distance = 1000.0;
uniform float u_strength = 1.0;
// LDR fallback: the Lambert gbuffer has no roughness channel
// (normal.a = 1.0), so with the LDR define the effect treats every
// surface as this roughness instead of skipping entirely.
uniform float u_ldr_roughness = 0.35;

// 1 = encode sRGB on write (final chain effect only); see ACES.glsl.
uniform int u_gamma_correct = 0;

vec3 encode_srgb(vec3 c)
{
	c.r = (c.r <= 0.0031308) ? 12.92 * c.r : 1.055 * pow(c.r, 1.0 / 2.4) - 0.055;
	c.g = (c.g <= 0.0031308) ? 12.92 * c.g : 1.055 * pow(c.g, 1.0 / 2.4) - 0.055;
	c.b = (c.b <= 0.0031308) ? 12.92 * c.b : 1.055 * pow(c.b, 1.0 / 2.4) - 0.055;
	return c;
}

vec3 view_pos_from_depth(vec2 uv, float depth)
{
	float viewZ = -depth * camera_far;
	vec2 ndc = uv * 2.0 - 1.0;
	return vec3(ndc.x * -viewZ / projection_matrix[0][0],
				ndc.y * -viewZ / projection_matrix[1][1],
				viewZ);
}

void main()
{
	vec3 col = texture(scene, out_uv).rgb;
	float depth = texture(gbuffer_depth, out_uv).r;
	vec4 nrm = texture(gbuffer_normal, out_uv);

#ifdef LDR
	float roughness = u_ldr_roughness;
#else
	float roughness = nrm.a;
#endif

	// Reflection weight + color; w = 0 means passthrough.
	float w = 0.0;
	vec3 reflected = vec3(0.0);

	if (depth < 1.0 && roughness < 0.95)
	{
		vec3 pos = view_pos_from_depth(out_uv, depth);
		vec3 normal = normalize(nrm.xyz * 2.0 - 1.0);
		vec3 viewDir = normalize(pos);
		vec3 rayDir = normalize(reflect(viewDir, normal));

		// March in view space. dz = p.z - sceneZ is POSITIVE while the
		// ray flies in front of a surface and flips NEGATIVE once it
		// crosses behind — the hit is that sign crossing, linearly
		// interpolated between the two bracketing samples. (The old
		// loop instead required landing within u_thickness IN FRONT of
		// the surface AND broke out the moment it flew over open
		// space — on meter-scale scenes it died within 1-2 steps and
		// never hit anything.)
		int steps = clamp(int(u_steps + 0.5), 1, 96);
		float stepLen = u_max_distance / float(steps);
		float t = stepLen * 0.5;
		bool hit = false;
		vec2 hitUV = out_uv;
		float hitT = t;
		vec2 prevUV = out_uv;
		float prevDz = 0.0;

		for (int i = 0; i < 96; ++i)
		{
			if (i >= steps) break;

			vec3 p = pos + rayDir * t;
			if (p.z > -0.1) break;            // behind the camera
			if (-p.z > camera_far) break;

			vec4 clip = projection_matrix * vec4(p, 1.0);
			vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
			if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
				break;

			float sceneZ = -texture(gbuffer_depth, uv).r * camera_far;
			float dz = p.z - sceneZ;

			if (dz < 0.0)
			{
				// Crossed behind a surface. Accept the hit only when
				// the depth gap at the crossing is within the gap
				// limit — larger gaps mean the ray flew past a
				// silhouette into open background (reject, no halo).
				// The limit floors at one ray step so surfaces
				// thinner than the step can't be skipped between
				// samples regardless of u_max_distance; u_thickness
				// then acts as the minimum registered thickness.
				float gapLimit = max(u_thickness, stepLen);
				float gap = (i == 0) ? -dz : max(prevDz, -dz);
				if (gap <= gapLimit)
				{
					float f = (i == 0) ? 0.5 : prevDz / max(prevDz - dz, 1e-5);
					hitUV = mix(prevUV, uv, clamp(f, 0.0, 1.0));
					hitT = t - stepLen * (1.0 - f);
					hit = true;
				}
				break;
			}

			prevDz = dz;
			prevUV = uv;
			t += stepLen;
		}

		// Reject hits on sky (depth=1): the ray left the scene —
		// blending in the black sky just darkens everything.
		if (hit && texture(gbuffer_depth, hitUV).r < 1.0)
		{
			reflected = texture(scene, hitUV).rgb;

			// Fades: roughness, screen-edge, ray distance. The
			// roughness fade ends at 0.5 so mid-rough surfaces don't
			// pick up black-sky haze.
			float fresnel = pow(1.0 - max(0.0, dot(-viewDir, normal)), 5.0);
			float roughFade = 1.0 - smoothstep(0.0, 0.5, roughness);
			vec2 edge = min(hitUV, 1.0 - hitUV);
			float edgeFade = smoothstep(0.0, 0.05, min(edge.x, edge.y));
			float distFade = 1.0 - clamp(hitT / u_max_distance, 0.0, 1.0);

			w = clamp((0.3 + 0.7 * fresnel) * roughFade * edgeFade * distFade * u_strength, 0.0, 1.0);
			col = mix(col, reflected, w);
		}
	}

#ifdef DEBUG_VIEW
	// Debug view: the composited reflection contribution
	// (reflected * weight). Black = SSR contributes nothing here.
	fragment_output = vec4(reflected * w, 1.0);
	return;
#endif

	if (u_gamma_correct != 0)
		col = encode_srgb(col);

	fragment_output = vec4(col, 1.0);
}

#endif
