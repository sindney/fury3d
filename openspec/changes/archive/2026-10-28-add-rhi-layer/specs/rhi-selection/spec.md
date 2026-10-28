# rhi-selection (delta for add-rhi-layer)

## ADDED Requirements

### Requirement: The RHI backend SHALL be selectable at startup through a defined resolution chain

The active backend SHALL be resolved at engine startup, first source winning: `--rhi=<backend>` CLI arg, `FURY_RHI` environment variable, Lua `Engine.run` option (`rhi = "<backend>"`), editor ini setting, platform default (`opengl`). Backend names SHALL be `opengl`, `vulkan`, `metal`. An unrecognized or unsupported value SHALL log a warning and resolve to the platform default. There SHALL be no runtime switching; changing the backend takes effect on next launch.

#### Scenario: CLI arg wins

- **WHEN** the engine launches with `--rhi=vulkan` while the editor ini says `opengl`
- **THEN** the Vulkan backend is selected and the log names the chosen backend and its source (CLI arg)

#### Scenario: Default is OpenGL

- **WHEN** the engine launches with no rhi specified anywhere
- **THEN** the OpenGL backend is selected

#### Scenario: Unknown value falls to default

- **WHEN** the engine launches with `--rhi=dx12`
- **THEN** a warning is logged naming the unsupported value and the OpenGL backend is selected

### Requirement: Each platform SHALL expose a defined backend support matrix

The supported/selectable backends SHALL be: Windows `opengl, vulkan`; macOS `opengl, vulkan, metal`; Linux `opengl, vulkan`. Editor and game UIs SHALL list only the current platform's backends. A backend compiled out via CMake (`FURY_RHI_VULKAN=OFF`, `FURY_RHI_METAL=OFF`) SHALL be treated as unsupported.

#### Scenario: macOS lists three backends

- **WHEN** the editor settings RHI dropdown opens on macOS with all backends compiled in
- **THEN** it offers exactly `opengl`, `vulkan`, and `metal`

#### Scenario: Windows hides Metal

- **WHEN** the RHI option is displayed on Windows
- **THEN** it offers exactly `opengl` and `vulkan`

### Requirement: A failed backend SHALL fall back to OpenGL with a logged error

After device and swapchain creation, the selected backend SHALL run a basic probe (create a texture, create a trivial pipeline, clear and present one frame). Any failure in device creation, surface creation, or the probe SHALL log the error and re-initialize with the OpenGL backend. Fallback SHALL NOT occur silently.

#### Scenario: Vulkan init failure falls back

- **WHEN** `--rhi=vulkan` is requested on a machine whose Vulkan driver fails device creation
- **THEN** the error is logged with the failing step, and the engine continues with the OpenGL backend in the same launch

#### Scenario: Probe failure falls back

- **WHEN** the probe frame cannot be presented on the selected backend
- **THEN** the failure is logged and the engine falls back to OpenGL

### Requirement: The editor SHALL expose the RHI choice as a persisted settings option

The editor settings window SHALL offer an RHI dropdown (platform matrix only), persisted via the existing imgui.ini settings pattern, applied at next launch, and displaying a "restart to apply" hint after change.

#### Scenario: Editor option persists across restarts

- **WHEN** the user selects `metal` in the editor settings, restarts the editor, and reopens settings
- **THEN** the dropdown shows `metal` and the Metal backend is active

### Requirement: Packaged games SHALL expose the RHI choice among platform-supported backends

The game-side options UI (island game) SHALL offer the platform's supported backends, persist the choice to the game's user config, apply it at next launch, and fall back per the fallback requirement. Lua SHALL expose the active backend name and the supported list.

#### Scenario: Lua reports active backend

- **WHEN** a Lua script queries the active RHI
- **THEN** it returns the name of the backend actually in use (after any fallback), and the supported list matches the platform matrix

### Requirement: Headless CLI paths SHALL ignore RHI selection

The `furye-cli` headless subcommands (`cook`, `package`, `shaderc`) SHALL create no RHI device. The headless capture tools (`render-mesh`, `gui shot`) SHALL always use the OpenGL backend regardless of selection.

#### Scenario: Cook needs no GPU

- **WHEN** `furye-cli cook scene.bin --rhi vulkan,metal` runs on a machine with no display
- **THEN** shader cross-compilation runs through the compiler library and no window or GPU device is created
