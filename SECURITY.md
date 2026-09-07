# Security policy

This repository is the engine component of [War Powers](https://github.com/abhishekpradhan/warpowers): a GeneralsX-derived engine compiled to WebAssembly for the browser game, plus the native macOS/Linux development builds used to work on it.

## Reporting a vulnerability

Report vulnerabilities privately through GitHub's **Report a vulnerability** form (Security tab, Advisories) rather than in a public issue, pull request or discussion:

- Engine (this repository): https://github.com/abhishekpradhan/warpowers-engine/security/advisories/new
- Parent repository (game data, web shell, hosting): https://github.com/abhishekpradhan/warpowers/security/advisories/new

Either destination is fine when you are unsure; reports are triaged together. Include the build identifier (browser game: Settings, Copy diagnostics; native: commit hash and CMake preset), the browser/OS or native platform, the steps to reproduce, and what an attacker gains. You will receive an acknowledgement in the advisory thread, and the fix ships as ordinary commits and, for the browser game, a new deployed build. There is no bug bounty.

## Scope

In scope:

- The browser engine build produced by the `wasm` preset (`GeneralsXZH.js`/`.wasm`) and its JavaScript boundary: the d8web bridge under `wasm/`, file-system staging, save/load exports and the callbacks the web shell registers.
- The native development builds (`macos-vulkan`, `linux64-*` presets) as used for engine work.

Elsewhere:

- Game content, the web application (loader, settings, saves) and hosting belong to the parent repository.
- The DXVK fork used only by the native macOS path: report on [warpowers-dxvk](https://github.com/abhishekpradhan/warpowers-dxvk), or upstream [DXVK](https://github.com/doitsujin/dxvk) if it reproduces there.
- Problems in code shared with [GeneralsX](https://github.com/fbraz3/GeneralsX) or [TheSuperHackers' GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) that reproduce on their builds should also be reported to them, following their own security policies, so the fix reaches every port. Use the advisory thread here to coordinate timing before anything is public.
- The original GameSpy online services are defunct and their code paths are not reachable in the browser build. The WebRTC prototype under `wasm/experimental/` is unsupported and not shipped.
