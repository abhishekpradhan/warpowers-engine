---
applyTo: '**/*.{cpp,h,hpp,c}'
---

## Code Quality & Maintainability

- **Scope discipline**: Focus on the cross-platform port (SDL3, DXVK, OpenAL/MiniAudio) and the Emscripten/d8web browser target. Avoid unrelated refactors.
- **Root cause**: Fix underlying issues, not symptoms. No lazy workarounds.
- **Isolation**: Platform-specific code stays in platform layers (`Core/GameEngineDevice/`, `Core/Libraries/Source/Platform/`)
- **Fallback paths**: Keep legacy Windows paths (DX8, Miles) intact behind `#ifdef` guards for VC6 baseline.
- **Determinism**: Never break gameplay determinism. Rendering/audio changes must not affect logic.
- **Audio Backporting**: Audio improvements or fixes must be implemented in both the OpenAL and MiniAudio backends where the code exists in both (the browser build ships MiniAudio).
- **Generals Backporting**: All general bugfixes and improvements must be backported to the Generals base game.

## Code Style & Conventions

- **English only**: All code, comments, identifiers in English.
- **No lazy solutions**: No empty stubs, empty `catch` blocks, or commented-out code.
- **C++ heritage**: Maintain consistency with surrounding legacy code patterns.
- **Change annotation**: Every fork change needs `// WarPowers @fix|@feature|@refactor DD/MM/YYYY <why>` above it: say why the code changed, not what it does. Inherited `// GeneralsX @keyword author DD/MM/YYYY ...`, `// GeneralsXWeb @...` and `// Igroteka @...` annotations stay untouched; do not restyle them.
- **Diagnostics**: opt-in traces go through `Core/Libraries/Include/WPTrace.h` (`WP_TRACE(...)`, gated by `IG_TRACE`; `wpEnvEnabled("WP_...")` for a dedicated switch). No unconditional `fprintf(stderr, ...)` and no output during ordinary play. Self-tests, click tests and review scenes belong in `GeneralsMD/Code/GameEngine/Source/WarPowers/WPHarness.cpp` behind `WP_HARNESS`.
- **New files**: start with `// SPDX-License-Identifier: GPL-3.0-or-later` and `// Copyright (c) <year> The War Powers authors`. Do not copy the Electronic Arts banner onto files EA did not write, and leave existing headers alone.
- **Upstream PR attribution**: When implementing work derived from a specific upstream PR, add an adjacent comment:
  - `// Upstream reference: <author>, PR #<id>` and the full GitHub URL.

## Platform Isolation Patterns

**Good** — Platform-specific code isolated in device/platform layers:
```cpp
// Core/GameEngineDevice/Include/w3dgraphicsdevice.h
#ifdef BUILD_WITH_DXVK
    #include "dxvk_adapter.h"
#else
    #include <d3d8.h>
#endif
```

**Bad** — Platform code leaking into game logic:
```cpp
// GeneralsMD/Code/GameEngine/GameLogic/object.cpp -- WRONG
#ifdef __linux__
    // Linux-specific hack in gameplay code
#endif
```

SDL3 is the unified platform layer — no native POSIX (`pthread_*`, `open()`), Win32, or Cocoa calls in game code.
