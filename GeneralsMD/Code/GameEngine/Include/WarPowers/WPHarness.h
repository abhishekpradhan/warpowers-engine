// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The War Powers authors
//
// War Powers autotest / clicktest harness (developer builds only).
//
// Scripted playtests that drive a running match through the same message
// stream and UI paths a human uses, then print [WP_AUTO] / [WP_CLICK]
// verdicts to stderr. The harness is a separate translation unit
// (Source/WarPowers/WPHarness.cpp) that is compiled only when the CMake
// option WP_HARNESS is ON: configure with -DWP_HARNESS=ON, or use the
// `wasm-harness` preset. The call sites in GameEngine::update() and
// SDL3GameEngine::update() are wrapped in `#if WP_HARNESS`, so a default
// (production) build contains none of this code and reads none of these
// environment variables.
//
// Environment variables (the browser shell forwards them as ENV.*):
//
//   WP_AUTOTEST=<mode>       in-match state machine; any value enables it:
//     <other/unlisted>       unit lab: select the local CC, queue 4 tanks, rally them,
//                            duel the enemy guards, then attack the enemy CC
//     build                  unit lab that stops once one tank has spawned (pairs with WP_CLICKTEST)
//     base                   dozer loop: CC -> Fabricator -> PowerArray -> VehiclePlant -> tank
//     wedge                  base, but the second construct order is injected at ~88% of the first
//     strike                 field 4 tanks and destroy the enemy structure nearest the player base
//     ghost                  fog-memory lifecycle: scout a neutral structure, retreat, kill it, re-scout
//     husk                   translucent-remnant repro: UI-placed VehiclePlant site killed while fogged
//     defeat                 kill the local command center at frame 300 (WP_Lose script -> defeat screen)
//     win                    kill the enemy command center at frame 300 (WP_Win script -> victory)
//     cycle                  quit match 1 at frame 300, redeploy WPTest from the shell, log display state
//     economy                supply-chain fixture: depot + hauler, verify stock transfer and cash
//     powers                 special-power fixture: fire the tech structure's power at an enemy relay
//     mission                authored-map objective lab (WPTraining, WPOp01..04, WPChallengeM/J)
//     mission-defeat[-hq]    mission lab that expects the defeat result (-hq: always kill the player CC)
//     retry                  score-screen Retry lifecycle: menu -> training -> loss -> Retry, repeated
//   WP_AUTOTEST_UNIT=<tpl>   template the unit lab fields instead of WP_Tank / WPJ_Mongrel
//   WP_RETRY_RUNS=<n>        retry mode: Retry cycles to complete (1..30, default 3)
//   WP_RETRY_FRAMES=<n>      retry mode: match length before the injected loss (900..54000, default 5400)
//   WP_CLICKTEST[=ui]        SDL3 self-driving mouse test: click the tank and a ground point
//                            (default), or ui: click the CC, then the ControlBar build button
//
// The labs keep their state in function statics and are single-shot per
// process: a second match in the same run (score-screen Retry, or the cycle
// mode) continues from the first match's stage counters by design.
//
// Related but not part of this unit: WP_SCENE_DUMP=<frame> (W3DDisplay.cpp
// scene census) and WP_REVIEW_SCENE (WPShell.cpp asset review fixture).
#pragma once

#include "Lib/BaseType.h"

// Runs the WP_AUTOTEST state machines (including the WP_AUTOTEST=retry score
// Retry diagnostic). Call once per GameEngine::update(), after
// TheGameClient->UPDATE() and before TheMessageStream->propagateMessages(),
// so the injected messages reach the logic on the same frame they always did.
void WPHarness_Update();

// Platform hook for WP_CLICKTEST. Fabricates one left-button press
// (down = TRUE) or release at internal display coordinates (x, y) and pushes
// it through the same path OS clicks take. Reports the window-space position
// the event landed on. Returns FALSE when the platform cannot inject input.
typedef Bool (*WPHarnessClickFn)(void* context, Int x, Int y, Bool down, Real* windowX, Real* windowY);

// Runs the WP_CLICKTEST state machine. Call once per platform update, after
// OS events are polled and before GameEngine::update(). Returns immediately
// unless WP_CLICKTEST is set and a match is running.
void WPHarness_ClickTest(WPHarnessClickFn sendClick, void* context);
