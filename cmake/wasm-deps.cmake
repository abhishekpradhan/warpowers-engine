# Igroteka @build 05/07/2026 - Emscripten dependency strategy (no vcpkg).
# glm/gli: header-only, fetched directly (their own CMake is skipped — Populate only).
# freetype: Emscripten built-in port (-sUSE_FREETYPE=1 at compile and link).
# fontconfig: local stub resolving every font query to the bundled /fonts/default.ttf.
# openal/curl: not used on wasm (miniaudio audio, update check off).

if(NOT EMSCRIPTEN)
    return()
endif()

include(FetchContent)

# ---- glm (header-only) ----
FetchContent_Declare(
    glm_src
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
    URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c
)
FetchContent_GetProperties(glm_src)
if(NOT glm_src_POPULATED)
    FetchContent_Populate(glm_src)
endif()
if(NOT TARGET glm::glm)
    add_library(glm_headers INTERFACE)
    target_include_directories(glm_headers INTERFACE ${glm_src_SOURCE_DIR})
    add_library(glm::glm ALIAS glm_headers)
endif()

# ---- gli (header-only) ----
FetchContent_Declare(
    gli_src
    URL https://github.com/g-truc/gli/archive/refs/tags/0.8.2.0.tar.gz
    URL_HASH SHA256=9e7024c2df77c011eff4f66667c1834620c70b7902cd50f32ab48edd49fe0139
)
FetchContent_GetProperties(gli_src)
if(NOT gli_src_POPULATED)
    FetchContent_Populate(gli_src)
endif()
if(NOT TARGET gli)
    add_library(gli INTERFACE)
    target_include_directories(gli INTERFACE ${gli_src_SOURCE_DIR})
endif()

# ---- freetype via Emscripten port ----
# The port supplies headers at compile time and the library at link time.
add_compile_options("-sUSE_FREETYPE=1")
add_link_options("-sUSE_FREETYPE=1")

# ---- zlib via Emscripten port ----
# One zlib for the engine and FreeType alike; see Core/Libraries/Source/Compression/CMakeLists.txt.
add_compile_options("-sUSE_ZLIB=1")
add_link_options("-sUSE_ZLIB=1")

# ---- fontconfig stub ----
add_library(fontconfig_stub STATIC ${CMAKE_SOURCE_DIR}/wasm/fontconfig_stub/fontconfig_stub.c)
target_include_directories(fontconfig_stub PUBLIC ${CMAKE_SOURCE_DIR}/wasm/fontconfig_stub)
# Header visible everywhere; the stub library is linked explicitly by WW3D2
# (a global link_libraries() here would leak into SDL3's exported targets).
include_directories(${CMAKE_SOURCE_DIR}/wasm/fontconfig_stub)

message(STATUS "wasm-deps: glm/gli fetched, freetype via emscripten port, fontconfig stubbed")

# ---- GameSpy SDK platform identity ----
# The vendored GameSpy SDK detects platforms via __linux__ and defines _LINUX
# internally; Emscripten defines neither. Its Linux/POSIX paths compile fine
# against Emscripten's musl headers (sockets exist at compile time; the service
# is dead anyway and the transport gets replaced with WebRTC later).
# GameSpy platform defines live in cmake/gamespy.cmake (PUBLIC on the gamespy target).

# ---- libc gap shim ----
# Force-included into every TU: wcslcpy/wcslcat (BSD functions macOS has, musl lacks).
add_compile_options("SHELL:-include ${CMAKE_SOURCE_DIR}/wasm/wasm_compat.h")

# ---- C++ exceptions ----
# SAGE throws C++ exceptions as control flow (INI parse errors, MetaMap label
# translation). Without wasm exception support every throw aborts the runtime.
add_compile_options("-fwasm-exceptions")
add_link_options("-fwasm-exceptions")

# ---- Emscripten link configuration ----
# GROWABLE_ARRAYBUFFERS=0: Chrome rejects resizable ArrayBuffer views in WebGL
# upload calls (learned the hard way in d8web).
# INITIAL_MEMORY 512MB + growth: SAGE's memory pools want a large heap up front.
# STACK_SIZE 8MB: the engine assumes Windows-sized thread stacks.
# Link-time: binaryen's -O3 post-link passes SIGABRT on this binary when wasm
# exceptions are enabled; -O1 + stripped DWARF avoids the crashing pass (compile
# optimization stays -O3).
# --profiling-funcs keeps the wasm name section (~1MB): browser stack traces
# name the C++ frame instead of "wasm-function[12345]" — the Safari
# quit-crash hunt needed exactly that.
add_link_options("-O1" "-g0" "--profiling-funcs")
# WarPowers @fix 05/09/2026 Leave address space for ASan shadow memory.
# ASan places its shadow inside wasm32 memory; a 4GB application limit cannot
# fit both. Ordinary release builds keep their existing 4GB ceiling.
if(RTS_BUILD_OPTION_ASAN)
    set(wasm_maximum_memory 2147483648)
else()
    set(wasm_maximum_memory 4294967296)
endif()
add_link_options(
    "-sALLOW_MEMORY_GROWTH=1"
    "-sGROWABLE_ARRAYBUFFERS=0"
    "-sINITIAL_MEMORY=536870912"
    # Staged files, renderer allocations and a loaded mission share this heap.
    "-sMAXIMUM_MEMORY=${wasm_maximum_memory}"
    "-sSTACK_SIZE=8388608"
    "-sEXIT_RUNTIME=0"
    # Persistent user saves are mounted and restored by the browser shell.
    "-lidbfs.js"
)

# NOTE: the loading-screen yield needs stack-switching. JSPI (Chrome-only, Safari
# lacks it -> engine won't instantiate) and ASYNCIFY (incompatible with
# -fwasm-exceptions, 3x binary) both fail as a universal build. The cross-browser
# fix is an async state-machine load (rAF-driven chunks); until then the load runs
# synchronously (brief freeze) but boots everywhere.

# ---- d8web: D3D8→WebGL2 translation layer + engine bridge ----
# WarPowers @refactor 07/09/2026 The renderer is vendored in the parent War
# Powers workspace (dvijoke/d8web, one level above this repository); another
# checkout is selected with -DWP_D8WEB_DIR=<path>. Fail with a clear message
# instead of CMake's "add_subdirectory given source which is not an existing
# directory" when the engine is configured outside the workspace.
set(WP_D8WEB_DIR "${CMAKE_SOURCE_DIR}/../dvijoke/d8web" CACHE PATH "Path to the dvijoke d8web renderer sources")
if(NOT EXISTS "${WP_D8WEB_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "d8web renderer not found at ${WP_D8WEB_DIR}: build from the War Powers workspace (git clone https://github.com/abhishekpradhan/warpowers.git; it vendors dvijoke/d8web beside engine/), or set WP_D8WEB_DIR to a d8web checkout")
endif()
add_subdirectory("${WP_D8WEB_DIR}" d8web EXCLUDE_FROM_ALL)
# (the d8web_bridge target is created next to z_generals, where the engine's
# d3d8lib interface target with the DXVK/CompatLib include set already exists)
