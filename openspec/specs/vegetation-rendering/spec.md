# vegetation-rendering

## Purpose

Foliage-correct shading for Kraut trees (and any alpha-cut vegetation): two-sided lighting on cutout materials, leaf-shaped shadows via alpha-tested depth shaders, wind vertex sway driven by the mesh's vertex-color channel, and camera-facing billboard rendering for the terminal LOD tier.
## Requirements
### Requirement: Materials SHALL support two-sided lighting

`Material` SHALL gain a serialized `m_TwoSided` flag (editor-exposed checkbox). When set, draws of that material SHALL disable backface culling for that unit, and the shader SHALL receive a `TWO_SIDED` define that flips the shading normal for back faces (via `gl_FrontFacing`) so leaf cards light correctly from both sides. Materials without the flag SHALL render exactly as before.

#### Scenario: Leaf card lit from behind

- **WHEN** a MASK material with `TwoSided` enabled is viewed from the back face against a light
- **THEN** the back face renders with a flipped normal and correct diffuse/specular response
- **AND** no faces are culled

#### Scenario: Default materials unchanged

- **WHEN** a material has `TwoSided` disabled
- **THEN** culling and normals behave exactly as before this change

### Requirement: Shadow depth shaders SHALL alpha-test MASK materials

The depth shaders (`DrawDepth.glsl` for dir/spot/CSM, `DrawDepthCube.glsl` for point lights) SHALL gain `ALPHA_TEST` variants binding the material's diffuse texture and `u_alpha_cutoff`, discarding cutout fragments. Shadow pass draw submission SHALL select the alpha-tested depth variant for MASK materials the same way the gbuffer pass selects its variants.

#### Scenario: Leaves cast leaf-shaped shadows

- **WHEN** an alpha-cutout leaf card casts into a spot shadow map
- **THEN** the shadow contains the leaf silhouette, not the full quad

#### Scenario: Opaque casters unchanged

- **WHEN** an OPAQUE material casts a shadow
- **THEN** the shadow pass uses the existing non-alpha-tested depth shader

### Requirement: Wind sway SHALL animate vegetation vertices from vertex-color weights

The gbuffer and depth vertex shaders SHALL gain a `WIND` variant that displaces vertices pre-projection (so shadows sway with the visible mesh) using the `vertex_color` channels as Kraut wind weights (branch stiffness / leaf flutter / phase) and `u_time` + `u_wind_params` (direction, strength, frequency) uniforms. Materials SHALL opt in via a serialized `m_WindEnabled` flag. Meshes without a `Colors` channel SHALL behave as fully rigid when wind is enabled (zero weights).

#### Scenario: Canopy moves, trunk stays

- **WHEN** a wind-enabled tree animates over time
- **THEN** high-weight vertices (leaves/branch tips) displace periodically while near-zero-weight vertices (trunk base) stay fixed
- **AND** the shadow of the canopy sways in sync

#### Scenario: Wind disabled by default

- **WHEN** a material has `WindEnabled` unset
- **THEN** the shader compiled for it contains no wind code path

### Requirement: Billboard-flagged LOD tiers SHALL render as camera-facing quads with atlas selection

A `BILLBOARD` shader variant SHALL render a billboard-tier quad as a cylindrical camera-facing quad (upright axis preserved) built in the vertex shader from the node/instance position, sampling the billboard atlas cell that matches the current view angle around the trunk axis (grid dimensions from kraut extras / material uniforms). Billboards SHALL be alpha-tested, two-sided, and SHALL receive gbuffer lighting like any foliage. Billboard tiers SHALL be skipped in shadow passes (the deepest non-billboard LOD casts instead). Billboards SHALL work in both the plain `MeshRender` path and the instanced path.

#### Scenario: Billboard tracks the camera

- **WHEN** the camera orbits a distant tree whose active tier is the billboard tier
- **THEN** the rendered quad always faces the camera around its trunk axis
- **AND** the sampled atlas image changes to match the view angle

#### Scenario: No quad shadows from billboards

- **WHEN** a light covers only billboard-tier instances of a tree
- **THEN** the shadow map contains the deepest mesh-LOD silhouette, not camera-facing quads

#### Scenario: LOD debug view distinguishes billboards

- **WHEN** the LOD debug-color view is active on a scene with billboard tiers
- **THEN** billboard-tier instances show their own distinct debug tint

### Requirement: Vegetation materials SHALL support an alpha-tested depth prepass before the gbuffer pass

`Material` SHALL gain a serialized `m_PreZ` flag (editor-exposed, default off; the Kraut importer SHALL set it on imported foliage materials). When the prepass is enabled (project render setting `vegetation_prez`, default on, `FURY_VEG_PREZ` env override for tests), the gbuffer pass SHALL first draw all visible `PreZ`-flagged materials as a depth-only pre-phase (color writes off, alpha-tested, same WIND vertex displacement as the gbuffer draw) into its own depth attachment, then draw non-flagged materials unchanged (LESS + depth write), then flagged materials with depth test `EQUAL` and depth writes off. Non-flagged materials SHALL render exactly as before. Billboard LOD tiers SHALL be excluded from the prepass (single quads, negligible overdraw).

#### Scenario: Only the front foliage layer is shaded

- **WHEN** a foliage-dense view (island palm canopy) renders with the prepass on
- **THEN** the gbuffer pass shades each covered pixel's front-most foliage layer once (verifiable via the overdraw/step debug view and Tracy GPU timing showing reduced gbuffer-pass cost vs prepass off)

#### Scenario: Identical image with prepass on/off

- **WHEN** the same frame is screenshotted with `FURY_VEG_PREZ=1` and `FURY_VEG_PREZ=0`
- **THEN** the two images match (no missing foliage, no z-fighting, no halo artifacts)

#### Scenario: Wind-displaced leaves do not z-fight

- **WHEN** a wind-enabled tree animates with the prepass on
- **THEN** leaves render without speckle/flicker (prepass and gbuffer displace vertices identically, so `EQUAL` passes)

#### Scenario: Flag off = old path

- **WHEN** a MASK material has `PreZ` unset or the render setting is off
- **THEN** it draws only in the gbuffer pass with the previous depth state (`LESS` + writes on), pixel-identical to before this change

#### Scenario: Shadow passes unaffected

- **WHEN** the prepass is enabled
- **THEN** shadow-map draws use the existing alpha-tested depth variants unchanged (the prepass adds no shadow-pass cost)

