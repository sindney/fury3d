# hdr-pipeline

## MODIFIED Requirements

### Requirement: Deferred pipeline supports HDR rendering

The deferred pipeline SHALL support an HDR mode that renders lighting and composite output into floating-point render targets (`rgba16f`) rather than 8-bit targets, preserving values above 1.0 through the lighting stage. HDR mode SHALL use the compact G-buffer's channel-appropriate formats; only targets that carry HDR luminance are required to be floating-point. Optional clustered local-light evaluation SHALL write the same HDR lighting output and obey the same PBR and tonemapping contracts as the fallback deferred path.

#### Scenario: Rendering lit scene in HDR

- **WHEN** the pipeline is in HDR mode with clustered lighting either enabled or disabled
- **THEN** the lighting/composite targets use floating-point formats, retain HDR luminance until tonemapping, and receive equivalent local-light contributions within image-comparison tolerance
