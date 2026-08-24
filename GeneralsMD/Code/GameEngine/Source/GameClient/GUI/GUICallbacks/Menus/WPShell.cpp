/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: WPShell.cpp //////////////////////////////////////////////////////////
// WarPowers @feature In-engine shell: main menu, deployment (skirmish) menu,
// options, and post-match score screen. The War Powers layouts declare these
// callbacks; the stock EA menus (MainMenu.cpp faction flyouts, GameSpy
// screens) stay dormant. Matches started here use the same start path as the
// -file command line (m_pendingFile + MSG_NEW_GAME) but, with m_initialFile
// empty, the engine returns to the shell after the match instead of quitting
// to desktop — which is what makes quit-to-menu work.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "Common/GameAudio.h"
#include "Common/AudioAffect.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/ScoreKeeper.h"
#include "GameClient/Color.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Mouse.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"
#include "GameLogic/GameLogic.h"

// ----------------------------------------------------------------------------
// Match-result capture (filled by ScriptActions at VICTORY/DEFEAT time, read
// by the score screen after the engine returns to the shell — player/score
// data is gone by then, so snapshot here).
// ----------------------------------------------------------------------------
struct WPMatchResult
{
	Bool valid;
	Bool victory;
	Int unitsBuilt;
	Int unitsLost;
	Int unitsDestroyed;
	Int buildingsBuilt;
	Int moneyEarned;
	UnsignedInt durationFrames;
};
static WPMatchResult s_wpResult = { FALSE, FALSE, 0, 0, 0, 0, 0, 0 };

void WPRecordMatchResult( Bool victory )
{
	s_wpResult.valid = TRUE;
	s_wpResult.victory = victory;
	s_wpResult.unitsBuilt = 0;
	s_wpResult.unitsLost = 0;
	s_wpResult.unitsDestroyed = 0;
	s_wpResult.buildingsBuilt = 0;
	s_wpResult.moneyEarned = 0;
	s_wpResult.durationFrames = TheGameLogic ? TheGameLogic->getFrame() : 0;

	Player *player = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if( player )
	{
		ScoreKeeper *score = player->getScoreKeeper();
		if( score )
		{
			s_wpResult.unitsBuilt = score->getTotalUnitsBuilt();
			s_wpResult.unitsLost = score->getTotalUnitsLost();
			s_wpResult.unitsDestroyed = score->getTotalUnitsDestroyed();
			s_wpResult.buildingsBuilt = score->getTotalBuildingsBuilt();
			s_wpResult.moneyEarned = score->getTotalMoneyEarned();
		}
	}

#ifdef __EMSCRIPTEN__
	// Web-shell mode: the page renders the result card (the engine quits back
	// to it after the end-game banner), so hand the stats out now — by shell
	// time the player/score data is gone.
	EM_ASM({
		if (typeof Module !== 'undefined' && Module.onMatchResult)
			Module.onMatchResult({ victory: $0 === 1, unitsBuilt: $1,
				unitsLost: $2, unitsDestroyed: $3, buildingsBuilt: $4,
				moneyEarned: $5, durationSeconds: $6 });
	}, s_wpResult.victory ? 1 : 0, s_wpResult.unitsBuilt, s_wpResult.unitsLost,
		s_wpResult.unitsDestroyed, s_wpResult.buildingsBuilt,
		s_wpResult.moneyEarned,
		(int)(s_wpResult.durationFrames / LOGICFRAMES_PER_SECOND));
#endif
}

// ----------------------------------------------------------------------------
// Shared: start a map through the command-line path (menu-mode: m_initialFile
// stays empty, so the post-match flow returns here instead of exiting).
// ----------------------------------------------------------------------------
static void wpStartMap( const char *mapPath )
{
	// WarPowers @debug IG_TRACE menu-start forensics
	static const bool wpTrace = getenv("IG_TRACE") && *getenv("IG_TRACE") != '0';
	if (wpTrace)
		fprintf(stderr, "[WPSHELL] startMap '%s'\n", mapPath);
	TheWritableGlobalData->m_pendingFile = mapPath;
	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheWritableGlobalData->m_playIntro = FALSE;

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SINGLE_PLAYER );
	msg->appendIntegerArgument( DIFFICULTY_NORMAL );
	msg->appendIntegerArgument( 0 );
	InitRandom( 0 );
}

// ----------------------------------------------------------------------------
// Generic immediate shutdown: the shell's push/pop machinery WAITS for the
// outgoing layout's shutdown to call shutdownComplete() (stock menus do it at
// the end of their hide transitions). A layout with LAYOUTSHUTDOWN = [None]
// wedges every subsequent push/pop forever — so every WP shell screen uses
// this.
// ----------------------------------------------------------------------------
void WPShellShutdown( WindowLayout *layout, void *userData )
{
	layout->hide( TRUE );
	if( TheShell )
		TheShell->shutdownComplete( layout );
}

// ----------------------------------------------------------------------------
// Main menu
// ----------------------------------------------------------------------------
static NameKeyType wpButtonEngageID = NAMEKEY_INVALID;
static NameKeyType wpButtonOptionsID = NAMEKEY_INVALID;
static NameKeyType wpButtonQuitID = NAMEKEY_INVALID;

void WPMainMenuInit( WindowLayout *layout, void *userData )
{
	// The intro render-freeze is normally cleared by the stock MainMenuInit;
	// this layout owns that job now (see Intro::doPostIntro).
	TheWritableGlobalData->m_breakTheMovie = FALSE;
	TheMouse->setVisibility( TRUE );

	wpButtonEngageID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonEngage" );
	wpButtonOptionsID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonOptions" );
	wpButtonQuitID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonQuit" );

	// Build stamp doubles as the version label.
	GameWindow *version = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( "MainMenu.wnd:LabelVersion" ) );
	if( version )
	{
		UnicodeString stamp;
		stamp.format( L"build %hs %hs", __DATE__, __TIME__ );
		GadgetStaticTextSetText( version, stamp );
	}

	layout->hide( FALSE );
	layout->bringForward();
}

void WPMainMenuUpdate( WindowLayout *layout, void *userData )
{
	// Intro::doPostIntro can arm the scene-render freeze after our init ran
	// (shell push resolves a frame later than engine init); with the menu up
	// the freeze must never persist or the shell renders exactly one frame.
	if( TheWritableGlobalData->m_breakTheMovie )
		TheWritableGlobalData->m_breakTheMovie = FALSE;

	// Post-match: once the shell is back on top with a recorded result, raise
	// the score screen (pushing from init would race the shell's own pending
	// push machinery).
	if( s_wpResult.valid && TheShell && !TheGameLogic->isInGame() )
	{
		if( TheShell->top() == layout )
		{
			TheShell->push( "Menus/WPScore.wnd" );
		}
	}
}

WindowMsgHandledType WPMainMenuSystem( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_CREATE:
		case GWM_DESTROY:
			break;

		case GWM_INPUT_FOCUS:
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;
			break;

		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if( controlID == wpButtonEngageID )
			{
				TheShell->push( "Menus/WPSkirmish.wnd" );
			}
			else if( controlID == wpButtonOptionsID )
			{
				WindowLayout *optLayout = TheShell->getOptionsLayout( TRUE );
				if( optLayout )
				{
					optLayout->runInit();
					optLayout->hide( FALSE );
					optLayout->bringForward();
				}
			}
			else if( controlID == wpButtonQuitID )
			{
				TheGameEngine->setQuitting( TRUE );
			}
			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}

// ----------------------------------------------------------------------------
// Deployment (skirmish) menu — pick a front, the map launches.
// ----------------------------------------------------------------------------
static NameKeyType wpDeployMeridianID = NAMEKEY_INVALID;
static NameKeyType wpDeployJackalID = NAMEKEY_INVALID;
static NameKeyType wpSkirmishBackID = NAMEKEY_INVALID;

void WPSkirmishInit( WindowLayout *layout, void *userData )
{
	wpDeployMeridianID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonDeployMeridian" );
	wpDeployJackalID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonDeployJackal" );
	wpSkirmishBackID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonBack" );

	layout->hide( FALSE );
	layout->bringForward();
}

WindowMsgHandledType WPSkirmishSystem( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_CREATE:
		case GWM_DESTROY:
			break;

		case GWM_INPUT_FOCUS:
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;
			break;

		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			// Long map paths (Maps\<name>\<name>.map): TerrainLogic->loadMap
			// takes the real file path; the short form the -file flag accepts is
			// expanded by CommandLine.cpp's (file-static) converter, which this
			// path never runs through. Short form here = extent-0 empty world.
			if( controlID == wpDeployMeridianID )
				wpStartMap( "Maps\\WPTest\\WPTest.map" );
			else if( controlID == wpDeployJackalID )
				wpStartMap( "Maps\\WPTestJ\\WPTestJ.map" );
			else if( controlID == wpSkirmishBackID )
				TheShell->pop();
			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}

// ----------------------------------------------------------------------------
// Options — master volume slider + back. Lives behind the stock filename
// (Menus/OptionsMenu.wnd) so the quit menu's Options button and
// ToggleQuitMenu's close-options path work unchanged.
// ----------------------------------------------------------------------------
static NameKeyType wpOptionsBackID = NAMEKEY_INVALID;
static NameKeyType wpVolumeSliderID = NAMEKEY_INVALID;

void WPOptionsInit( WindowLayout *layout, void *userData )
{
	wpOptionsBackID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:ButtonBack" );
	wpVolumeSliderID = TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:SliderVolume" );

	GameWindow *slider = TheWindowManager->winGetWindowFromId( nullptr, wpVolumeSliderID );
	if( slider )
	{
		Int pos = 80;
		const char *env = getenv( "WP_VOLUME" );
		if( env && *env )
			pos = atoi( env );
		if( pos < 0 ) pos = 0;
		if( pos > 100 ) pos = 100;
		GadgetSliderSetPosition( slider, pos );
	}

	layout->hide( FALSE );
	layout->bringForward();
}

WindowMsgHandledType WPOptionsSystem( GameWindow *window, UnsignedInt msg,
																			WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_CREATE:
		case GWM_DESTROY:
			break;

		case GWM_INPUT_FOCUS:
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;
			break;

		case GSM_SLIDER_TRACK:
		{
			GameWindow *control = (GameWindow *)mData1;
			if( control && control->winGetWindowId() == wpVolumeSliderID )
			{
				Real volume = ((Real)(Int)mData2) / 100.0f;
				if( volume < 0.0f ) volume = 0.0f;
				if( volume > 1.0f ) volume = 1.0f;
				if( TheAudio )
					TheAudio->setVolume( volume, (AudioAffect)AudioAffect_All );
#ifdef __EMSCRIPTEN__
				// Persist across sessions; the boot page seeds WP_VOLUME
				// from this key.
				EM_ASM({ try { localStorage.setItem('wpVolume', String($0)); } catch (e) {} },
					(int)mData2);
#endif
			}
			break;
		}

		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if( controlID == wpOptionsBackID )
			{
				WindowLayout *optLayout = TheShell->getOptionsLayout( FALSE );
				if( optLayout )
					optLayout->hide( TRUE );
				TheShell->destroyOptionsLayout();
			}
			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}

// ----------------------------------------------------------------------------
// Score screen — post-match stats over the main menu.
// ----------------------------------------------------------------------------
static NameKeyType wpScoreContinueID = NAMEKEY_INVALID;

static void wpSetScoreLine( const char *winName, const UnicodeString &text )
{
	GameWindow *win = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( winName ) );
	if( win )
		GadgetStaticTextSetText( win, text );
}

void WPScoreInit( WindowLayout *layout, void *userData )
{
	wpScoreContinueID = TheNameKeyGenerator->nameToKey( "WPScore.wnd:ButtonContinue" );

	GameWindow *banner = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( "WPScore.wnd:ResultBanner" ) );
	if( banner )
	{
		GadgetStaticTextSetText( banner, TheGameText->fetch(
			s_wpResult.victory ? "WP:Victory" : "WP:Defeat" ) );
		if( !s_wpResult.victory )
		{
			Color red = GameMakeColor( 200, 70, 70, 255 );
			banner->winSetEnabledTextColors( red, red );
		}
	}

	UnicodeString line;
	line.format( TheGameText->fetch( "WP:ScoreUnits" ).str(),
							 s_wpResult.unitsBuilt, s_wpResult.unitsLost, s_wpResult.unitsDestroyed );
	wpSetScoreLine( "WPScore.wnd:StatUnits", line );

	line.format( TheGameText->fetch( "WP:ScoreStructures" ).str(), s_wpResult.buildingsBuilt );
	wpSetScoreLine( "WPScore.wnd:StatStructures", line );

	line.format( TheGameText->fetch( "WP:ScoreMoney" ).str(), s_wpResult.moneyEarned );
	wpSetScoreLine( "WPScore.wnd:StatMoney", line );

	UnsignedInt seconds = s_wpResult.durationFrames / LOGICFRAMES_PER_SECOND;
	line.format( TheGameText->fetch( "WP:ScoreDuration" ).str(), seconds / 60, seconds % 60 );
	wpSetScoreLine( "WPScore.wnd:StatDuration", line );

	// One showing per match.
	s_wpResult.valid = FALSE;

	layout->hide( FALSE );
	layout->bringForward();
}

WindowMsgHandledType WPScoreSystem( GameWindow *window, UnsignedInt msg,
																		WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_CREATE:
		case GWM_DESTROY:
			break;

		case GWM_INPUT_FOCUS:
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;
			break;

		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			if( control->winGetWindowId() == wpScoreContinueID )
				TheShell->pop();
			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}
