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
#include "Common/GameState.h"
#include "Common/Energy.h"
#include "Common/Money.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/ScoreKeeper.h"
#include "GameClient/Color.h"
#include "GameClient/ControlBar.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/Drawable.h"
#include "GameClient/Display.h"
#include "GameClient/View.h"
#include "GameClient/InGameUI.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Image.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetSlider.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Mouse.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/Shell.h"
#include "GameClient/WindowLayout.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/ScriptEngine.h"

// GeneralsX @feature Codex 05/09/2026 Keep browser telemetry tied to the actual
// map, including direct loads, restarts and restored snapshots.
static AsciiString wpCurrentMapID()
{
	AsciiString path = TheGameState ? TheGameState->getPristineMapName() : AsciiString::TheEmptyString;
	const char *leaf = path.str();
	for( const char *p = leaf; *p; ++p )
		if( *p == '/' || *p == '\\' ) leaf = p + 1;
	char id[128];
	snprintf( id, sizeof(id), "%s", leaf );
	char *extension = strrchr( id, '.' );
	if( extension ) *extension = '\0';
	AsciiString result( id );
	// Portable native save paths are lowercased for historical compatibility.
	// Keep pack identity stable for mission metadata, outcome records and retry.
	static const char *maps[] = { "WPTest", "WPTestJ", "WPRidge", "WPRidgeJ", "WPScrap", "WPScrapJ",
		"WPBasin", "WPBasinJ", "WPRange", "WPRangeJ", "WPTraining", "WPOp01", "WPOp02",
		"WPOp03", "WPOp04", "WPChallengeM", "WPChallengeJ" };
	for( Int i = 0; i < (Int)(sizeof(maps) / sizeof(maps[0])); ++i )
		if( result.compareNoCase(maps[i]) == 0 ) return AsciiString(maps[i]);
	return result;
}

static const char *wpOperationID( const AsciiString &id )
{
	if( id == "WPTraining" ) return "training";
	if( id == "WPOp01" ) return "op01";
	if( id == "WPOp02" ) return "op02";
	if( id == "WPOp03" ) return "op03";
	if( id == "WPOp04" ) return "op04";
	if( id == "WPChallengeM" ) return "challenge-meridian";
	if( id == "WPChallengeJ" ) return "challenge-jackal";
	return nullptr;
}

static AsciiString s_wpLastMapPath;
static AsciiString s_wpResultMap;
static Int s_wpResultDifficulty = DIFFICULTY_NORMAL;
static Bool s_wpWebOwnsPause = FALSE;
static Bool s_wpOpenDeployment = FALSE;
static WindowLayout *s_wpMainLayout = nullptr;
static WindowLayout *s_wpDeploymentLayout = nullptr;
static Bool s_wpReviewActive = FALSE;
static Bool s_wpReviewScene = FALSE;
static UnsignedInt s_wpReviewStartMS = 0, s_wpReviewReportFrame = 0;
static UnsignedInt s_wpReviewPowerLogs = 0, s_wpReviewPowerFrame = 0;
static ObjectID s_wpReviewPowerSelection = INVALID_ID;
static Bool s_wpReviewPowerReady = FALSE;
static AsciiString s_wpReviewPowerPending;
static Bool s_wpMissionDiagnosticArmed = FALSE, s_wpMissionDiagnosticExpectedWin = FALSE;
static Int s_wpMissionDiagnosticResult = 0;

Int WPGetMissionDiagnosticResult()
{
	return s_wpMissionDiagnosticResult;
}

void WPArmMissionDiagnosticResult( Bool victory )
{
	s_wpMissionDiagnosticArmed = TRUE;
	s_wpMissionDiagnosticExpectedWin = victory;
	// An unexpected early result stays a failure for this diagnostic run.
	if( s_wpMissionDiagnosticResult != -1 ) s_wpMissionDiagnosticResult = 0;
}

// Developer-only, repeatable native asset/contact review. The regular maps,
// simulation and campaign flow never enter this path without an explicit flag.
void WPCreateReviewScene()
{
	s_wpMissionDiagnosticArmed = FALSE; // This entry point runs once per fresh map.
	s_wpMissionDiagnosticResult = 0;
	s_wpReviewScene = FALSE;
	s_wpReviewPowerLogs = 0;
	s_wpReviewPowerFrame = 0;
	s_wpReviewPowerSelection = INVALID_ID;
	s_wpReviewPowerPending.clear();
	const char *mode = getenv("WP_REVIEW_SCENE");
	if( !mode || (strcmp(mode, "1") != 0 && strcmp(mode, "stress") != 0) ) return;
	const AsciiString map = wpCurrentMapID();
	if( map != "WPTest" && map != "WPTestJ" ) {
		fprintf(stderr, "[WP_REVIEW] skipped: use the Flats map WPTest or WPTestJ\n"); return;
	}
	Player *owner = ThePlayerList->getLocalPlayer(), *enemy = nullptr;
	if( !owner ) return;
	const Bool jackal = map == "WPTestJ";
	const Bool stress = strcmp(mode, "stress") == 0;
	for( Object *obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
		if( obj->isKindOf(KINDOF_COMMANDCENTER) && obj->getControllingPlayer() != owner ) enemy = obj->getControllingPlayer();
	Int created = 0, missing = 0;
	auto spawn = [&](const char *name, Player *player, Real x, Real y) -> Object* {
		const ThingTemplate *tt = TheThingFactory->findTemplate(name);
		Object *obj = tt && player ? TheThingFactory->newObject(tt, player->getDefaultTeam()) : nullptr;
		if( !obj ) { ++missing; fprintf(stderr, "[WP_REVIEW] unavailable template=%s\n", name); return nullptr; }
		Coord3D position = {x, y, TheTerrainLogic->getGroundHeight(x, y)};
		obj->setPosition(&position); obj->setOrientation(0.3f);
		TheAI->pathfinder()->addObjectToPathfindMap(obj);
		for( BehaviorModule **module = obj->getBehaviorModules(); *module; ++module )
			if( (*module)->getCreate() ) (*module)->getCreate()->onBuildComplete();
		++created;
		return obj;
	};
	const char *meridianBuildings[] = {"WP_CommandCenter", "WP_PowerArray", "WP_VehiclePlant", "WP_Bulwark", "WP_LaunchPad",
		"WP_Exchange", "WP_Skyspear", "WP_Rampart", "WP_Directorate", "WP_Longbow"};
	const char *jackalBuildings[] = {"WPJ_CommandPost", "WPJ_Dynamo", "WPJ_ChopShop", "WPJ_Watchpost", "WPJ_Roost",
		"WPJ_Racket", "WPJ_Flakhut", "WPJ_Nest", "WPJ_Den", "WPJ_Lobber"};
	const char *meridianUnits[] = {"WP_Fabricator", "WP_Porter", "WP_Warden", "WP_Lancer", "WP_Vigil", "WP_Bastion",
		"WP_Tank", "WP_Outrider", "WP_Zenith", "WP_Kestrel", "WP_Shrike"};
	const char *jackalUnits[] = {"WPJ_Rigger", "WPJ_Scavenger", "WPJ_Scrapper", "WPJ_Sting", "WPJ_Prowler", "WPJ_Bruiser",
		"WPJ_Mongrel", "WPJ_Vulture", "WPJ_Buzzard", "WPJ_Gnat"};
	for( Int i = 0; i < 10; ++i )
		spawn(jackal ? jackalBuildings[i] : meridianBuildings[i], owner, 450.0f + (i % 5) * 115.0f, 285.0f + (i / 5) * 125.0f);
	for( Int i = 0; i < (jackal ? 10 : 11); ++i )
		spawn(jackal ? jackalUnits[i] : meridianUnits[i], owner, 415.0f + i * 53.0f, 570.0f);
	spawn(jackal ? "WP_JackalWreck" : "WP_MeridianWreck", owner, 965.0f, 330.0f);
	spawn("WP_SmallRuin", owner, 965.0f, 415.0f);
	// Include the Jackal Dynamo as an art reference; it is not a player tech prerequisite.
	fprintf(stderr, "[WP_REVIEW] roster faction=%s created=%d missing=%d rows=buildings,units,wrecks fixtureConstruction=complete\n",
		jackal ? "jackal" : "meridian", created, missing);
	Coord3D camera = {690.0f, 410.0f, 0.0f};
	if( stress && enemy )
	{
		const char *meridianArmy[] = {"WP_Tank", "WP_Warden", "WP_Lancer", "WP_Outrider", "WP_Zenith", "WP_Bastion", "WP_Kestrel", "WP_Shrike"};
		const char *jackalArmy[] = {"WPJ_Mongrel", "WPJ_Scrapper", "WPJ_Sting", "WPJ_Vulture", "WPJ_Bruiser", "WPJ_Prowler", "WPJ_Buzzard", "WPJ_Gnat"};
		Object *armies[120] = {};
		for( Int i = 0; i < 120; ++i ) {
			const Bool friendly = i < 60;
			const Bool factionJ = friendly ? jackal : !jackal;
			const Int n = i % 60;
			armies[i] = spawn(factionJ ? jackalArmy[n % 8] : meridianArmy[n % 8], friendly ? owner : enemy,
				(friendly ? 510.0f : 990.0f) + (n % 6) * 24.0f, 750.0f + (n / 6) * 25.0f);
		}
		Int orders = 0;
		for( Int i = 0; i < 120; ++i )
			if( armies[i] && armies[i]->getAIUpdateInterface() ) {
				Coord3D target = {i < 60 ? 1030.0f : 570.0f, 865.0f, 0.0f};
				target.z = TheTerrainLogic->getGroundHeight(target.x, target.y);
				armies[i]->getAIUpdateInterface()->aiAttackMoveToPosition(&target, -1, CMD_FROM_SCRIPT);
				++orders;
			}
		camera.x = 810.0f; camera.y = 865.0f;
		fprintf(stderr, "[WP_REVIEW] stress addedArmyLimit=120 realAttackMoveOrders=%d totalFixtures=%d missing=%d\n", orders, created, missing);
	}
	if( TheTacticalView ) { TheTacticalView->setZoomToMax(); TheTacticalView->lookAt(&camera); }
	s_wpReviewScene = TRUE;
	s_wpReviewActive = TRUE; s_wpReviewStartMS = timeGetTime(); s_wpReviewReportFrame = 0;
	fprintf(stderr, "[WP_REVIEW] enabled map=%s mode=%s; visual quality requires manual review\n", map.str(), mode);
}

// GeneralsX @feature Codex 05/09/2026 Scripted objective announcements use
// the accessible web event surface when available, with native fallback.
Bool WPDisplayMissionText( const AsciiString &key )
{
#ifdef __EMSCRIPTEN__
	if( strncmp(key.str(), "WP:", 3) != 0 ) return FALSE;
	AsciiString text;
	text.translate( TheGameText->fetch(key) );
	AsciiString map = wpCurrentMapID();
	return EM_ASM_INT({
		if (!Module.onGameMessage) return 0;
		Module.onGameMessage({id: UTF8ToString($0), text: UTF8ToString($1), map: UTF8ToString($2)});
		return 1;
	}, key.str(), text.str(), map.str()) != 0;
#else
	return FALSE;
#endif
}

// GeneralsX @bugfix Codex 05/09/2026 Native War Powers notices share the
// browser notification surface so they cannot hide behind the objective HUD.
Bool WPDisplayPlayerMessage( const UnicodeString &message )
{
#ifdef __EMSCRIPTEN__
	if( !TheGameLogic || !TheGameLogic->isInGame() || !ThePlayerList ) return FALSE;
	const Player *player = ThePlayerList->getLocalPlayer();
	if( !player || (player->getSide() != "WP" && player->getSide() != "WPJ") ) return FALSE;
	AsciiString text;
	text.translate( message );
	const AsciiString map = wpCurrentMapID();
	return EM_ASM_INT({
		if (typeof Module.onGameMessage !== 'function') return 0;
		Module.onGameMessage({id: 'WP:PlayerNotice', text: UTF8ToString($0), map: UTF8ToString($1)});
		return 1;
	}, text.str(), map.str()) != 0;
#else
	return FALSE;
#endif
}

static void wpHudText( const char *name, const UnicodeString &text )
{
	if( !TheWindowManager ) return;
	GameWindow *window = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey(name) );
	if( window && GadgetStaticTextGetText(window) != text ) GadgetStaticTextSetText( window, text );
}

static AsciiString wpControlKey( GameMessage::Type command )
{
	// Read the active native map, including user remaps applied at boot.
	for( const MetaMapRec *map = TheMetaMap ? TheMetaMap->getFirstMetaMapRec() : nullptr; map; map = map->m_next )
	{
		if( map->m_meta != command || !(map->m_usableIn & COMMANDUSABLE_GAME) ) continue;
		AsciiString key;
		if( map->m_modState & CTRL ) key.concat("CTRL+");
		if( map->m_modState & ALT ) key.concat("ALT+");
		if( map->m_modState & SHIFT ) key.concat("SHIFT+");
		for( Int i = 0; KeyNames[i].name; ++i )
			if( KeyNames[i].value == map->m_key && map->m_key != MK_NONE ) {
				key.concat(KeyNames[i].name + 4); key.concat(": "); return key;
			}
	}
	return AsciiString::TheEmptyString;
}

// GeneralsX @feature Codex 05/09/2026 Review-only diagnostics distinguish a
// charging/disabled power from a command button that never enters targeting.
// Read public state only; never force readiness or invoke the command.
static void wpReviewSelectedPower( Object *selected, const AsciiString &map, UnsignedInt frame )
{
	if( !s_wpReviewScene || !selected || s_wpReviewPowerLogs >= 64 ) return;
	const AsciiString name = selected->getTemplate()->getName();
	if( name != "WP_Directorate" && name != "WPJ_Den" ) return;
	SpecialPowerModuleInterface *power = nullptr;
	for( BehaviorModule **module = selected->getBehaviorModules(); *module && !power; ++module )
		power = (*module)->getSpecialPower();
	const CommandButton *pending = TheInGameUI ? TheInGameUI->getGUICommand() : nullptr;
	const AsciiString pendingName = pending ? pending->getName() : AsciiString::TheEmptyString;
	const Bool ready = power && power->isReady();
	if( selected->getID() == s_wpReviewPowerSelection && ready == s_wpReviewPowerReady &&
		pendingName == s_wpReviewPowerPending && frame < s_wpReviewPowerFrame + 150 ) return;
	GameWindow *button = TheWindowManager ? TheWindowManager->winGetWindowFromId(nullptr,
		TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonCommand01")) : nullptr;
	const UnsignedInt buttonStatus = button ? button->winGetStatus() : 0;
	const CommandButton *command = button ? (const CommandButton *)GadgetButtonGetData(button) : nullptr;
	UnsignedInt disabledMask = 0;
	for( Int i = 0; i < DISABLED_COUNT; ++i )
		if( selected->isDisabledByType((DisabledType)i) ) disabledMask |= 1u << i;
	const Player *owner = selected->getControllingPlayer();
	fprintf(stderr, "[WP_REVIEW] power frame=%u map=%s side=%s object=%s id=%u power=%s ready=%d readyFrame=%u percent=%.3f disabled=%d disabledMask=0x%x underConstruction=%d scriptDisabled=%d scriptUnpowered=%d powerProduced=%d powerConsumed=%d buttonFound=%d buttonEnabled=%d buttonHidden=%d command=%s options=0x%x pending=%s\n",
		frame, map.str(), owner ? owner->getSide().str() : "none", name.str(), (UnsignedInt)selected->getID(),
		power ? power->getPowerName().str() : "none", (Int)ready, power ? power->getReadyFrame() : 0,
		power ? power->getPercentReady() : 0.0f, (Int)selected->isDisabled(), disabledMask,
		(Int)selected->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION),
		(Int)selected->testScriptStatusBit(OBJECT_STATUS_SCRIPT_DISABLED),
		(Int)selected->testScriptStatusBit(OBJECT_STATUS_SCRIPT_UNPOWERED),
		owner ? owner->getEnergy()->getProduction() : 0, owner ? owner->getEnergy()->getConsumption() : 0,
		(Int)(button != nullptr), (Int)((buttonStatus & WIN_STATUS_ENABLED) != 0),
		(Int)((buttonStatus & WIN_STATUS_HIDDEN) != 0), command ? command->getName().str() : "none",
		command ? (UnsignedInt)command->getOptions() : 0, pending ? pendingName.str() : "none");
	fflush(stderr);
	++s_wpReviewPowerLogs;
	s_wpReviewPowerFrame = frame;
	s_wpReviewPowerSelection = selected->getID();
	s_wpReviewPowerReady = ready;
	s_wpReviewPowerPending = pendingName;
}

// GeneralsX @feature Codex 05/09/2026 Read-only player telemetry drives help
// and mission UI; it never changes simulation counters or opponent state.
void WPUpdatePlayerExperience()
{
	static UnsignedInt ticks = 0;
	if( (++ticks % 8) != 0 ) return;
	const Bool inGame = TheGameLogic && TheGameLogic->isInGame();
	Player *player = inGame && ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	const AsciiString map = inGame ? wpCurrentMapID() : AsciiString::TheEmptyString;
	const UnsignedInt frame = inGame ? TheGameLogic->getFrame() : 0;
	Int units = 0, structures = 0, builders = 0, production = 0, income = 0, idleWorkers = 0;
	Real headquartersHealth = 0.0f, headquartersMaxHealth = 0.0f;
	if( player )
		for( Object *object = TheGameLogic->getFirstObject(); object; object = object->getNextObject() )
		{
			if( object->getControllingPlayer() != player || object->isEffectivelyDead() ) continue;
			if( object->isKindOf(KINDOF_STRUCTURE) ) ++structures;
			else if( object->isKindOf(KINDOF_VEHICLE) || object->isKindOf(KINDOF_INFANTRY) ) ++units;
			if( object->isKindOf(KINDOF_DOZER) )
			{
				++builders;
				if( object->getAIUpdateInterface() && object->getAIUpdateInterface()->isIdle() ) ++idleWorkers;
			}
			if( object->isKindOf(KINDOF_COMMANDCENTER) && object->getBodyModule() )
			{
				headquartersHealth = object->getBodyModule()->getHealth();
				headquartersMaxHealth = object->getBodyModule()->getMaxHealth();
			}
			if( object->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION) ) continue;
			const char *name = object->getTemplate()->getName().str();
			if( strstr(name, "VehiclePlant") || strstr(name, "ChopShop") || strstr(name, "Barracks") ||
				strstr(name, "Hideout") || strstr(name, "LaunchPad") || strstr(name, "Roost") ) ++production;
			if( strcmp(name, "WP_Exchange") == 0 || strcmp(name, "WPJ_Racket") == 0 ) ++income;
		}
	Drawable *drawable = inGame && TheInGameUI ? TheInGameUI->getFirstSelectedDrawable() : nullptr;
	Object *selected = drawable ? drawable->getObject() : nullptr;
	wpReviewSelectedPower(selected, map, frame);
	Int selectedCount = inGame && TheInGameUI ? TheInGameUI->getSelectCount() : 0;
	AsciiString selectedName, selectedTemplate;
	Real health = 0.0f, maxHealth = 0.0f;
	UnicodeString label, detail;
	if( selected )
	{
		selectedName.translate( selected->getTemplate()->getDisplayName() );
		selectedTemplate = selected->getTemplate()->getName();
		if( selected->getBodyModule() )
		{
			health = selected->getBodyModule()->getHealth();
			maxHealth = selected->getBodyModule()->getMaxHealth();
		}
		label = selected->getTemplate()->getDisplayName();
		if( selectedCount > 1 ) label.format( L"%d UNITS SELECTED", selectedCount );
		if( !selected->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION) )
			detail.format( L"HEALTH  %d / %d", (Int)health, (Int)maxHealth );
	}
	else if( TheGameText ) label = TheGameText->fetch("WP:SelectionHint");
	wpHudText( "ControlBar.wnd:SelectionTitle", label );
	wpHudText( "ControlBar.wnd:SelectionDetail", detail );
	// GeneralsX @feature Codex 05/09/2026 Explain power availability in text;
	// a dark command portrait alone does not communicate charge or power state.
	UnicodeString orders = TheGameText ? TheGameText->fetch("WP:OrdersLabel") : UnicodeString::TheEmptyString;
	if( TheGameText && selected && selectedCount == 1 && selected->getControllingPlayer() == player &&
		(selectedTemplate == "WP_Directorate" || selectedTemplate == "WPJ_Den") &&
		!selected->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION) )
	{
		SpecialPowerModuleInterface *power = nullptr;
		for( BehaviorModule **module = selected->getBehaviorModules(); *module && !power; ++module )
			power = (*module)->getSpecialPower();
		if( power )
		{
			if( selected->isDisabledByType(DISABLED_UNDERPOWERED) ||
				selected->isDisabledByType(DISABLED_SCRIPT_UNDERPOWERED) ||
				selected->testScriptStatusBit(OBJECT_STATUS_SCRIPT_UNPOWERED) )
				orders = TheGameText->fetch("WP:PowerRequired");
			else if( selected->isDisabled() || selected->testScriptStatusBit(OBJECT_STATUS_SCRIPT_DISABLED) )
				orders = TheGameText->fetch("WP:PowerUnavailable");
			else if( power->isReady() )
				orders = TheGameText->fetch("WP:PowerReady");
			else
			{
				const UnsignedInt readyFrame = power->getReadyFrame();
				const UnsignedInt remaining = readyFrame > frame ? readyFrame - frame : 0;
				const UnsignedInt seconds = remaining / LOGICFRAMES_PER_SECOND + (remaining % LOGICFRAMES_PER_SECOND != 0);
				if( seconds ) orders.format(TheGameText->fetch("WP:PowerRecharging"), (Int)(seconds / 60), (Int)(seconds % 60));
				else orders = TheGameText->fetch("WP:PowerUnavailable");
			}
		}
	}
	wpHudText( "ControlBar.wnd:OrdersTitle", orders );
	if( player )
	{
		UnicodeString army;
		if( !selected || !selected->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION) )
			army.format( L"%d UNITS  /  %d STRUCTURES", units, structures );
		wpHudText( "ControlBar.wnd:ArmySummary", army );
		UnicodeString hint;
		AsciiString hintText;
		hintText.format( "%sIDLE BUILDER  /  %sHEADQUARTERS",
			wpControlKey(GameMessage::MSG_META_SELECT_NEXT_IDLE_WORKER).str(),
			wpControlKey(GameMessage::MSG_META_VIEW_COMMAND_CENTER).str() );
		hint.translate( hintText );
		wpHudText( "ControlBar.wnd:ControlsHint", hint );
		UnicodeString power;
		power.format( L"POWER  %d / %d", player->getEnergy()->getConsumption(), player->getEnergy()->getProduction() );
		wpHudText( "ControlBar.wnd:PowerSummary", power );
	}
	if( !inGame ) s_wpWebOwnsPause = FALSE;
	if( !inGame ) s_wpReviewActive = FALSE;
	if( !inGame ) s_wpReviewScene = FALSE;
	if( s_wpReviewActive && frame >= s_wpReviewReportFrame + 300 )
	{
		const Real wallSeconds = (timeGetTime() - s_wpReviewStartMS) / 1000.0f;
		UnsignedInt heapBytes = 0;
#ifdef __EMSCRIPTEN__
		heapBytes = (UnsignedInt)EM_ASM_INT({ return HEAP8.length; });
#endif
		fprintf(stderr, "[WP_REVIEW] sample frame=%u wallSeconds=%.1f renderFPS=%.1f observedLogicFPS=%.1f heapCapacityBytes=%u playerUnits=%d structures=%d\n",
			frame, wallSeconds, TheDisplay ? TheDisplay->getAverageFPS() : 0.0f,
			wallSeconds > 0.0f ? frame / wallSeconds : 0.0f, heapBytes, units, structures);
		s_wpReviewReportFrame = frame;
		if( frame >= 1800 ) { s_wpReviewActive = FALSE; fprintf(stderr, "[WP_REVIEW] sampling complete; no performance or visual pass is inferred\n"); }
	}
#ifdef __EMSCRIPTEN__
	const TCounter *stage = inGame && TheScriptEngine ? TheScriptEngine->getCounter("WP_ObjectiveStage") : nullptr;
	const TCounter *progress = inGame && TheScriptEngine ? TheScriptEngine->getCounter("WP_ObjectiveProgress") : nullptr;
	const TCounter *target = inGame && TheScriptEngine ? TheScriptEngine->getCounter("WP_ObjectiveTarget") : nullptr;
	const TCounter *timer = inGame && TheScriptEngine ? TheScriptEngine->getCounter("WP_ObjectiveTimer") : nullptr;
	ScoreKeeper *score = player ? player->getScoreKeeper() : nullptr;
	// Emscripten's EM_ASM argument wrappers support a bounded argument list.
	// Pass a synchronous numeric snapshot plus the four strings instead of
	// dozens of scalar varargs; the browser copies every field before return.
	const double state[] = {
		double(inGame), double(inGame && TheGameLogic->isGamePaused()), double(frame),
		double(player ? player->getMoney()->countMoney() : 0),
		double(player ? player->getEnergy()->getProduction() : 0),
		double(player ? player->getEnergy()->getConsumption() : 0),
		double(units), double(structures), double(builders), double(production), double(income),
		double(selectedCount), double(health), double(maxHealth),
		double(stage ? stage->value : 0), double(progress ? progress->value : 0),
		double(target ? target->value : 0),
		double(timer ? (timer->value > 0 ? (timer->value + 29) / 30 : 0) : -1),
		double(score ? score->getTotalUnitsBuilt() : 0), double(score ? score->getTotalUnitsLost() : 0),
		double(score ? score->getTotalUnitsDestroyed() : 0), double(score ? score->getTotalBuildingsBuilt() : 0),
		double(score ? score->getTotalMoneyEarned() : 0), double(idleWorkers),
		double(TheScriptEngine ? (int)TheScriptEngine->getGlobalDifficulty() : 1),
		double(headquartersHealth), double(headquartersMaxHealth)
	};
	EM_ASM({
		if (!Module.onGameState) return;
		var base = $0 >> 3;
		var n = function(i) { return HEAPF64[base + i]; };
		Module.onGameState({version: 1, inGame: !!n(0), paused: !!n(1), map: UTF8ToString($1),
			operationId: $2 ? UTF8ToString($2) : null, frame: n(2), seconds: Math.floor(n(2) / 30),
			money: n(3), powerProduced: n(4), powerConsumed: n(5), units: n(6), structures: n(7),
			builders: n(8), productionBuildings: n(9), incomeBuildings: n(10),
			selected: {count: n(11), name: UTF8ToString($3), template: UTF8ToString($4), health: n(12), maxHealth: n(13)},
			objectiveStage: n(14), objectiveProgress: n(15), objectiveTarget: n(16), objectiveSeconds: n(17),
			unitsBuilt: n(18), unitsLost: n(19), unitsDestroyed: n(20), buildingsBuilt: n(21), moneyEarned: n(22),
			idleWorkers: n(23), difficulty: n(24), headquartersHealth: n(25), headquartersMaxHealth: n(26)});
	}, state, map.str(), wpOperationID(map), selectedName.str(), selectedTemplate.str());
#endif
}

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
	s_wpResultMap = wpCurrentMapID();
	const char *test = getenv("WP_AUTOTEST");
	if( test && (strcmp(test, "mission") == 0 || strncmp(test, "mission-defeat", 14) == 0) ) {
		const Bool expected = strcmp(test, "mission") == 0;
		const Bool valid = s_wpMissionDiagnosticArmed && victory == expected && victory == s_wpMissionDiagnosticExpectedWin;
		s_wpMissionDiagnosticResult = valid ? 1 : -1;
		fprintf(stderr, "[WP_AUTO] MISSION_RESULT %s expected=%s actual=%s armed=%d map=%s\n", valid ? "PASS" : "FAIL",
			expected ? "win" : "loss", victory ? "win" : "loss", (Int)s_wpMissionDiagnosticArmed, s_wpResultMap.str());
		s_wpMissionDiagnosticArmed = FALSE;
		fflush(stderr);
	}
	s_wpResultDifficulty = TheScriptEngine ? TheScriptEngine->getGlobalDifficulty() : DIFFICULTY_NORMAL;
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
	// Capture campaign progression before teardown; the native score screen
	// and web journal use the same outcome after player/score data is gone.
	EM_ASM({
		if (typeof Module !== 'undefined' && Module.onMatchResult)
			Module.onMatchResult({ map: UTF8ToString($7), operationId: $8 ? UTF8ToString($8) : null,
				won: $0 === 1, difficulty: $9, stats: {unitsBuilt: $1, unitsLost: $2,
				unitsDestroyed: $3, buildingsBuilt: $4, moneyEarned: $5, durationSeconds: $6},
				victory: $0 === 1, unitsBuilt: $1,
				unitsLost: $2, unitsDestroyed: $3, buildingsBuilt: $4,
				moneyEarned: $5, durationSeconds: $6 });
	}, s_wpResult.victory ? 1 : 0, s_wpResult.unitsBuilt, s_wpResult.unitsLost,
		s_wpResult.unitsDestroyed, s_wpResult.buildingsBuilt,
		s_wpResult.moneyEarned,
		(int)(s_wpResult.durationFrames / LOGICFRAMES_PER_SECOND), s_wpResultMap.str(),
		wpOperationID(s_wpResultMap), (int)s_wpResultDifficulty);
#endif
}

#ifdef __EMSCRIPTEN__
// ----------------------------------------------------------------------------
// Compatibility master-volume export. The web settings surface uses the
// independent master/channel API below for current builds.
// ----------------------------------------------------------------------------
extern "C" EMSCRIPTEN_KEEPALIVE void wpSetMasterVolume( int pct )
{
	if( pct < 0 ) pct = 0;
	if( pct > 100 ) pct = 100;
	if( TheAudio )
		TheAudio->setVolume( ((Real)pct) / 100.0f, (AudioAffect)AudioAffect_All );
}

// GeneralsX @feature Codex 05/09/2026 Settings use system channels so mission
// fades retain their independent script multipliers on every audio backend.
extern "C" EMSCRIPTEN_KEEPALIVE void wpSetAudioLevels( int master, int music, int effects, int voice )
{
	if( !TheAudio ) return;
	const int levels[] = { music, effects, voice };
	const int groups[] = { AudioAffect_Music, AudioAffect_Sound | AudioAffect_Sound3D, AudioAffect_Speech };
	master = master < 0 ? 0 : master > 100 ? 100 : master;
	for( int i = 0; i < 3; ++i )
	{
		int level = levels[i] < 0 ? 0 : levels[i] > 100 ? 100 : levels[i];
		TheAudio->setVolume( (Real)(master * level) / 10000.0f,
			(AudioAffect)(groups[i] | AudioAffect_SystemSetting) );
	}
}

// A web panel may release only the pause it acquired, preserving Escape/P.
extern "C" EMSCRIPTEN_KEEPALIVE int wpSetWebPause( int paused )
{
	if( !TheGameLogic || !TheGameLogic->isInGame() ) { s_wpWebOwnsPause = FALSE; return 0; }
	if( paused && !TheGameLogic->isGamePaused() )
	{
		TheGameLogic->setGamePaused( TRUE, FALSE, TRUE );
		s_wpWebOwnsPause = TRUE;
	}
	else if( !paused && s_wpWebOwnsPause )
	{
		TheGameLogic->setGamePaused( FALSE, FALSE, TRUE );
		s_wpWebOwnsPause = FALSE;
	}
	return TheGameLogic->isGamePaused() ? 1 : 0;
}

// Fixed local slot; the page owns IDBFS persistence and version metadata.
// Saving does not imply durable storage until the page's syncfs completes.
extern "C" EMSCRIPTEN_KEEPALIVE int wpSaveGame()
{
	if( !TheGameState || !TheGameLogic || !TheGameLogic->isInGame() ) return SC_INVALID;
	UnicodeString description;
	description.translate( "War Powers checkpoint" );
	return TheGameState->saveGame( "wp-checkpoint.sav", description, SAVE_FILE_TYPE_NORMAL );
}

extern "C" EMSCRIPTEN_KEEPALIVE int wpLoadGame()
{
	if( !TheGameState || !TheGameLogic || !TheGameState->doesSaveGameExist("wp-checkpoint.sav") )
		return SC_FILE_NOT_FOUND;
	AvailableGameInfo info;
	info.filename = "wp-checkpoint.sav";
	info.next = info.prev = nullptr;
	try
	{
		TheGameState->getSaveGameInfoFromFile( TheGameState->getFilePathInSaveDirectory(info.filename), &info.saveGameInfo );
	}
	catch( ... )
	{
		fprintf(stderr, "[WPSAVE] Invalid checkpoint metadata; the current match is unchanged.\n");
		return SC_INVALID_DATA;
	}
	destroyQuitMenu();
	if( !TheGameLogic->isInGame() )
		TheGameLogic->prepareNewGame( GAME_SINGLE_PLAYER, DIFFICULTY_NORMAL, 0 );
	s_wpWebOwnsPause = FALSE;
	SaveCode result = TheGameState->loadGame( info );
	if( result == SC_OK )
	{
		TheWritableGlobalData->m_breakTheMovie = FALSE;
		TheGameLogic->setGamePaused( FALSE );
		// A restored game starts the normal bar entrance animation. Complete it
		// before a web briefing can pause the engine with the bar below screen.
		if( TheControlBar ) {
			if( TheControlBar->m_animateWindowManager ) TheControlBar->m_animateWindowManager->reset();
			if( TheControlBar->m_animateWindowManagerForGenShortcuts ) TheControlBar->m_animateWindowManagerForGenShortcuts->reset();
			ShowControlBar( TRUE );
		}
	}
	else
	{
		if( TheGameLogic->isInGame() ) TheGameLogic->clearGameData( FALSE );
		TheGameEngine->reset();
		TheShell->showShell( TRUE );
	}
	return result;
}
#endif

// ----------------------------------------------------------------------------
// Shared: start a map through the command-line path (menu-mode: m_initialFile
// stays empty, so the post-match flow returns here instead of exiting).
// ----------------------------------------------------------------------------
// Opponent difficulty picked on the deployment screen (0/1/2 = the
// GameDifficulty enum). MSG_NEW_GAME's difficulty argument feeds
// prepareNewGame -> ScriptEngine::setGlobalDifficulty, which the AIPlayer
// ctor snapshots and the per-difficulty map scripts (easy/normal/hard flag
// bytes) key off. Restart Battle re-reads the live global, so the choice
// survives restarts for free.
static Int s_wpDiffIdx = DIFFICULTY_NORMAL;

static void wpStartMap( const char *mapPath )
{
	if( !mapPath || !*mapPath ) return;
	s_wpLastMapPath = mapPath;
	s_wpWebOwnsPause = FALSE;
	// WarPowers @debug IG_TRACE menu-start forensics
	static const bool wpTrace = getenv("IG_TRACE") && *getenv("IG_TRACE") != '0';
	if (wpTrace)
		fprintf(stderr, "[WPSHELL] startMap '%s' diff=%d\n", mapPath, (int)s_wpDiffIdx);
	TheWritableGlobalData->m_pendingFile = mapPath;
	TheWritableGlobalData->m_shellMapOn = FALSE;
	TheWritableGlobalData->m_playIntro = FALSE;

	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument( GAME_SINGLE_PLAYER );
	msg->appendIntegerArgument( (GameDifficulty)s_wpDiffIdx );
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
	if( layout == s_wpDeploymentLayout ) s_wpDeploymentLayout = nullptr;
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

extern Bool g_wpMenuCurtain;  // WarPowers @feature menu curtain

void WPMainMenuInit( WindowLayout *layout, void *userData )
{
	s_wpMainLayout = layout;
	// The menu is up — drop the exit curtain. Exception: with a match result
	// pending, the score screen is about to push over this menu; keep the
	// curtain up through the 1-3 frame menu flash so the player sees
	// banner -> black -> stats, not banner -> black -> menu blink -> stats.
	// (WPScoreInit drops it; WPMainMenuUpdate carries a failsafe.)
	if( !s_wpResult.valid )
		g_wpMenuCurtain = FALSE;
	// The intro render-freeze is normally cleared by the stock MainMenuInit;
	// this layout owns that job now (see Intro::doPostIntro).
	TheWritableGlobalData->m_breakTheMovie = FALSE;
	TheMouse->setVisibility( TRUE );

	wpButtonEngageID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonEngage" );
	wpButtonOptionsID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonOptions" );
	wpButtonQuitID = TheNameKeyGenerator->nameToKey( "MainMenu.wnd:ButtonQuit" );

	// GeneralsX @tweak Codex 05/09/2026 Settings exposes the staged build ID;
	// a compiler timestamp is redundant and does not identify the game pack.
	GameWindow *version = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( "MainMenu.wnd:LabelVersion" ) );
	if( version )
		GadgetStaticTextSetText( version, UnicodeString::TheEmptyString );

	layout->hide( FALSE );
	layout->bringForward();
}

void WPMainMenuUpdate( WindowLayout *layout, void *userData )
{
	if( s_wpOpenDeployment && TheShell && TheShell->top() == layout && !TheGameLogic->isInGame() )
	{
		s_wpOpenDeployment = FALSE;
		TheShell->push( "Menus/WPSkirmish.wnd" );
		return;
	}
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

		// Failsafe: the curtain stays up while the result is pending (see
		// WPMainMenuInit). If the score screen never arrives (missing
		// layout, wedged push), don't hold a black screen forever — give
		// up after ~2s of updates and show the menu.
		static Int s_wpScoreWaitTicks = 0;
		if( g_wpMenuCurtain )
		{
			if( ++s_wpScoreWaitTicks > 60 )
			{
				s_wpResult.valid = FALSE;
				g_wpMenuCurtain = FALSE;
				s_wpScoreWaitTicks = 0;
			}
		}
		else
			s_wpScoreWaitTicks = 0;
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

// Battlefield rotation: layouts from tools/genmap.py --layout=... . The
// picker cycles; the choice persists in the browser (localStorage 'wpMap').
struct WPMapEntry { const char *label; const char *desc; const char *mer; const char *jak; const char *preview; Int mode; };
static const WPMapEntry s_wpMaps[] = {
	{ "WP:MapFlats", "WP:MapFlatsDesc", "Maps\\WPTest\\WPTest.map", "Maps\\WPTestJ\\WPTestJ.map", "WPMapPreviewFlats", 0 },
	{ "WP:MapRidge", "WP:MapRidgeDesc", "Maps\\WPRidge\\WPRidge.map", "Maps\\WPRidgeJ\\WPRidgeJ.map", "WPMapPreviewRidge", 0 },
	{ "WP:MapScrap", "WP:MapScrapDesc", "Maps\\WPScrap\\WPScrap.map", "Maps\\WPScrapJ\\WPScrapJ.map", "WPMapPreviewScrap", 0 },
	{ "WP:MapBasin", "WP:MapBasinDesc", "Maps\\WPBasin\\WPBasin.map", "Maps\\WPBasinJ\\WPBasinJ.map", "WPMapPreviewBasin", 0 },
	{ "WP:MapRange", "WP:MapRangeDesc", "Maps\\WPRange\\WPRange.map", "Maps\\WPRangeJ\\WPRangeJ.map", "WPMapPreviewRange", 0 },
	{ "WP:MapTraining", "WP:MapTrainingDesc", "Maps\\WPTraining\\WPTraining.map", nullptr, "WPMapPreviewFlats", 1 },
	{ "WP:MapOp01", "WP:MapOp01Desc", "Maps\\WPOp01\\WPOp01.map", nullptr, "WPMapPreviewRidge", 2 },
	{ "WP:MapOp02", "WP:MapOp02Desc", nullptr, "Maps\\WPOp02\\WPOp02.map", "WPMapPreviewBasin", 2 },
	{ "WP:MapOp03", "WP:MapOp03Desc", "Maps\\WPOp03\\WPOp03.map", nullptr, "WPMapPreviewRange", 2 },
	{ "WP:MapOp04", "WP:MapOp04Desc", nullptr, "Maps\\WPOp04\\WPOp04.map", "WPMapPreviewScrap", 2 },
	{ "WP:MapChallengeM", "WP:MapChallengeMDesc", "Maps\\WPChallengeM\\WPChallengeM.map", nullptr, "WPMapPreviewRange", 3 },
	{ "WP:MapChallengeJ", "WP:MapChallengeJDesc", nullptr, "Maps\\WPChallengeJ\\WPChallengeJ.map", "WPMapPreviewRidge", 3 },
};
// The first five entries are skirmish layouts; the remaining order matches operations.json.
static const Int s_wpFirstMissionIndex = 5;
static Int s_wpMapIdx = 0;
static NameKeyType wpMapPrevID = NAMEKEY_INVALID;
static NameKeyType wpMapNextID = NAMEKEY_INVALID;
static NameKeyType wpModeIDs[4];
static const char *wpModeNames[] = {"Skirmish", "Training", "Operation", "Challenge"};

static void wpPersistMapChoice()
{
#ifdef __EMSCRIPTEN__
	EM_ASM({ try { localStorage.setItem('wpMap', String($0)); }
		catch (error) { console.warn('War Powers: deployment preference was not saved.'); } }, s_wpMapIdx);
#endif
}

struct WPDiffEntry { const char *label; const char *desc; };
static const WPDiffEntry s_wpDiffs[] = {
	{ "WP:DiffEasy",   "WP:DiffEasyDesc" },
	{ "WP:DiffNormal", "WP:DiffNormalDesc" },
	{ "WP:DiffHard",   "WP:DiffHardDesc" },
};
static NameKeyType wpDiffPrevID = NAMEKEY_INVALID;
static NameKeyType wpDiffNextID = NAMEKEY_INVALID;

static void wpSkirmishRefreshLabels( void )
{
	const WPMapEntry &entry = s_wpMaps[s_wpMapIdx];
	GameWindow *preview = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey("WPSkirmish.wnd:MapPreview") );
	if( preview && TheMappedImageCollection )
		preview->winSetEnabledImage( 0, TheMappedImageCollection->findImageByName(entry.preview) );
	GameWindow *meridian = TheWindowManager->winGetWindowFromId( nullptr, wpDeployMeridianID );
	GameWindow *jackal = TheWindowManager->winGetWindowFromId( nullptr, wpDeployJackalID );
	// GeneralsX @tweak Codex 05/09/2026 All scenarios are open; show only the factions authored for this map.
	if( meridian )
	{
		meridian->winHide( entry.mer == nullptr );
		meridian->winEnable( entry.mer != nullptr );
	}
	if( jackal )
	{
		jackal->winHide( entry.jak == nullptr );
		jackal->winEnable( entry.jak != nullptr );
	}
	GameWindow *meridianBrief = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey("WPSkirmish.wnd:MeridianBrief") );
	GameWindow *jackalBrief = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey("WPSkirmish.wnd:JackalBrief") );
	if( meridianBrief ) meridianBrief->winHide( entry.mer == nullptr );
	if( jackalBrief ) jackalBrief->winHide( entry.jak == nullptr );
	for( Int mode = 0; mode < 4; ++mode )
	{
		GameWindow *button = TheWindowManager->winGetWindowFromId( nullptr, wpModeIDs[mode] );
		if( button )
		{
			button->winSetDisabledColor( 0, GameMakeColor(37, 42, 43, 255) );
			Color activeText = GameMakeColor(225, 191, 107, 255);
			button->winSetDisabledTextColors( activeText, activeText );
			button->winEnable( mode != entry.mode );
		}
	}
	wpHudText( "WPSkirmish.wnd:ObjectiveHint", TheGameText->fetch(
		entry.mer && entry.jak ? "WP:HQObjective" :
		entry.mer ? "WP:ScenarioFactionMeridian" : "WP:ScenarioFactionJackal") );
	// GadgetStaticTextSetText, not winSetText: STATICTEXT caches its
	// render string in the gadget data - bare winSetText leaves the drawn
	// text stale/empty.
	GameWindow *w = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:MapName" ) );
	if (w)
		GadgetStaticTextSetText( w, TheGameText->fetch( s_wpMaps[s_wpMapIdx].label ) );
	w = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:MapDesc" ) );
	if (w)
		GadgetStaticTextSetText( w, TheGameText->fetch( s_wpMaps[s_wpMapIdx].desc ) );
	w = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:DiffName" ) );
	if (w)
		GadgetStaticTextSetText( w, TheGameText->fetch( s_wpDiffs[s_wpDiffIdx].label ) );
	w = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:DiffDesc" ) );
	if (w)
		GadgetStaticTextSetText( w, TheGameText->fetch( s_wpDiffs[s_wpDiffIdx].desc ) );
}

void WPSkirmishInit( WindowLayout *layout, void *userData )
{
	s_wpDeploymentLayout = layout;
	wpDeployMeridianID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonDeployMeridian" );
	wpDeployJackalID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonDeployJackal" );
	wpSkirmishBackID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonBack" );
	wpMapPrevID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonMapPrev" );
	wpMapNextID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonMapNext" );
	wpDiffPrevID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonDiffPrev" );
	wpDiffNextID = TheNameKeyGenerator->nameToKey( "WPSkirmish.wnd:ButtonDiffNext" );
	for( Int mode = 0; mode < 4; ++mode )
	{
		AsciiString name;
		name.format( "WPSkirmish.wnd:ButtonMode%s", wpModeNames[mode] );
		wpModeIDs[mode] = TheNameKeyGenerator->nameToKey(name);
	}

#if defined(__EMSCRIPTEN__)
	s_wpMapIdx = EM_ASM_INT({
		try { return Math.max(0, Math.min($0, parseInt(localStorage.getItem('wpMap') || '0', 10) || 0)); }
		catch (e) { return 0; }
	}, (int)(ARRAY_SIZE(s_wpMaps) - 1));
	s_wpDiffIdx = EM_ASM_INT({
		try { return Math.max(0, Math.min($0, parseInt(localStorage.getItem('wpDiff') || '1', 10) || 0)); }
		catch (e) { return 1; }
	}, (int)(ARRAY_SIZE(s_wpDiffs) - 1));
#endif
	layout->hide( FALSE );
	wpSkirmishRefreshLabels();
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
			for( Int mode = 0; mode < 4; ++mode )
				if( controlID == wpModeIDs[mode] )
				{
					for( Int i = 0; i < (Int)ARRAY_SIZE(s_wpMaps); ++i )
						if( s_wpMaps[i].mode == mode ) { s_wpMapIdx = i; break; }
					wpSkirmishRefreshLabels();
					wpPersistMapChoice();
					return MSG_HANDLED;
				}

			// Long map paths (Maps\<name>\<name>.map): TerrainLogic->loadMap
			// takes the real file path; the short form the -file flag accepts is
			// expanded by CommandLine.cpp's (file-static) converter, which this
			// path never runs through. Short form here = extent-0 empty world.
			if( controlID == wpDeployMeridianID && s_wpMaps[s_wpMapIdx].mer )
				wpStartMap( s_wpMaps[s_wpMapIdx].mer );
			else if( controlID == wpDeployJackalID && s_wpMaps[s_wpMapIdx].jak )
				wpStartMap( s_wpMaps[s_wpMapIdx].jak );
			else if( controlID == wpMapPrevID || controlID == wpMapNextID )
			{
				const Int n = (Int)ARRAY_SIZE(s_wpMaps);
				const Int mode = s_wpMaps[s_wpMapIdx].mode;
				do { s_wpMapIdx = (s_wpMapIdx + (controlID == wpMapNextID ? 1 : n - 1)) % n; }
				while( s_wpMaps[s_wpMapIdx].mode != mode );
				wpSkirmishRefreshLabels();
#if defined(__EMSCRIPTEN__)
				EM_ASM({ try { localStorage.setItem('wpMap', String($0)); } catch (e) {} },
					(int)s_wpMapIdx);
#endif
			}
			else if( controlID == wpDiffPrevID || controlID == wpDiffNextID )
			{
				const Int n = (Int)ARRAY_SIZE(s_wpDiffs);
				s_wpDiffIdx = (s_wpDiffIdx + (controlID == wpDiffNextID ? 1 : n - 1)) % n;
				wpSkirmishRefreshLabels();
#if defined(__EMSCRIPTEN__)
				EM_ASM({ try { localStorage.setItem('wpDiff', String($0)); } catch (e) {} },
					(int)s_wpDiffIdx);
#endif
			}
			else if( controlID == wpSkirmishBackID )
				TheShell->pop();
			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}

#ifdef __EMSCRIPTEN__
// The journal selects an engine deployment screen; it never starts a match
// behind an active battle. Index follows data/operations.json mission order.
extern "C" EMSCRIPTEN_KEEPALIVE int wpShowMission( int mission )
{
	if( mission < 0 || mission >= (int)ARRAY_SIZE(s_wpMaps) - s_wpFirstMissionIndex || !TheShell ||
		!TheGameLogic || TheGameLogic->isInGame() ) return 0;
	s_wpMapIdx = mission + s_wpFirstMissionIndex;
	wpPersistMapChoice();
	if( s_wpDeploymentLayout && TheShell->top() == s_wpDeploymentLayout ) wpSkirmishRefreshLabels();
	else
	{
		s_wpOpenDeployment = TRUE;
		if( TheShell->top() != s_wpMainLayout ) TheShell->pop();
	}
	return 1;
}
#endif

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
static NameKeyType wpScoreRetryID = NAMEKEY_INVALID;
static NameKeyType wpScoreChooseID = NAMEKEY_INVALID;

static void wpSetScoreLine( const char *winName, const UnicodeString &text )
{
	GameWindow *win = TheWindowManager->winGetWindowFromId( nullptr,
		TheNameKeyGenerator->nameToKey( winName ) );
	if( win )
		GadgetStaticTextSetText( win, text );
}

void WPScoreInit( WindowLayout *layout, void *userData )
{
	// Stats are on screen — drop the match-exit curtain (kept up through the
	// underlying menu's init when a result was pending).
	g_wpMenuCurtain = FALSE;

	wpScoreContinueID = TheNameKeyGenerator->nameToKey( "WPScore.wnd:ButtonContinue" );
	wpScoreRetryID = TheNameKeyGenerator->nameToKey( "WPScore.wnd:ButtonRetry" );
	wpScoreChooseID = TheNameKeyGenerator->nameToKey( "WPScore.wnd:ButtonChoose" );

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
			else if( control->winGetWindowId() == wpScoreRetryID )
			{
				AsciiString path;
				path.format( "Maps\\%s\\%s.map", s_wpResultMap.str(), s_wpResultMap.str() );
				s_wpDiffIdx = s_wpResultDifficulty;
				wpStartMap( path.str() );
			}
			else if( control->winGetWindowId() == wpScoreChooseID )
			{
				s_wpOpenDeployment = TRUE;
				TheShell->pop();
			}
			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}
