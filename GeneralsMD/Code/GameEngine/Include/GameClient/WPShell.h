// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The War Powers authors
//
// War Powers shell and browser-bridge entry points (WPShell.cpp). The window
// layout callbacks (WP*Init / WP*System) stay in GameClient/GUICallbacks.h next
// to the stock menus; everything the rest of the engine calls lives here so no
// translation unit needs a function-scope extern declaration.
#pragma once

#include "Lib/BaseType.h"

class AsciiString;
class UnicodeString;

// Match flow -------------------------------------------------------------------------------------

/// ScriptActions VICTORY/DEFEAT: snapshot the score before player data is torn down.
void WPRecordMatchResult( Bool victory );

/// GameEngine::update: HUD text and the browser state snapshot (Module.onGameState).
void WPUpdatePlayerExperience();

/// ScriptActions display-text: route a WP: objective key to the web surface. FALSE = show natively.
Bool WPDisplayMissionText( const AsciiString &key );

/// InGameUI player notices: route to the web surface. FALSE = show natively.
Bool WPDisplayPlayerMessage( const UnicodeString &message );

/// GameLogic::startNewGame: developer review fixture. A no-op unless built with WP_HARNESS.
void WPCreateReviewScene();

// Mission diagnostics (WP_AUTOTEST=mission*) ------------------------------------------------------
void WPArmMissionDiagnosticResult( Bool victory );
Int WPGetMissionDiagnosticResult();

// Diagnostics (defined in GameEngine.cpp) ---------------------------------------------------------
void WPPrintBacktrace();

// Menu curtain (defined in GameLogicDispatch.cpp) -------------------------------------------------
/// TRUE from the moment a match exit is confirmed until the shell's first paint;
/// the display blacks out every frame in between.
extern Bool g_wpMenuCurtain;
