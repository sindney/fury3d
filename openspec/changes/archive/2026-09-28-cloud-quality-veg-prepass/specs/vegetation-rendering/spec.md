# vegetation-rendering (delta)

## ADDED Requirements

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
