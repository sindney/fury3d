# instanced-static-mesh

## Purpose

ISM/HISM-style rendering of large static mesh sets (vegetation first): an `InstancedMeshRender` component holds one mesh plus N static transforms and draws visible instances with instanced draw calls — one per (pass, LOD tier) in hierarchical (HISM) mode — with per-instance frustum culling and instanced shadow casting. Instance transforms stream via SSBO on GL 4.3+ (preferred, untested) with an attribute-divisor VBO fallback on GL 3.3/4.1.

## Requirements

### Requirement: `InstancedMeshRender` SHALL hold a shared mesh and a static transform list

The component SHALL reference one `Mesh` (with optional LOD chain) plus per-submesh materials, and SHALL own an ordered list of instance transforms (TRS). The component, its mesh reference, materials, and full instance list SHALL serialize with the scene (JSON and `.bin`). Adding/removing/editing instances SHALL be possible from the editor inspector.

#### Scenario: Round-trip a component with 500 instances

- **WHEN** a scene containing an `InstancedMeshRender` with 500 instance transforms is saved and reloaded
- **THEN** the reloaded component has the same mesh, materials, and 500 identical transforms

### Requirement: Rendering SHALL use instanced draw calls with a capability-selected instance stream

Visible instances SHALL be drawn via `glDrawElementsInstanced`, one draw per (pass, LOD tier). Per-instance world matrices SHALL stream to the GPU through one of two paths selected once by runtime GL capability:

- **SSBO path (preferred)**: on GL 4.3+ contexts, matrices are stored in a shader storage buffer read by `gl_InstanceID` in the vertex shader. This path is the preferred implementation but is UNTESTED at spec time — it MUST be validated on a GL 4.3+ target before being trusted.
- **Divisor-VBO path (fallback)**: on GL 3.3/4.1 contexts (e.g. macOS), matrices stream through a dynamic VBO bound with `glVertexAttribDivisor` (mat4 as four vec4 attributes).

Both paths SHALL produce identical rendered output for the same component and camera. Shaders SHALL select the instance matrix via an `INSTANCED` define (with `INSTANCE_SSBO` selecting the read mechanism) instead of the `u_world` uniform. The existing non-instanced `DrawUnit` path SHALL be unchanged when no instanced components are present (instanced units live in a separate `RenderQuery` list).

#### Scenario: One draw per pass per LOD tier

- **WHEN** an HISM component has 1000 visible instances bucketing into LOD tiers 0 and 1
- **THEN** the gbuffer pass issues exactly 2 instanced draws for that component (500-instance and 500-instance streams, or whatever the bucket sizes are)
- **AND** no per-instance `glDrawElements` calls occur

#### Scenario: Capability selects the stream path

- **WHEN** the engine runs on a GL 4.3+ context
- **THEN** instanced draws read instance matrices from the SSBO path
- **AND** on a GL 4.1 macOS context the same scene uses the divisor-VBO path
- **AND** both render identically

#### Scenario: No instanced components, no behavior change

- **WHEN** a scene contains no `InstancedMeshRender` components
- **THEN** the render path issues draws exactly as before this change

### Requirement: Instances SHALL be frustum-culled individually and LOD-bucketed in HISM mode

Each frame, the component SHALL frustum-test every instance's cached world AABB (the component holds one coarse octree entry covering all instances). In HISM mode (default) each visible instance SHALL be assigned a LOD tier by the same screen-coverage rule `MeshRender` uses, evaluated per instance, and instances SHALL be grouped into per-tier streams. In ISM mode all visible instances SHALL draw at one tier picked from the component's aggregate bounds. Billboard-flagged tiers SHALL participate as their own bucket with the billboard shader.

#### Scenario: Far instances use deep tiers, near instances use shallow tiers

- **WHEN** an HISM tree component has instances at 5 m and at 200 m from the camera and the mesh has tiers `{0, 1, 2, billboard}`
- **THEN** the near instances appear in the LOD0 bucket and the far instances in the billboard bucket in the same frame

#### Scenario: Off-screen instances cost nothing

- **WHEN** the camera faces away from half the instances
- **THEN** those instances are absent from every per-tier instance stream

### Requirement: Instanced casts SHALL participate in shadow passes

Shadow passes (dir/CSM, spot, point-cube) SHALL draw instanced casters with instanced draws using each pass's existing light matrices and the depth shaders' `INSTANCED` variants, honoring a per-component `cast_shadows` flag. Shadow draws SHALL use the deepest non-billboard LOD tier rather than LOD 0.

#### Scenario: Forest casts shadows at shadow LOD

- **WHEN** a directional light covers an instanced forest with `cast_shadows` enabled
- **THEN** the shadow map contains silhouettes for the instances, drawn from the deepest non-billboard tier, via instanced draws

#### Scenario: Shadows disabled per component

- **WHEN** a component's `cast_shadows` is false
- **THEN** no shadow-pass draw is issued for it

### Requirement: The editor SHALL expose the component

The inspector SHALL show: mesh/material slots, ISM/HISM mode toggle, instance count, add/remove/duplicate instance controls, and a scatter helper that distributes instances over a terrain/area with a seed. The LOD debug-color view SHALL visualize per-instance tier assignment.

#### Scenario: Scatter 200 trees on the island

- **WHEN** the user invokes the scatter helper with count 200 and seed 7 over the ocean_island terrain
- **THEN** the component gains 200 instances placed on the terrain surface
- **AND** re-running with the same seed reproduces identical placement
