# War Powers engine

[![License: GPL-3.0 with EA's additional terms](https://img.shields.io/badge/license-GPL--3.0%20with%20EA%20terms-2b6cb0?style=flat)](LICENSE.md)
[![QA](https://github.com/abhishekpradhan/warpowers-engine/actions/workflows/qa.yml/badge.svg)](https://github.com/abhishekpradhan/warpowers-engine/actions/workflows/qa.yml)

The engine component of [War Powers](https://github.com/abhishekpradhan/warpowers), a free browser real-time strategy game with its own original dataset, playable at <https://warpowers.vercel.app>. This repository is a fork of [GeneralsX](https://github.com/fbraz3/GeneralsX), fbraz3's native macOS and Linux port, which builds on [TheSuperHackers' GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode), which continues Electronic Arts' GPL source release of *Command & Conquer: Generals* and *Zero Hour* (the release terms are in [LICENSE.md](LICENSE.md)). War Powers adds the browser target: the engine is compiled with [Emscripten](https://emscripten.org/) against SDL3 for windowing and input and [miniaudio](https://miniaud.io/) for audio, and renders through the vendored [d8web](https://github.com/abhishekpradhan/warpowers/tree/main/dvijoke/d8web) Direct3D 8 to WebGL2 layer (MIT) in the parent workspace. The Emscripten target and d8web originate in [GeneralsXWeb](https://github.com/meerzulee/GeneralsXWeb); this fork merged and extended them.

No EA assets or game data ship with War Powers or are needed to build it. The dataset lives in the parent repository; this repository holds engine code, its build system and the browser bridge. The native macOS and Linux builds keep working and are the fastest way to debug engine behaviour outside a browser.

## What this fork adds

- **The WebAssembly target.** The `wasm` and `wasm-harness` presets, `cmake/wasm-deps.cmake` and the glue under [`wasm/`](wasm/README.md): the d8web bridge (`Igroteka_Direct3DCreate8` stands in for the `Direct3DCreate8` that native builds load), a musl compatibility header, a fontconfig stub, and the link options for memory, wasm exceptions and IDBFS.
- **The War Powers shell.** The in-engine menu and mission callbacks (`WPShell.cpp`, `WarPowers/`), the `_wpShowMission` export, the game-state and match-result callbacks the web shell reads, and checkpoint save/load on an IDBFS-mounted directory. The contract is in the parent's [web-bridge.md](https://github.com/abhishekpradhan/warpowers/blob/main/docs/web-bridge.md).
- **A dataset-neutral core.** Behaviour the retail engine hard-coded is switchable in `GameData.ini` — `RallyPointModel`, `RallyPointLineTexture`, `DozerResumesAbandonedConstruction`, `MapPlacedHarvestersAutoGather`, `CommandButtonAvailabilityCues` and `MusicRotation` — each defaulting to the retail behaviour, so the engine names no War Powers asset.
- **Fixes with regression fixtures.** Keyboard-modifier ordering, sentence layout and hotkeys, D3DX mip-filter reference ownership and surface copies. `scripts/qa/` compiles the production methods against fixtures under ASan/UBSan, locally and in [qa.yml](.github/workflows/qa.yml).
- **Diagnostics that cost nothing in play.** `Core/Libraries/Include/WPTrace.h` traces behind runtime switches, and a self-test, click-test and review-scene harness that is compiled only with `WP_HARNESS=ON`.
- **Traceable changes.** Fork changes are annotated at the site as `// WarPowers @fix|@feature|@refactor DD/MM/YYYY <why>`; new files carry `SPDX-License-Identifier: GPL-3.0-or-later`.

## Build the browser game

The browser build is driven from the parent workspace, which supplies the d8web renderer, the web shell and the game data. Prerequisites: an activated Emscripten SDK (`emcc` and `emcmake` on `PATH`; 6.0.8 is the tested version), CMake 3.25 or newer, Ninja and Python 3.10 or newer. The first configure downloads the dependencies declared under `cmake/`. The full quick start, including the browser requirements and the local gates, is in the [parent README](https://github.com/abhishekpradhan/warpowers#run-it-locally); in short:

```sh
git clone https://github.com/abhishekpradhan/warpowers.git
cd warpowers
git submodule update --init engine
cd engine
emcmake cmake --preset wasm
cmake --build build/wasm --target GeneralsXZH.js
cd ..
python3 tools/genwebstage.py
python3 tools/serve.py
```

Open http://localhost:8322 in a desktop browser with WebGL2. The staging step combines `build/wasm/GeneralsMD/GeneralsXZH.js` and `GeneralsXZH.wasm` with the current dataset and web application; the local server adds the WebAssembly MIME type and cache headers. Configure with `emcmake`: plain CMake may pick the host compiler. Keep one server and URL across sessions, because browser records and checkpoints are stored per origin.

Two configure presets produce browser builds:

| Preset | Build directory | Purpose |
|---|---|---|
| `wasm` | `build/wasm` | Production build. Contains no self-test, click-test or review-scene code (`WP_HARNESS=OFF`). |
| `wasm-harness` | `build/wasm-harness` | Diagnostic build (`WP_HARNESS=ON`). Compiles `GeneralsMD/Code/GameEngine/Source/WarPowers/WPHarness.cpp` and the harness hooks in `GameEngine.cpp`, `SDL3GameEngine.cpp` and `WPShell.cpp`; the definition is applied per file, so the rest of the engine is identical. Required for `?autotest=` and `?review=` runs. Stage it into its own directory and serve that directory from the parent workspace: `python3 tools/genwebstage.py webstage-harness --build-dir engine/build/wasm-harness/GeneralsMD`, then `python3 tools/serve.py --directory webstage-harness`. |

`cmake/wasm-deps.cmake` expects the renderer at `../dvijoke/d8web`, which the clone above provides (d8web is vendored in the parent repository, so the browser build needs no nested submodule; `references/fbraz3-dxvk` and `references/OpenSAGE.BlenderPlugin` are for native development and art regeneration). Point `WP_D8WEB_DIR` at another checkout when the layout differs: `emcmake cmake --preset wasm -DWP_D8WEB_DIR=/path/to/d8web`. The configure step fails with a clear message when the renderer is missing.

The browser build uses DXVK's Direct3D 8 compatibility headers at compile time only, fetched from an upstream DXVK Native release tarball; DXVK's Vulkan libraries and MoltenVK never run in the browser. A successful WebAssembly build does not by itself establish browser multiplayer or cross-platform replay compatibility. [wasm/README.md](wasm/README.md) describes the bridge, the compatibility shims and the unsupported WebRTC prototype kept under `wasm/experimental/`.

## Native development

Native builds are useful for engine diagnostics, asset inspection and native debuggers. Start with the inherited [macOS build guide](docs/BUILD/MACOS.md) or [Linux build guide](docs/BUILD/LINUX.md) for toolchain setup (CMake, Ninja, plus Meson and a LunarG Vulkan SDK on macOS). Inherited installation and replay instructions assume retail game files; they do not describe the War Powers dataset, which the parent workspace copies into a runtime directory for native runs.

The native macOS path renders through DXVK on MoltenVK. `cmake/dx8.cmake` offers two DXVK sources:

- Default: a pinned commit (`DXVK_REMOTE_REF`) of fbraz3's DXVK fork, cloned into `build/<preset>/_deps/`.
- `-DSAGE_DXVK_USE_LOCAL_FORK=ON`: the submodule at `references/fbraz3-dxvk`. The path keeps its upstream name so that merges stay simple, but the submodule tracks the War Powers DXVK fork ([warpowers-dxvk](https://github.com/abhishekpradhan/warpowers-dxvk), branch `main`), which carries the fbraz3 macOS history plus the fork's own fixes. Its [README](https://github.com/abhishekpradhan/warpowers-dxvk#readme) describes the integration boundary.

```sh
cmake --preset macos-vulkan -DSAGE_DXVK_USE_LOCAL_FORK=ON
cmake --build build/macos-vulkan --target z_generals
```

Edit DXVK in the submodule checkout, never under `build/_deps/`. The Linux presets download DXVK's prebuilt native tarball instead of building it.

## Diagnostics

Diagnostic output goes through `Core/Libraries/Include/WPTrace.h`: `WP_TRACE(...)` prints to stderr only while the `IG_TRACE` environment variable is set to a non-empty value other than `0`. The browser shell sets it only with `?debug=1`; native runs export it in the shell. The `WP_*` switches below work the same way: environment variables for native builds, query parameters that the web shell copies into the Emscripten `ENV` at boot for browser builds. The harness column says whether the hook exists only in the `wasm-harness` build; the `#if WP_HARNESS` blocks in the four harness files are the authoritative list.

| Variable | Effect | Needs harness build |
|---|---|---|
| `IG_TRACE=1` | Enables `WP_TRACE` output everywhere (browser: `?debug=1`) | no |
| `WP_AUTOTEST=<mode>` | Self-driving diagnostics (browser: `?autotest=<mode>`). Modes include `base` and `wedge` (construction), `build` and the unit lab, `economy`, `powers`, `strike`, `ghost`, `husk`, `cycle`, `win`, `defeat`, `mission` and `mission-defeat[-hq]` (result paths) and `retry` (repeated score-screen Retry lifecycle). They inject fixtures or force outcomes; they are not played matches. | yes |
| `WP_AUTOTEST_UNIT=<template>` | Unit template fielded by the unit lab instead of the default tank | yes |
| `WP_RETRY_RUNS`, `WP_RETRY_FRAMES` | Runs (1 to 30, default 3) and match length in frames (900 to 54000, default 5400) for `WP_AUTOTEST=retry` (browser: `?retryruns=`, `?retryframes=`) | yes |
| `WP_CLICKTEST=1` or `ui` | Synthesised SDL mouse clicks pushed through the real input path; pair with `WP_AUTOTEST=build` | yes |
| `WP_REVIEW_SCENE=1` or `stress` | Faction asset rows on the `WPTest`/`WPTestJ` Flats maps; `stress` adds up to 120 mixed units with attack-move orders (browser: `?review=1`) | yes |
| `WP_SCENE_DUMP=<frame>` (`+<frame>` re-arms per match) | One-shot render-object census at that logic frame (browser: `?scenedump=`) | no |
| `WP_AI_TRACE=1` | AI production and script-condition tracing (browser: `?aitrace=1`) | no |
| `WP_DOZER_TRACE=1` | Builder task-gate forensics every 15 frames (browser: `?doztrace=1`) | no |
| `WP_SURFACE_TRACE=1` | Surface ownership records in the d8web bridge (browser: `?surfacetrace=1` in diagnostic runs) | no |
| `WP_FRAME_DUMP=<path.tga>` | Writes the back buffer to that file every 60th frame; works with an occluded native window | no |
| `WP_PRESENT_SKIP=<N>` | Presents only every Nth frame so an occluded native window does not throttle the simulation | no |
| `WP_BOOT_MAP=<Maps\X\X.map>` | Boots straight into a map and returns to the in-engine shell afterwards, unlike `-file`, which quits | no |
| `WP_DIFFICULTY=0`, `1` or `2` | Difficulty for `-file` matches (headless AI testing) | no |
| `WP_VOLUME=0..100` | Master volume; the browser shell forwards its settings slider | no |

[CONTRIBUTING.md](CONTRIBUTING.md) explains what each diagnostic does and does not prove.

## Relationship to upstream

A maintained checkout has `origin` (this repository) plus the fetch-only remotes `upstream` ([fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX)), `superhackers` ([TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode)) and `generalsxweb` ([meerzulee/GeneralsXWeb](https://github.com/meerzulee/GeneralsXWeb)) with their push URLs disabled. Upstream changes are merged in from those remotes; nothing is pushed to them from here.

Fixes that are not specific to War Powers (input ordering, D3DX compatibility, text layout, determinism) are kept as focused commits with reproduction details so that they can be offered upstream as pull requests. Dataset-specific behaviour (the War Powers menus, mission callbacks, the browser bridge) stays in this fork. Fork changes are annotated in code as `// WarPowers @fix|@feature|@refactor DD/MM/YYYY <why>`; the inherited `GeneralsX @...`, `GeneralsXWeb @...` and `Igroteka @...` annotations are kept as they are.

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) describes routing, style and validation; [AGENTS.md](AGENTS.md) collects the engine constraints inherited from GeneralsX; [SECURITY.md](SECURITY.md) says how to report a vulnerability privately. Engine bugs use the [issue template](.github/ISSUE_TEMPLATE/bug-report.yaml); content, map, mission and web-shell problems belong in the [parent repository](https://github.com/abhishekpradhan/warpowers/issues/new/choose).

| This repository | Parent workspace | DXVK fork |
|---|---|---|
| Engine fixes; the browser bridge under `wasm/`; input, rendering, audio and simulation; save/load exports; the War Powers menu and mission callbacks (`WPShell.cpp`, `WarPowers/`); CMake and presets | Unit rules, models, textures, maps, missions, strings, the web application, asset generators, staging and hosting | DXVK changes for the native macOS path, followed by a submodule update here |

Keep shared platform fixes separate from dataset-specific behaviour so that generic fixes can be offered upstream. The [worklog](docs/WORKLOG/README.md) is an AI-generated development diary kept for history; contributors are not asked to extend it. Inherited [replay instructions](TESTING.md) and [runtime flags](docs/ETC/COMMAND_LINE_PARAMETERS.md) remain references for the native builds.

## License

GPL-3.0 with Electronic Arts' additional terms, unchanged from the source release: see [LICENSE.md](LICENSE.md). Files added by the fork carry `SPDX-License-Identifier: GPL-3.0-or-later` and are copyright "The War Powers authors". Third-party components keep their own licenses (for example d8web under MIT and DXVK under zlib); the parent workspace's [licensing guide](https://github.com/abhishekpradhan/warpowers/blob/main/LICENSING.md) records the notices that ship with the browser build. This source release does not grant redistribution rights to EA game assets; War Powers neither includes nor requires them. War Powers is not affiliated with or endorsed by Electronic Arts; EA names appear here only to identify the engine's source lineage.

## Inherited GeneralsX material

The sections below come from the upstream GeneralsX README and describe that project. Their release links, community ports and support channels are not War Powers releases or support channels; they are kept for attribution and orientation.

### Upstream projects and releases

- [GeneralsX](https://github.com/fbraz3/GeneralsX): the cross-platform (Linux and macOS) port this fork is based on; [releases](https://github.com/fbraz3/GeneralsX/releases), inherited [build guides](docs/BUILD/) and [tutorials](docs/HOWTO/README.md).
- [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode): the upstream foundation for stability, bug fixes and retail compatibility; [releases](https://github.com/TheSuperHackers/GeneralsGameCode/releases).
- [Fighter19's fork](https://github.com/Fighter19/CnC_Generals_Zero_Hour), with major work by feliwir: the SDL3, DXVK, OpenAL, FFmpeg and filesystem groundwork GeneralsX builds on.
- [GeneralsXWeb](https://github.com/meerzulee/GeneralsXWeb): the first browser/WebAssembly port of GeneralsX and the origin of the Emscripten target, the d8web renderer and the `wasm/` glue.

### Project goals (GeneralsX)

GeneralsX exists to turn upstream preservation and porting work into a practical and maintainable project for active Linux and macOS players: preserve retail gameplay behaviour while modernising the platform layer; keep a single codebase with Linux and macOS as the active targets, applying fixes to both Zero Hour and the Generals base game; replace the Windows-only DirectX 8 and Miles stack with portable open-source equivalents; and keep the upstream lineage clear by distinguishing foundational work from GeneralsX's integration, packaging and platform support. Improvements aligned with upstream stability belong in TheSuperHackers' repository; GeneralsX keeps the changes specific to cross-platform delivery and packaging.

### Special thanks

- [Westwood Studios](https://cnc-comm.com/westwood-studios) for creating the Command & Conquer series, and [EA Games](https://www.ea.com/) for Command & Conquer: Generals and the source release.
- [TheSuperHackers / Xezon](https://github.com/TheSuperHackers/GeneralsGameCode) and contributors for the upstream stability, bug fixes and code modernisation that form the foundation of GeneralsX.
- [Fighter19](https://github.com/Fighter19) for the cross-platform port that pioneered SDL3 windowing, DXVK graphics and MinGW build support on Linux, and [feliwir](https://github.com/feliwir) for the foundational cross-platform systems in that fork: OpenAL audio, FFmpeg video decoding, C++17 filesystem and FreeType/Fontconfig text rendering.
- [fbraz3](https://github.com/fbraz3) and the GeneralsX contributors, and [Meerzulee](https://github.com/meerzulee) for GeneralsXWeb and d8web.

All trademarks are the property of their respective owners.
