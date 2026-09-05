# Contributing to the War Powers engine fork

This repository is the engine component of the private War Powers workspace. Work here is reviewed against the fork's current goals and its parent dataset. Inherited GeneralsX documentation remains useful technical context; its release, issue and pull-request destinations are not automatic destinations for this fork.

## Route work to the right repository

- Engine and browser-runtime changes belong here. Use the parent workspace for game data, maps, art, the web application and generators.
- DXVK source changes belong in `references/fbraz3-dxvk`, followed by an engine submodule update. Do not patch generated `build/_deps/` trees.
- Before an authorized push or pull request, verify that its target is this fork's intended remote. Upstream remotes are references; preserve their disabled push URLs.
- Publication, repository visibility changes, deployment, and sending upstream issues or pull requests require the owner's explicit authorization. A request to prepare or review a change does not publish it. Follow any authorization already given in the active work session.
- Prepare generally useful fixes as focused commits with reproduction details so an authorized upstream contribution can be made separately. Keep War Powers-specific menus and mission callbacks in this fork.

## Implement a focused change

Read [AGENTS.md](AGENTS.md) and the applicable [.github instructions](.github/instructions/). Preserve platform isolation, deterministic simulation boundaries and existing backend paths. Shared fixes should apply to both Generals and Zero Hour where relevant; a feature depending on War Powers-specific data is not a generic retail backport.

Keep behavior changes separate from broad formatting or refactoring. Follow nearby code conventions, explain user-visible changes at their implementation sites, and retain copyright and source attribution. Use the current names Meridian Combine and Jackal Front when describing War Powers content; inherited retail names remain appropriate only for retail compatibility work.

Do not add EA assets, copied retail data, unlicensed imports, credentials or local machine configuration. Every new game asset needs the parent workspace's provenance record. Review tracked files and new diffs for sensitive data before distribution; do not rewrite shared history or remove inherited source as an incidental cleanup.

## Validate the behavior being changed

Build instructions are in [README.md](README.md). For browser changes, build and stage through the complete parent workspace and exercise the affected interaction in a browser. Record the browser, tested build, input sequence and result. A native build alone does not validate the WebAssembly renderer or browser input.

For keyboard modifier ordering, run this focused regression from the engine directory with Python 3 and a native C++17 compiler (`CXX` defaults to `c++`):

```sh
python3 scripts/qa/test-keyboard-modifiers.py
```

The runner extracts and compiles the actual `Keyboard.cpp` update, repeat and translation methods with deterministic device/timer fixtures; it does not duplicate their algorithm. It checks fast chords, event ordering, modifiers held across updates, combined modifiers and repeats. Add `--baseline <revision-before-fix>` to require an earlier revision to fail those checks as a negative control. This isolates keyboard event-state behavior; SDL/browser delivery, message translation, text entry and actual group creation/recall still require runtime checks. The fixture's small type/key definitions also do not replace a full engine build.

The `WP_AUTOTEST` hooks in `GameEngine.cpp` are focused diagnostics. `base` and `wedge` exercise construction; unit/control labs may create explicitly logged fixtures; `economy` checks native hauling and `powers` checks effects with forced readiness; `win` and `defeat` trigger result paths directly. Do not describe those as human-played balanced victories or as coverage of the entire interface. Pair them with input-driven checks when selection, layout, loading or restart behavior changes.

`WP_AUTOTEST=retry` starts from the ordinary menu and repeats Field Orientation
through paid construction/production, actual supply deliveries and native UI
selection of eight unit/building categories. It waits for training stage 4,
then kills the headquarters after a configurable match duration, allowing the
normal loss script, native score screen and Retry callback to run. It never
calls reset directly or advances objective counters. `WP_RETRY_RUNS` defaults
to 3 (range 1–30); `WP_RETRY_FRAMES` defaults to 5400 (range 900–54000, at 30
simulation frames per second). Each run must reach a fresh stage-0 training
battlefield with a selectable headquarters and visible HUD. PASS pauses that
final scene. The browser equivalent is
`/?autotest=retry&retryruns=3&retryframes=5400`, without a `map` parameter.
Only this diagnostic bypasses the pausing web debrief, retaining result
telemetry without saving operation records; ordinary play and the `defeat`
diagnostic retain the debrief. This is a controlled lifecycle test with an
explicitly injected headquarters loss, not a human-played defeat or balance
check. Use `retryframes=14400` for an eight-minute match before Retry.

`WP_REVIEW_SCENE=1` creates faction asset rows on a fresh Flats map (`WPTest` or `WPTestJ`); `WP_REVIEW_SCENE=stress` adds at most 120 mixed combat units with native attack-move orders. The scene logs fixture counts, observed frame rates and WebAssembly heap capacity for 60 simulation seconds. These are controlled development fixtures, not a normal opening or an automatic visual/performance acceptance test. Pausing or background throttling affects wall-time measurements; heap capacity is not live memory usage. The Jackal Dynamo appears as an art reference even though it is not part of the player's build order.

For save changes, distinguish successful serialization from durable IndexedDB persistence, then verify reload, restore and failure recovery. For audio or rendering changes, check the relevant native backends as well as the browser path. Use the inherited [testing notes](TESTING.md) for retail replay work with appropriately licensed local data. Do not assume inherited CI is running for this fork.

## Prepare the review

Use focused commits and the [commit-message conventions](.github/instructions/git-commit.instructions.md). State the problem, resulting behavior, relevant validation and material remaining limits. Use the active fork as the review target; upstream-target examples in inherited instructions do not override the routing rules above.

Update the monthly [worklog](docs/WORKLOG/README.md), including its AI-generated-content disclosure when applicable. Put implementation notes in the appropriate existing documentation area rather than creating competing roadmaps. AI-assisted changes are welcome, but the contributor remains responsible for readable code, verification and an accurate account of what was tested.
