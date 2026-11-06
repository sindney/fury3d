# clustered-deferred-lighting

## ADDED Requirements

### Requirement: Clustered lighting SHALL be optional and capability-gated

The deferred renderer SHALL retain the existing per-light accumulation path and SHALL use clustered local-light shading only when the user enables it and the active backend reports all required compute, storage-buffer, barrier, and shader-compilation capabilities. The setting SHALL default to disabled. An unsupported request SHALL render through the fallback without failing scene load or leaving stale cluster resources active, and SHALL report the unavailable state through the editor and logs.

#### Scenario: Supported backend enables clustering

- **WHEN** clustered lighting is enabled on a backend with all required capabilities
- **THEN** visible point and spot lights are evaluated through the clustered lighting path

#### Scenario: Unsupported backend falls back

- **WHEN** clustered lighting is requested on a backend lacking a required capability
- **THEN** the frame uses the existing per-light deferred path and the editor reports that clustering is unavailable

#### Scenario: Legacy scene defaults to fallback

- **WHEN** a scene without a clustered-lighting setting is loaded
- **THEN** clustered lighting is disabled and rendering uses the existing deferred path

### Requirement: The renderer SHALL build a camera-relative 3D cluster grid

For each clustered frame, the renderer SHALL divide the camera view frustum into a fixed-size XY tile grid and exponential depth slices between the camera near and far planes. Cluster coordinates and light assignment SHALL be rebuilt from the current camera, viewport dimensions, and visible-light snapshot. Depth reconstruction SHALL use the active backend's projection conventions and the G-buffer's linear view depth.

#### Scenario: Camera motion updates cluster assignment

- **WHEN** the camera transform, projection, near/far planes, or viewport size changes
- **THEN** the next clustered frame derives its grid and light assignments from the updated values rather than reusing stale assignments

#### Scenario: Pixel selects its containing cluster

- **WHEN** the lighting or debug shader processes an opaque pixel with linear view depth
- **THEN** it selects the same cluster coordinate used by the assignment pass for that pixel's screen position and depth

### Requirement: Cluster assignment SHALL conservatively cover local-light influence

The assignment pass SHALL test each visible point or spot light's view-space influence volume against cluster bounds and append the light index to every cluster it can illuminate. Per-cluster counts and global index storage SHALL have explicit validated limits; overflow SHALL be reported in debug/development builds and handled deterministically without memory corruption. Directional lights SHALL remain in the existing global lighting path.

#### Scenario: Light spanning multiple clusters

- **WHEN** a point or spot light's influence volume intersects several clusters
- **THEN** its index appears in each intersected cluster and pixels in those clusters can receive its contribution

#### Scenario: Light outside the camera frustum

- **WHEN** a local light cannot intersect any visible cluster
- **THEN** it consumes no cluster-light-list entries and contributes no local lighting

#### Scenario: Cluster list reaches capacity

- **WHEN** assignment exceeds a documented per-cluster or global storage limit
- **THEN** the renderer follows a deterministic overflow policy, emits a diagnostic, and remains within allocated bounds

### Requirement: Clustered shading SHALL match fallback deferred lighting

For the same camera and frame inputs, clustered and fallback paths SHALL produce equivalent direct point and spot lighting within image-comparison tolerance, including attenuation, spot cones, shadowing, normal mapping, metallic/roughness response, emissive, and HDR values. A local light SHALL be evaluated at most once per shaded pixel. Directional lighting, ambient, sky, transparency, and postprocessing SHALL follow the same downstream passes in both modes.

#### Scenario: A/B render of mixed local lights

- **WHEN** a scene containing shadowed and unshadowed point and spot lights renders with clustering on and off
- **THEN** reference images agree within the established rendering comparison tolerance and no light is doubled or omitted

#### Scenario: HDR highlights remain HDR

- **WHEN** clustered PBR lighting produces values above 1.0
- **THEN** those values reach the HDR lighting/composite target and are tonemapped by the existing mandatory HDR chain

### Requirement: The compact G-buffer SHALL preserve decoded consumer contracts

The deferred G-buffer SHALL remove redundant channels and use channel-appropriate formats while preserving the decoded diffuse/ambient-mixed color, view normal, linear depth, metallic/roughness material data, and masking information required by existing passes. Existing reserved texture names used by postprocess descriptors and tooling SHALL remain valid. Every G-buffer writer and consumer, including PBR and Lambert lighting, SSAO, SSR, sky, ocean, picking/debug tools, and capture previews, SHALL use one shared packing contract.

#### Scenario: Screen-space effect reads compact normal and depth

- **WHEN** SSAO or SSR samples `$gbuffer_normal` and `$gbuffer_depth` after compaction
- **THEN** it receives the same decoded world/view normal convention and linear view-depth semantics as before compaction

#### Scenario: Reserved G-buffer name remains valid

- **WHEN** an existing postprocess descriptor references a previously supported `$gbuffer_*` texture name
- **THEN** the name resolves and the effect receives the documented decoded data rather than silently binding an unrelated target

#### Scenario: No redundant full-frame channel remains

- **WHEN** the pipeline allocates its compact G-buffer
- **THEN** no color attachment duplicates linear depth or another value already available from an allocated G-buffer attachment unless a documented precision or sampling constraint requires it

### Requirement: Cluster resources SHALL obey render-thread and frame-lifetime rules

Cluster light data, counters, index lists, dispatch arguments, and debug statistics SHALL be created and accessed through the RHI on the render thread. Their lifetime SHALL cover all submitted GPU work that references them, and resizing or toggling the feature SHALL not read scene state on the render thread or recycle in-flight storage prematurely.

#### Scenario: Toggle during rendering

- **WHEN** clustered lighting is toggled while render-thread execution is active
- **THEN** subsequent packets use a consistent mode and resource set without data races or use-after-free

#### Scenario: Cooked shader loads without source compilation

- **WHEN** a packaged Metal or Vulkan build runs with clustered lighting enabled
- **THEN** all required clustered compute and lighting permutations load from cooked backend artifacts with reflection metadata and no runtime GLSL-only dependency

### Requirement: The editor SHALL visualize cluster structure and occupancy

The editor SHALL offer `Cluster Grid` and `Cluster Light Count` entries through the same debug-view mechanism used for other rendering diagnostics. The grid view SHALL show the screen-space tile boundaries and depth-slice structure using the active camera's cluster parameters. The count view SHALL visualize each pixel's assigned local-light count with a documented color scale. Selecting either view SHALL generate the required cluster data even if normal effect debug views are disabled, and returning to `None` SHALL restore the normal scene output.

#### Scenario: Inspect cluster grid

- **WHEN** the user selects `Cluster Grid` with clustered lighting active
- **THEN** the viewport displays cluster tile boundaries and depth-slice coloring derived from the same grid used for shading

#### Scenario: Inspect a dense light region

- **WHEN** the user selects `Cluster Light Count` and local lights overlap in part of the view
- **THEN** the overlapping region displays a higher count color than an otherwise identical empty region

#### Scenario: Debug view on unsupported backend

- **WHEN** a cluster debug view is requested while clustered lighting is unavailable
- **THEN** the editor explains that clustering is unavailable and continues to present a valid scene or fallback image rather than sampling uninitialized buffers
