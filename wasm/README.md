# `wasm/`: browser build glue

Everything the Emscripten target needs that is not ordinary engine code. `cmake/wasm-deps.cmake` wires these pieces in when `EMSCRIPTEN` is set; nothing here is compiled into native builds.

| Path | Purpose |
|---|---|
| `d8web_bridge/d8web_bridge.cpp` | COM-style adapter between the DXVK `d3d8.h` interfaces the engine compiles against and the vendored d8web renderer (`WP_D8WEB_DIR`, default `../dvijoke/d8web` in the parent workspace). It provides `Igroteka_Direct3DCreate8`, which stands in for the `Direct3DCreate8` the native builds `dlopen`. Built as the `d8web_bridge` static library by `GeneralsMD/Code/Main/CMakeLists.txt` and linked into `GeneralsXZH`; also hosts the `WP_SURFACE_TRACE` surface-ownership records. |
| `wasm_compat.h` | Force-included into every translation unit (`-include`): `wcslcpy`/`wcslcat`, BSD functions that macOS has and Emscripten's musl lacks. |
| `fontconfig_stub/` | `fontconfig/fontconfig.h` and `fontconfig_stub.c`: the minimal fontconfig API used by `render2dsentence.cpp`, resolving every font query to `/fonts/default.ttf` in the virtual file system. FreeType itself comes from the Emscripten port (`-sUSE_FREETYPE=1`). |
| `experimental/` | GeneralsXWeb's WebRTC LAN prototype (`boot.html`, `webrtc_udp.js`). Unsupported, not built or staged; see its [README](experimental/README.md). |

The browser shell (loader, settings, staging of the dataset and the `.wasm`) is not here: it lives in the parent workspace's `web/` and `tools/genwebstage.py`. Link options for the browser build (memory limits, wasm exceptions, IDBFS) are in `cmake/wasm-deps.cmake`; the `wasm` and `wasm-harness` presets are described in the [README](../README.md).
