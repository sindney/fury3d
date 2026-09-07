// VegetationCommon.glsl -- shared wind + billboard helpers for the
// vegetation shader variants (WIND / BILLBOARD). Pure functions only;
// the including shader declares the uniforms and guards with defines.
//
// Kraut vertex_color packing (COLOR_0, see kraut-tree-import spec):
//   R = branch sway weight (0 trunk base .. 1 tips)
//   G = leaf flutter weight
//   B = per-branch phase (0..1)
//   A = color variation (unused by wind)
//
// u_wind_params = (dirX, dirZ, strength_cm, frequency)

// Wind sway offset for a vertex, applied to world position pre-projection
// (shadow depth variants call this too so shadows sway with the mesh).
//
// The phase combines a per-branch jitter (weights.b) with a position-based
// traveling wave (worldPos . windDir): across a field, neighboring plants
// share the wave phase and the sway reads as rolling wind fronts instead
// of per-plant white noise. Wavelength ~800 cm.
vec3 VegetationWindOffset(vec4 weights, vec3 worldPos, float time, vec4 windParams)
{
	float strength = windParams.z;
	if (strength <= 0.0 || (weights.r <= 0.0 && weights.g <= 0.0))
		return vec3(0.0);

	float wavePhase = dot(worldPos.xz, windParams.xy) * 0.00785;
	float phase = weights.b * 6.2831853 + time * windParams.w + wavePhase;
	float sway = weights.r * strength;
	vec3 offset = vec3(windParams.x, 0.0, windParams.y) * (sway * sin(phase));
	offset.y += sway * 0.25 * sin(phase * 0.63 + 1.3);

	float flutter = weights.g * strength * 0.15;
	offset += flutter * vec3(
		sin(phase * 2.9 + worldPos.x * 0.35),
		sin(phase * 3.4 + worldPos.y * 0.29 + 0.7),
		cos(phase * 3.1 + worldPos.z * 0.31));
	return offset;
}

// Cylindrical billboard: rotate the unit quad to face the camera around
// the +Y axis, and select the atlas cell for the current view azimuth.
// Quad local space: x in [-0.5, 0.5] across, y in [0, 1] up, z = 0.
// atlasInfo = (cols, rows); rows is currently always 1 (cylindrical).
// Cell k is rendered from azimuth ((k + 0.5) / cols - 0.5) * 2*pi around
// the node's +Z axis (contract with the Kraut atlas baker).
//
// upBias bends the SHADING normal from camera-facing toward +Y:
// normal = normalize(mix(fwd, up, upBias)). A pure camera-facing normal
// collapses N.L under overhead sun (billboards ~10x darker than the mesh
// tiers); a pure up normal overshoots ~3x at noon and dies against axial
// sun. The mesh canopy (two-sided leaf cards, isotropic normals) responds
// ~constant per sun elevation, so no fixed normal matches it at every
// azimuth -- the empirically tuned default (0.28, see 10.3 in
// openspec/changes/add-kraut-vegetation/tasks.md) lands the billboard
// within ~0% of the deepest mesh tier for overhead + front sun and
// ~-50% for perpendicular side sun (inherent: a quad has no side leaves).
// Geometry and UVs are unaffected -- the quad still faces the camera.
void VegetationBillboard(mat4 worldMat, vec3 cameraPos, vec3 quadPos, vec2 quadUV, vec2 atlasInfo,
	float upBias,
	out vec3 worldPos, out vec3 worldNormal, out vec2 uv)
{
	vec3 origin = (worldMat * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	float sx = length(worldMat[0].xyz);
	float sy = length(worldMat[1].xyz);

	vec3 toCam = cameraPos - origin;
	toCam.y = 0.0;
	float dist = max(length(toCam), 1e-4);
	vec3 fwd = toCam / dist;
	vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), fwd));

	worldPos = origin + right * (quadPos.x * sx) + vec3(0.0, 1.0, 0.0) * (quadPos.y * sy);
	worldNormal = normalize(mix(fwd, vec3(0.0, 1.0, 0.0), clamp(upBias, 0.0, 1.0)));

	vec3 nodeFwd = normalize((worldMat * vec4(0.0, 0.0, 1.0, 0.0)).xyz);
	nodeFwd.y = 0.0;
	nodeFwd = normalize(nodeFwd + vec3(1e-6, 0.0, 0.0));
	vec3 nodeRight = normalize(cross(vec3(0.0, 1.0, 0.0), nodeFwd));
	float angle = atan(dot(fwd, nodeRight), dot(fwd, nodeFwd)); // [-pi, pi]
	float cols = max(atlasInfo.x, 1.0);
	float cell = floor(fract(angle / 6.2831853 + 0.5) * cols);   // [0, cols)
	uv = vec2((quadUV.x + cell) / cols, quadUV.y);
}
