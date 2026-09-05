# War Powers engine

The engine fork used by [War Powers](https://github.com/abhishekpradhan/warpowers), a browser RTS with its own freely licensed game dataset. This repository carries the GeneralsX engine lineage, the Emscripten target, and the integration needed to run the War Powers menus, controls and missions.

**This is a development fork, currently private.** The upstream release links below describe other projects; they are not War Powers releases. Publication, deployment and upstream submissions remain separate owner decisions.

## Build the browser game

Use the complete War Powers workspace. The WebAssembly target expects `dvijoke/d8web` beside `engine/`; this repository alone does not contain the browser renderer, boot page or game dataset.

Prerequisites: an activated Emscripten SDK with `emcc` and `emcmake` on `PATH`, CMake, Ninja and Python 3. Initial configuration downloads the dependencies declared in CMake.

```sh
git clone --recursive https://github.com/abhishekpradhan/warpowers.git
cd warpowers/engine
emcmake cmake --preset wasm
cmake --build build/wasm --target GeneralsXZH.js
cd ..
python3 tools/genwebstage.py
python3 tools/serve.py
```

Open [the local game](http://localhost:8322). The workspace server supplies WASM MIME and cache headers. Keep the same URL for browser records and checkpoints, and reuse an existing server on that port. Use `emcmake` when configuring: plain CMake can select the host compiler. The engine outputs are `build/wasm/GeneralsMD/GeneralsXZH.js` and `GeneralsXZH.wasm`. The workspace staging command combines them with the current dataset and web application.

The browser build uses SDL3 input, MiniAudio, and the D3D8-to-WebGL2 `d8web` renderer. DXVK's native compatibility headers are used during compilation, but DXVK's Vulkan libraries and MoltenVK do not run in the browser. Browser multiplayer and cross-platform deterministic replay compatibility are not established by a successful WebAssembly build.

## Native development

Native builds are useful for engine diagnostics and asset inspection. Start with the inherited [macOS build guide](docs/BUILD/MACOS.md) or [Linux build guide](docs/BUILD/LINUX.md) for toolchain setup. Some inherited installation and replay instructions assume separately owned retail game files; they do not describe the War Powers dataset.

For native macOS work against the DXVK checkout pinned by this repository, configure from the engine directory:

```sh
cmake --preset macos-vulkan -DSAGE_DXVK_USE_LOCAL_FORK=ON
cmake --build build/macos-vulkan --target z_generals
```

The local-fork option matters: without it, [cmake/dx8.cmake](cmake/dx8.cmake) fetches its configured upstream DXVK commit instead of consuming `references/fbraz3-dxvk`. Edit that source checkout, never generated files under `build/_deps/`. See the [DXVK fork README](references/fbraz3-dxvk/README.md) for its integration boundary.

## What belongs here

Engine fixes, browser integration, input/rendering/audio behavior and War Powers engine callbacks belong in this fork. Unit rules, models, textures, maps, mission metadata, web UI and asset generators belong in the parent War Powers workspace. Keep shared platform fixes separate from pack-specific behavior so useful fixes can be reviewed for upstream contribution later.

[CONTRIBUTING.md](CONTRIBUTING.md) describes fork routing, validation and review. [AGENTS.md](AGENTS.md) contains implementation constraints. The [worklog](docs/WORKLOG/README.md) distinguishes current fork work from inherited upstream history. Existing [replay instructions](TESTING.md) and [runtime flags](docs/ETC/COMMAND_LINE_PARAMETERS.md) remain references, not evidence that this fork has passed every upstream test.

## Lineage and attribution

This work builds on EA's released Generals / Zero Hour source, [GeneralsX](https://github.com/fbraz3/GeneralsX), [TheSuperHackers](https://github.com/TheSuperHackers/GeneralsGameCode), the [Fighter19/feliwir port](https://github.com/Fighter19/CnC_Generals_Zero_Hour), and [GeneralsXWeb](https://github.com/meerzulee/GeneralsXWeb). Their engine, platform and browser work is the foundation of this fork. Original copyright notices and history are retained.

For the upstream projects' own releases and documentation, see [GeneralsX releases](https://github.com/fbraz3/GeneralsX/releases), [TheSuperHackers releases](https://github.com/TheSuperHackers/GeneralsGameCode/releases), and the inherited [tutorial index](docs/HOWTO/README.md). Those projects do not supply or endorse War Powers releases.

The inherited [LICENSE.md](LICENSE.md) contains GPL version 3 and EA's additional terms; it is unchanged. Component licenses remain with their respective code. This source release does not grant redistribution rights to EA game assets, and War Powers does not include or require them. EA does not endorse or support this fork.
