---
applyTo: 'cmake/**,CMakeLists.txt,CMakePresets.json'
---

## Build Presets

### Legacy (Maintenance Only)
- **`vc6`** — Visual Studio 6 (C++98), 32-bit, DirectX 8 + Miles
- **`win32`** — MSVC 2022 (C++20), experimental upstream path

### Browser (Emscripten + SDL3 + MiniAudio + d8web) — War Powers Product Target
- **`wasm`** — production browser build; contains no `WP_HARNESS` diagnostic code
- **`wasm-harness`** — the same plus `WP_HARNESS=ON` (`WPHarness.cpp`, self-tests, click tests, review scenes) for `?autotest=`/`?review=` runs; builds under `build/wasm-harness`
- Configure with `emcmake cmake --preset wasm`; the renderer comes from `WP_D8WEB_DIR` (default `../dvijoke/d8web` in the parent workspace)
- Link and memory options live in `cmake/wasm-deps.cmake`; DXVK supplies headers only (`cmake/dx8.cmake`, `EMSCRIPTEN` branch)

### Cross-Platform (SDL3 + DXVK + OpenAL/MiniAudio) — Native Development
- **`linux64-deploy`** — GCC/Clang x86_64, Release — **PRIMARY LINUX**
- **`linux64-miniaudio`** / **`linux64-openal`** — Linux audio-backend variants
- **`macos-vulkan`** — macOS ARM64, RelWithDebInfo — **PRIMARY MACOS**
- **`mingw-w64-i686`** — exploratory MinGW-w64 cross-compile for Windows
- **`windows64-deploy`** — planned MinGW-w64 x86_64 (issue #29, not active)

## Build Workflow

```bash
# Browser (War Powers product target; emsdk activated)
emcmake cmake --preset wasm
cmake --build build/wasm --target GeneralsXZH.js

# Linux (Docker)
./scripts/build/linux/docker-configure-linux.sh linux64-deploy
./scripts/build/linux/docker-build-linux-zh.sh linux64-deploy

# Linux (native)
cmake --preset linux64-deploy
cmake --build build/linux64-deploy --target z_generals

# macOS
cmake --preset macos-vulkan
cmake --build build/macos-vulkan --target z_generals

# Windows cross-build (exploratory)
cmake --preset mingw-w64-i686
cmake --build build/mingw-w64-i686 --target z_generals
```

## DXVK Source of Truth (macOS)

- DXVK fixes live in the `references/fbraz3-dxvk` submodule, which tracks the War Powers DXVK fork (`warpowers-dxvk`, branch `main`); the path keeps its upstream name for merge compatibility.
- By default the macOS build clones the pinned upstream commit `DXVK_REMOTE_REF` (fbraz3/dxvk) into `build/<preset>/_deps/` and ignores the submodule.
- Local mode: `-DSAGE_DXVK_USE_LOCAL_FORK=ON` builds the submodule checkout (no fetch/update).

**Rules**:
1. Never patch DXVK files inside `build/_deps/...`.
2. Do not rely on transient patch scripts.
3. Keep Linux/macOS aligned on DXVK 2.6 branch unless explicitly changed.
4. Validate locally in local-fork mode → commit in the submodule → update the submodule pointer here.

## Testing Strategy

1. Per-platform smoke tests: launch game, reach main menu, load skirmish map.
2. Replay compatibility: VC6 optimized builds with `RTS_BUILD_OPTION_DEBUG=OFF`.
3. Cross-platform validation: same replays valid across platforms.
