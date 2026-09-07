# Contributing to the War Powers engine

This repository is the engine half of War Powers: a GeneralsX fork compiled to WebAssembly for the browser game and kept buildable natively for development. This page explains where a change belongs, how the code is written and what to run before opening a pull request. [README.md](README.md) has the build instructions.

## Where does the change go?

| Change | Repository |
|---|---|
| Engine code, the browser bridge under `wasm/`, input, rendering, audio, simulation, save/load exports, the War Powers menu and mission callbacks, CMake and presets | Here |
| Unit rules, models, textures, maps, missions, strings, the web application (loader, settings, saves), asset generators, staging and hosting | [Parent repository](https://github.com/abhishekpradhan/warpowers) |
| DXVK changes for the native macOS path | [warpowers-dxvk](https://github.com/abhishekpradhan/warpowers-dxvk) through the submodule at `references/fbraz3-dxvk`, followed by a submodule update here. Never patch `build/_deps/`. |

Generic engine fixes, meaning anything a retail-data GeneralsX build would also want, are welcome here. Keep them as focused commits with reproduction details so that they can be offered upstream as pull requests later, and keep them separate from dataset-specific behaviour: a feature that depends on War Powers data is not a retail backport. Upstream remotes in a maintained checkout are fetch-only; nothing is pushed there from this repository.

## Writing the code

- Follow the surrounding code. The engine sources use tabs and the legacy Westwood style (`Bool`, `Int`, `TheGameLogic`); `.editorconfig` covers the basics. CMake and Python use four spaces.
- Annotate fork changes at the site: `// WarPowers @fix DD/MM/YYYY <why>` (also `@feature`, `@refactor`). Say why the code changed, not what it does. Inherited `GeneralsX @...`, `GeneralsXWeb @...` and `Igroteka @...` annotations stay untouched.
- Diagnostics use `Core/Libraries/Include/WPTrace.h`: `WP_TRACE("...")` for output gated by `IG_TRACE`, `wpEnvEnabled("WP_SOMETHING")` for an opt-in switch. No unconditional `fprintf(stderr, ...)`, and nothing that prints during ordinary play. Self-tests, click tests and review scenes belong in `GeneralsMD/Code/GameEngine/Source/WarPowers/WPHarness.cpp` behind `WP_HARNESS`; the production `wasm` preset must not grow diagnostic code.
- New files start with `// SPDX-License-Identifier: GPL-3.0-or-later` and `// Copyright (c) <year> The War Powers authors`. Do not copy the Electronic Arts banner onto files EA did not write, and leave existing headers alone.
- Platform code stays in `Core/GameEngineDevice/`, `Core/Libraries/Source/Platform/`, `GeneralsMD/Code/GameEngineDevice/` and `wasm/`. No `__EMSCRIPTEN__`, Cocoa, X11 or raw POSIX calls in game logic.
- The simulation stays deterministic: rendering, audio and UI changes must not change `GameLogic` results. Follow the `WWMath` wrapper, NaN-guard and `ScopedFPUGuard` rules in [AGENTS.md](AGENTS.md).
- Shared fixes apply to both trees (`GeneralsMD/`, the Zero Hour engine that the browser ships, and `Generals/`) and to both audio backends where the code exists in both.
- No EA assets, retail data, credentials or machine-specific paths. Keep behaviour changes separate from reformatting, and use the current content names (Meridian Combine, Jackal Front) when describing War Powers behaviour.

## Testing

Run the fixture checks from the engine directory. They need Python 3 and a C++17 compiler with AddressSanitizer and UndefinedBehaviorSanitizer support; `CXX` selects it.

```sh
CXX=clang++ python3 scripts/qa/test-keyboard-modifiers.py
CXX=clang++ python3 scripts/qa/test-sentence-hotkeys.py
CXX=clang++ python3 scripts/qa/test-mip-filter.py
CXX=clang++ python3 scripts/qa/test-surface-copy.py
```

They compile the actual production methods (keyboard modifier ordering, sentence layout and hotkeys, D3DX mip filtering and surface copies) against deterministic fixtures; `--baseline <revision>` additionally requires an older revision to fail. They do not exercise SDL, the browser or game assets. The same commands are in [.github/workflows/qa.yml](.github/workflows/qa.yml).

Browser changes need a browser walkthrough: build the `wasm` preset, stage through the parent workspace, play the affected interaction and record the browser, build identifier, steps and result in the pull request. A native build alone does not validate the WebGL2 renderer or browser input.

The `wasm-harness` preset adds the diagnostics described in the README's table. Use them as regression evidence, and say what each run injects or forces so it is not mistaken for a played result:

- `WP_AUTOTEST` modes create logged fixtures, advance readiness or trigger result paths directly; `win` and `defeat` are not human-played victories, and none of them covers the whole interface. Pair them with input-driven checks when selection, layout, loading or restart behaviour changes.
- `WP_AUTOTEST=retry` starts from the ordinary menu, repeats Field Orientation through paid construction, supply deliveries and native UI selection, then kills the headquarters after `WP_RETRY_FRAMES` frames so that the normal loss script, score screen and Retry callback run (`WP_RETRY_RUNS` times). Each run must reach a fresh training battlefield with a selectable headquarters and visible HUD; PASS pauses the final scene. The browser form is `/?autotest=retry&retryruns=3&retryframes=5400`. This is a controlled lifecycle test with an injected loss, not a balance check.
- `WP_REVIEW_SCENE=1` places faction asset rows on the `WPTest`/`WPTestJ` Flats maps; `stress` adds at most 120 mixed units with attack-move orders and logs counts, frame rates and heap capacity for 60 simulation seconds. Wall-time numbers depend on the tab not being throttled, and heap capacity is not live memory use.

For save changes, distinguish successful serialization from durable IndexedDB persistence, then check reload, restore and failure recovery. For audio or rendering changes, check a native backend as well when the code is shared. Retail replay compatibility ([TESTING.md](TESTING.md)) is the upstream projects' process and needs their game files; it is not part of this fork's checks.

## Pull requests

- One topic per pull request, in focused commits with [Conventional Commit](.github/instructions/git-commit.instructions.md) messages (`fix(input): ...`; no `@` in commit text).
- Describe the problem, the resulting behaviour, what was run (fixture commands, native build, browser walkthrough with its build identifier) and any remaining limits.
- Target `main` of this repository. Contributions to upstream GeneralsX or TheSuperHackers are separate pull requests against their repositories.
- Report engine bugs with the [issue template](.github/ISSUE_TEMPLATE/bug-report.yaml); security problems go through [SECURITY.md](SECURITY.md).
