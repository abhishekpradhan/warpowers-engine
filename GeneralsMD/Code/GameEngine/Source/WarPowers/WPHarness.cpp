// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 The War Powers authors
//
// War Powers autotest / clicktest harness.
//
// Developer-only scripted playtests. Every mode drives a running match through
// the same message stream and UI paths a human uses (selection groups, queue
// and construct messages, raw mouse messages, menu button notifications) and
// prints [WP_AUTO] / [WP_CLICK] verdicts to stderr. Nothing here runs unless
// the matching environment variable is set, and the whole translation unit is
// compiled only when the CMake option WP_HARNESS is ON (-DWP_HARNESS=ON or the
// `wasm-harness` preset). The production build never contains it.
//
// WarPowers @refactor 07/09/2026 Extracted from GameEngine::update() and
// SDL3GameEngine::update() so the shipped engine tick carries no test-harness
// state machines. See WarPowers/WPHarness.h for the env vars and modes.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "WarPowers/WPHarness.h"

#include "Common/BuildAssistant.h"
#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/Display.h"
#include "GameClient/Drawable.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Shell.h"
#include "GameClient/View.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Scripts.h"
#include "GameLogic/TerrainLogic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "WPTrace.h"  // wpEnvEnabled(): the WP_* switch semantics

#include "GameClient/WPShell.h"  // WPArmMissionDiagnosticResult / WPGetMissionDiagnosticResult (mission lab)

namespace
{

//-------------------------------------------------------------------------------------------------
// WP_AUTOTEST=retry
//-------------------------------------------------------------------------------------------------

// WarPowers @feature 05/09/2026 Repeat the real War Powers score Retry
// lifecycle with paid production, supply deliveries and changing HUD text.
// This diagnostic is dataset-specific and never runs during ordinary play.
void updateRetryDiagnostic()
{
	const char* mode = getenv("WP_AUTOTEST");
	if (!mode || strcmp(mode, "retry") != 0) return;
	enum Phase { START_MENU, SELECT_TRAINING, CONFIRM_DEPLOY, WAIT_MATCH,
		PLAY_MATCH, WAIT_SCORE, SCORE_DWELL, STOPPED };
	static Phase phase = START_MENU;
	if (phase == STOPPED) return;
	auto boundedSetting = [](const char* name, Int fallback, Int minimum, Int maximum) -> Int {
		const char* text = getenv(name);
		if (!text || !*text) return fallback;
		char* end = nullptr;
		const long value = strtol(text, &end, 10);
		if (end == text || *end) return fallback;
		return value < minimum ? minimum : value > maximum ? maximum : (Int)value;
	};
	static const Int runs = boundedSetting("WP_RETRY_RUNS", 3, 1, 30);
	static const UnsignedInt matchFrames = (UnsignedInt)boundedSetting("WP_RETRY_FRAMES", 5400, 900, 54000);
	static UnsignedInt ticks = 0, phaseTick = 0, taskFrame = 0, readyFrame = 0;
	static UnsignedInt lastFrame = 0, lastFrameTick = 0, selectionFrame = 0, heartbeatFrame = 0;
	static Int completed = 0, taskIndex = 0, selectionIndex = 0;
	static Int initialBoxes = 0, maxCargo = 0;
	static ObjectID cacheID = INVALID_ID;
	static ObjectID uiSelectionID = INVALID_ID;
	static UnsignedInt uiSelectionMask = 0, pendingSelectionMask = 0;
	static Bool taskIssued = FALSE, ready = FALSE, injectedLoss = FALSE, supplyDelivered = FALSE;
	static Coord3D home = { 0, 0, 0 };
	++ticks;
	if (ticks == 1)
	{
		fprintf(stderr, "[WP_AUTO] RETRY_CONFIG runs=%d frames=%u; normal menu callbacks, paid construction/production; no direct reset or objective mutation\n", runs, matchFrames);
		fflush(stderr);
	}
	const Bool inGame = TheGameLogic && TheGameLogic->isInGame();
	const UnsignedInt frame = inGame ? TheGameLogic->getFrame() : 0;
	auto finish = [&](Bool passed, const char* detail) {
		fprintf(stderr, "[WP_AUTO] RETRY %s completed=%d/%d phase=%d frame=%u: %s\n",
			passed ? "PASS" : "FAIL", completed, runs, (Int)phase, frame, detail);
		fflush(stderr);
		phase = STOPPED;
		// Hold the final scene for inspection instead of quitting the browser or
		// letting the diagnostic continue changing the world after its verdict.
		if (inGame) TheGameLogic->setGamePaused(TRUE, FALSE, TRUE);
	};
	auto transition = [&](Phase next) { phase = next; phaseTick = ticks; };
	auto window = [](const char* name) -> GameWindow* {
		return TheWindowManager && TheNameKeyGenerator ? TheWindowManager->winGetWindowFromId(nullptr,
			TheNameKeyGenerator->nameToKey(name)) : nullptr;
	};
	auto click = [&](const char* name) -> Bool {
		GameWindow* button = window(name);
		if (!button || button->winIsHidden() || !BitIsSet(button->winGetStatus(), WIN_STATUS_ENABLED) || !button->winGetOwner()) return FALSE;
		fprintf(stderr, "[WP_AUTO] RETRY_CLICK run=%d button=%s\n", completed + 1, name);
		fflush(stderr);
		// GadgetPushButtonInput sends this same notification to this owner.
		TheWindowManager->winSendSystemMsg(button->winGetOwner(), GBM_SELECTED, (WindowMsgData)button, 0);
		return TRUE;
	};
	auto labelEquals = [&](const char* name, const char* key) -> Bool {
		GameWindow* label = window(name);
		return label && TheGameText && GadgetStaticTextGetText(label) == TheGameText->fetch(key);
	};
	if (phase != PLAY_MATCH && ticks - phaseTick > 1800)
	{
		finish(FALSE, "menu/load/result transition timed out after 1800 client updates");
		return;
	}
	if (phase == START_MENU)
	{
		if (inGame) { finish(FALSE, "start from the normal main menu, without a map URL or -file argument"); return; }
		if (click("MainMenu.wnd:ButtonEngage")) transition(SELECT_TRAINING);
		return;
	}
	if (phase == SELECT_TRAINING)
	{
		if (labelEquals("WPSkirmish.wnd:MapName", "WP:MapTraining")) transition(CONFIRM_DEPLOY);
		else if (click("WPSkirmish.wnd:ButtonModeTraining")) transition(CONFIRM_DEPLOY);
		return;
	}
	if (phase == CONFIRM_DEPLOY)
	{
		if (!labelEquals("WPSkirmish.wnd:MapName", "WP:MapTraining")) { finish(FALSE, "deployment did not select Field Orientation"); return; }
		if (!labelEquals("WPSkirmish.wnd:DiffName", "WP:DiffNormal"))
		{
			if (ticks % 30 == 0) click("WPSkirmish.wnd:ButtonDiffNext");
			return;
		}
		if (click("WPSkirmish.wnd:ButtonDeployMeridian")) transition(WAIT_MATCH);
		return;
	}
	if (phase == WAIT_SCORE || phase == SCORE_DWELL)
	{
		if (inGame) return; // The map's DEFEAT banner/exit timer still owns the transition.
		GameWindow* score = window("WPScore.wnd:ScoreParent");
		if (!score || score->winIsHidden()) return;
		if (!labelEquals("WPScore.wnd:ResultBanner", "WP:Defeat")) { finish(FALSE, "native score screen did not report defeat"); return; }
		if (phase == WAIT_SCORE)
		{
			fprintf(stderr, "[WP_AUTO] RETRY_RESULT run=%d result=DEFEAT source=native-score loss=%s; warming score text before Retry\n",
				completed + 1, injectedLoss ? "injected-hq-damage" : "natural-combat");
			fflush(stderr);
			transition(SCORE_DWELL);
		}
		else if (ticks - phaseTick >= 120 && click("WPScore.wnd:ButtonRetry"))
		{
			++completed;
			transition(WAIT_MATCH);
		}
		return;
	}
	if (!inGame)
	{
		if (phase == PLAY_MATCH) finish(FALSE, "match exited before a verified headquarters loss");
		return;
	}
	if (!ThePlayerList || !TheThingFactory || !TheTerrainLogic || !TheScriptEngine) return;
	Player* owner = ThePlayerList->getLocalPlayer();
	if (!owner) { finish(FALSE, "training has no local player"); return; }
	auto find = [&](const char* name, Bool completedOnly) -> Object* {
		for (Object* object = TheGameLogic->getFirstObject(); object; object = object->getNextObject())
			if (object->getControllingPlayer() == owner && object->getTemplate()->getName() == name &&
				!object->isEffectivelyDead() && (!completedOnly || !object->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION))) return object;
		return nullptr;
	};
	auto count = [&](const char* name) -> Int {
		Int result = 0;
		for (Object* object = TheGameLogic->getFirstObject(); object; object = object->getNextObject())
			if (object->getControllingPlayer() == owner && object->getTemplate()->getName() == name &&
				!object->isEffectivelyDead() && !object->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION)) ++result;
		return result;
	};
	auto select = [&](Object* object) -> Bool {
		if (!object || !object->getDrawable() || !TheInGameUI || !TheMessageStream) return FALSE;
		pendingSelectionMask = 0;
		// Match SelectionXlat's single-selection path: the local drawable selection
		// drives displayed text, while the group message drives game-logic orders.
		TheInGameUI->deselectAllDrawables();
		TheInGameUI->selectDrawable(object->getDrawable());
		if (TheInGameUI->getSelectCount() != 1 || TheInGameUI->getFirstSelectedDrawable() != object->getDrawable()) return FALSE;
		GameMessage* message = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
		message->appendBooleanArgument(TRUE);
		message->appendObjectIDArgument(object->getID());
		uiSelectionID = object->getID();
		return TRUE;
	};
	const char* selections[] = { "WP_CommandCenter", "WP_Fabricator", "WP_Exchange", "WP_Porter",
		"WP_VehiclePlant", "WP_Tank", "WP_Vigil", "WP_Directorate" };
	const UnsignedInt allSelectionMask = (1U << ARRAY_SIZE(selections)) - 1;
	Object* uiObject = TheGameLogic->findObjectByID(uiSelectionID);
	if (pendingSelectionMask && uiObject && !uiObject->isEffectivelyDead() && TheInGameUI &&
		TheInGameUI->getSelectCount() == 1 && TheInGameUI->getFirstSelectedDrawable() == uiObject->getDrawable())
	{
		// Observe the real selection on a later client update, after the HUD can
		// consume it. Readiness requires every roster category in the churn loop.
		uiSelectionMask |= pendingSelectionMask;
		pendingSelectionMask = 0;
	}
	const TCounter* objective = TheScriptEngine->getCounter("WP_ObjectiveStage");
	Object* headquarters = find("WP_CommandCenter", TRUE);
	if (phase == WAIT_MATCH)
	{
		if (TheGameLogic->isLoadingMap() || frame < 90) return;
		GameWindow* bar = window("ControlBar.wnd:ControlBarParent");
		if (!strstr(TheGameState->getPristineMapName().str(), "WPTraining") || !headquarters ||
			!objective || objective->value != 0 || frame > 300 || !bar || bar->winIsHidden() ||
			TheGlobalData->m_loadScreenRender || TheGlobalData->m_breakTheMovie)
		{
			finish(FALSE, "fresh training map/stage/headquarters/visible HUD did not recover after deployment");
			return;
		}
		if (!TheInGameUI || TheInGameUI->getSelectCount() != 1 || TheInGameUI->getFirstSelectedDrawable() != headquarters->getDrawable())
		{
			if (!select(headquarters)) finish(FALSE, "fresh training headquarters cannot be selected by the native UI");
			return;
		}
		if (completed == runs) { finish(TRUE, "every native defeat/score Retry reached a fresh training battlefield with native headquarters selection; final scene paused"); return; }
		home = *headquarters->getPosition(); taskIndex = 0; selectionIndex = 0;
		taskIssued = FALSE; ready = FALSE; injectedLoss = FALSE; readyFrame = 0; taskFrame = frame;
		selectionFrame = heartbeatFrame = 0; lastFrame = frame; lastFrameTick = ticks;
		uiSelectionMask = pendingSelectionMask = 0;
		maxCargo = 0; supplyDelivered = FALSE; cacheID = INVALID_ID;
		for (Object* object = TheGameLogic->getFirstObject(); object; object = object->getNextObject())
			if (object->getName() == "HomeSupplyA") cacheID = object->getID();
		Object* cache = TheGameLogic->findObjectByID(cacheID);
		SupplyWarehouseDockUpdate* dock = cache ? static_cast<SupplyWarehouseDockUpdate*>(cache->findUpdateModule(NAMEKEY("SupplyWarehouseDockUpdate"))) : nullptr;
		if (!dock || dock->getBoxesStored() <= 0) { finish(FALSE, "training home supply cache is missing or empty"); return; }
		initialBoxes = dock->getBoxesStored();
		fprintf(stderr, "[WP_AUTO] RETRY_MATCH run=%d/%d frame=%u stage=0; beginning actual training economy and army\n", completed + 1, runs, frame);
		fflush(stderr);
		transition(PLAY_MATCH);
	}
	if (frame != lastFrame) { lastFrame = frame; lastFrameTick = ticks; }
	if (ticks - lastFrameTick > 1800) { finish(FALSE, "game logic stopped advancing during the stress match"); return; }
	if (!headquarters)
	{
		if (!ready) finish(FALSE, "headquarters lost before the production and stage-four setup completed");
		else transition(WAIT_SCORE);
		return;
	}
	if (!supplyDelivered)
	{
		Object* porter = find("WP_Porter", TRUE);
		SupplyTruckAIInterface* truck = porter && porter->getAIUpdateInterface() ? porter->getAIUpdateInterface()->getSupplyTruckAIInterface() : nullptr;
		Object* cache = TheGameLogic->findObjectByID(cacheID);
		SupplyWarehouseDockUpdate* dock = cache ? static_cast<SupplyWarehouseDockUpdate*>(cache->findUpdateModule(NAMEKEY("SupplyWarehouseDockUpdate"))) : nullptr;
		const Int cargo = truck ? truck->getNumberBoxes() : 0;
		if (cargo > maxCargo) maxCargo = cargo;
		if (truck && dock && maxCargo > 0 && initialBoxes - dock->getBoxesStored() > cargo)
		{
			supplyDelivered = TRUE;
			fprintf(stderr, "[WP_AUTO] RETRY_SUPPLY run=%d frame=%u stockRemoved=%d cargo=%d maxCargo=%d; actual hauler delivery observed\n",
				completed + 1, frame, initialBoxes - dock->getBoxesStored(), cargo, maxCargo);
			fflush(stderr);
		}
	}
	struct Task { const char* name; const char* producer; Int count; Bool structure; Real dx; Real dy; };
	static const Task tasks[] = {
		{ "WP_Fabricator", "WP_CommandCenter", 1, FALSE, 0, 0 },
		{ "WP_Exchange", "WP_Fabricator", 1, TRUE, 120, -130 },
		{ "WP_PowerArray", "WP_Fabricator", 1, TRUE, -75, 75 },
		{ "WP_Porter", "WP_Exchange", 1, FALSE, 0, 0 },
		{ "WP_VehiclePlant", "WP_Fabricator", 1, TRUE, 105, 105 },
		{ "WP_Tank", "WP_VehiclePlant", 2, FALSE, 0, 0 },
		{ "WP_Vigil", "WP_CommandCenter", 1, FALSE, 0, 0 },
		{ "WP_Directorate", "WP_Fabricator", 1, TRUE, -125, -100 }
	};
	if (!ready)
	{
		if (frame - taskFrame > 2700) { finish(FALSE, "training construction/production prerequisite stalled for 90 game seconds"); return; }
		if (taskIndex < (Int)ARRAY_SIZE(tasks))
		{
			const Task& task = tasks[taskIndex];
			const Int finished = count(task.name);
			if (finished >= task.count)
			{
				fprintf(stderr, "[WP_AUTO] RETRY_TASK run=%d frame=%u template=%s complete=%d stage=%d\n",
					completed + 1, frame, task.name, finished, objective ? objective->value : -1);
				fflush(stderr);
				++taskIndex; taskIssued = FALSE; taskFrame = frame;
			}
			else if (!taskIssued)
			{
				Object* producer = find(task.producer, TRUE);
				const ThingTemplate* thing = TheThingFactory->findTemplate(task.name);
				if (!producer || !thing) { finish(FALSE, "missing training producer/template"); return; }
				if (owner->getMoney()->countMoney() < thing->calcCostToBuild(owner) * (task.count - finished)) return;
				if (!select(producer)) { finish(FALSE, "native producer drawable selection failed"); return; }
				if (task.structure)
				{
					Coord3D position = home; position.x += task.dx; position.y += task.dy;
					position.z = TheTerrainLogic->getGroundHeight(position.x, position.y);
					GameMessage* message = TheMessageStream->appendMessage(GameMessage::MSG_DOZER_CONSTRUCT);
					message->appendIntegerArgument(thing->getTemplateID());
					message->appendLocationArgument(position); message->appendRealArgument(0.0f);
				}
				else for (Int i = finished; i < task.count; ++i)
				{
					GameMessage* message = TheMessageStream->appendMessage(GameMessage::MSG_QUEUE_UNIT_CREATE);
					message->appendIntegerArgument(thing->getTemplateID()); message->appendIntegerArgument(1);
				}
				fprintf(stderr, "[WP_AUTO] RETRY_ORDER run=%d frame=%u template=%s count=%d path=%s\n",
					completed + 1, frame, task.name, task.count - finished, task.structure ? "native-dozer" : "native-production");
				fflush(stderr);
				taskIssued = TRUE;
				return;
			}
		}
		else if (objective && objective->value == 4 && supplyDelivered && uiSelectionMask == allSelectionMask)
		{
			ready = TRUE; readyFrame = frame;
			fprintf(stderr, "[WP_AUTO] RETRY_READY run=%d frame=%u stage=4 money=%d uiSelectionMask=0x%x; supply, army and Directorate are real completed objects with observed native UI selection\n",
				completed + 1, frame, owner->getMoney()->countMoney(), uiSelectionMask);
			fflush(stderr);
		}
	}
	// Select different real objects so native names, health, orders, production,
	// money and recharge displays create/release their normal text resources.
	if (frame % 30 == 0 && selectionFrame != frame)
	{
		selectionFrame = frame;
		const Int index = selectionIndex++ % ARRAY_SIZE(selections);
		Object* object = find(selections[index], TRUE);
		if (object)
		{
			if (!select(object)) { finish(FALSE, "native drawable selection failed during HUD churn"); return; }
			pendingSelectionMask = 1U << index;
		}
	}
	if (frame % 900 == 0 && heartbeatFrame != frame)
	{
		heartbeatFrame = frame;
		fprintf(stderr, "[WP_AUTO] RETRY_HEARTBEAT run=%d frame=%u task=%d stage=%d money=%d uiSelectionMask=0x%x uiSelected=%d\n",
			completed + 1, frame, taskIndex, objective ? objective->value : -1, owner->getMoney()->countMoney(),
			uiSelectionMask, TheInGameUI ? TheInGameUI->getSelectCount() : 0);
		fflush(stderr);
	}
	if (ready && frame >= matchFrames && frame - readyFrame >= 300)
	{
		fprintf(stderr, "[WP_AUTO] RETRY_LOSS run=%d frame=%u injecting lethal headquarters damage; awaiting unmodified WP_Lose script/result\n", completed + 1, frame);
		fflush(stderr);
		injectedLoss = TRUE;
		headquarters->kill();
		transition(WAIT_SCORE);
	}
}

//-------------------------------------------------------------------------------------------------
// WP_AUTOTEST=<every other mode>
//-------------------------------------------------------------------------------------------------

// Which WP_AUTOTEST mode is active. Resolved once, on the first update.
struct AutotestMode
{
	const char* env;
	Bool active;          // any WP_AUTOTEST value except "retry" (which has its own diagnostic)
	Bool buildOnly;       // build: stop once the tank spawns, leaving move/attack to a human
	Bool economy;         // economy: actual supply transfers, isolated from construction
	Bool powers;          // powers: special-power effects, isolated from targeting tests
	Bool mission;         // mission: authored-map regression lab
	Bool missionDefeat;   // mission-defeat[-hq]: the lab expects the defeat result
	Bool base;            // base: D016 dozer loop
	Bool wedge;           // wedge: base with the second construct order injected at ~88%
	Bool strike;          // strike: deterministic building kill for kill-credit scoring
	Bool ghost;           // ghost: fog-memory lifecycle
	Bool husk;            // husk: translucent-remnant repro
	Bool defeat;          // defeat: kill the player CC (WP_Lose -> Defeat.wnd)
	Bool win;             // win: kill the enemy CC (WP_Win -> victory -> score screen)
	Bool cycle;           // cycle: quit match 1, redeploy, log display state through match 2
};

AutotestMode resolveAutotestMode()
{
	AutotestMode mode;
	memset(&mode, 0, sizeof(mode));
	const char* env = getenv("WP_AUTOTEST");
	auto is = [&](const char* name) -> Bool { return env && strcmp(env, name) == 0; };
	mode.env = env;
	mode.active = env && strcmp(env, "retry") != 0;
	mode.buildOnly = is("build");
	mode.economy = is("economy");
	mode.powers = is("powers");
	mode.mission = is("mission");
	mode.missionDefeat = env && strncmp(env, "mission-defeat", 14) == 0;
	mode.base = is("base");
	mode.wedge = is("wedge");
	mode.strike = is("strike");
	mode.ghost = is("ghost");
	mode.husk = is("husk");
	mode.defeat = is("defeat");
	mode.win = is("win");
	mode.cycle = is("cycle");
	return mode;
}

// State shared across the lab modes. One stage counter serves every mode
// because a process only ever runs a single WP_AUTOTEST value; the per-mode
// details stay function-local statics inside their own update function.
struct LabState
{
	UnsignedInt stage;
	UnsignedInt frame;      // TheGameLogic->getFrame() for the current update
	Int localIdx;           // local player index for the current update
	Int cycleStage;
	ObjectID ccId;
	ObjectID enemyCcId;
	ObjectID tankId;
	ObjectID fleet[4];
	Coord3D ccPos;
	UnsignedInt lastStatus;
};

LabState s_lab = { 0, 0, -1, 0, INVALID_ID, INVALID_ID, INVALID_ID,
	{ INVALID_ID, INVALID_ID, INVALID_ID, INVALID_ID }, { 0, 0, 0 }, 0 };

// WarPowers @feature 05/09/2026 Unit/control labs use an
// explicit production fixture now that real tanks require a
// factory. The base/wedge labs still exercise dozer construction.
const char* labTankName()
{
	Object* cc = TheGameLogic->findObjectByID(s_lab.ccId);
	return cc && cc->getTemplate()->getName() == "WPJ_CommandPost" ? "WPJ_Mongrel" : "WP_Tank";
}

void prepareUnitLab()
{
	Player* owner = ThePlayerList->getLocalPlayer();
	if (!owner) return;
	const Bool jackal = strcmp(labTankName(), "WPJ_Mongrel") == 0;
	const char* fixtures[] = { jackal ? "WPJ_ChopShop" : "WP_VehiclePlant", jackal ? nullptr : "WP_PowerArray" };
	for (Int fi = 0; fi < 2; ++fi) {
		if (!fixtures[fi]) continue;
		Bool exists = FALSE;
		for (Object* object = TheGameLogic->getFirstObject(); object; object = object->getNextObject())
			if (object->getControllingPlayer() == owner && object->getTemplate()->getName() == fixtures[fi]) { exists = TRUE; break; }
		if (exists) continue;
		const ThingTemplate* fixture = TheThingFactory->findTemplate(fixtures[fi]);
		Object* object = fixture ? TheThingFactory->newObject(fixture, owner->getDefaultTeam()) : nullptr;
		if (!object) continue;
		Coord3D position = s_lab.ccPos;
		position.x += 130.0f + 90.0f * fi;
		position.y -= 100.0f;
		position.z = TheTerrainLogic->getGroundHeight(position.x, position.y);
		object->setPosition(&position);
		TheAI->pathfinder()->addObjectToPathfindMap(object);
		fprintf(stderr, "[WP_AUTO] UNIT_LAB_FIXTURE %s (construction tested by base/wedge)\n", fixtures[fi]);
	}
}

// WP_AUTOTEST=cycle: play match 1 briefly, quit to the shell,
// redeploy the same map (the WPShell start path), and print
// display-state diagnostics through match 2 — headless repro
// for the second-match-black-screen bug. Pair with
// WP_SCENE_DUMP=+N for a per-match scene census.
void updateCycleOutOfGame()
{
	Int& wp_cycleStage = s_lab.cycleStage;
	if (wp_cycleStage == 1 && TheShell && TheShell->top())
	{
		fprintf(stderr, "[WP_AUTO] CYCLE: shell is back, redeploying match 2\n");
		fflush(stderr);
		TheWritableGlobalData->m_pendingFile = "Maps\\WPTest\\WPTest.map";
		TheWritableGlobalData->m_shellMapOn = FALSE;
		GameMessage *wp_msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
		wp_msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
		wp_msg->appendIntegerArgument(DIFFICULTY_NORMAL);
		wp_msg->appendIntegerArgument(0);
		InitRandom(0);
		wp_cycleStage = 2;
	}
}

void updateCycleInGame()
{
	Int& wp_cycleStage = s_lab.cycleStage;
	const UnsignedInt wp_f = s_lab.frame;
	if (wp_cycleStage == 0 && wp_f >= 300)
	{
		// quit() with no quit menu up just OPENS the menu
		// (canOpenQuitMenu guard); the second call passes
		// the guard and actually exits — same net effect
		// as the player's Abandon.
		fprintf(stderr, "[WP_AUTO] CYCLE: opening quit menu at f=%u\n", wp_f);
		fflush(stderr);
		TheGameLogic->quit(FALSE);
		TheGameLogic->quit(FALSE);
		fprintf(stderr, "[WP_AUTO] CYCLE: quit issued\n");
		fflush(stderr);
		wp_cycleStage = 1;
	}
	else if (wp_cycleStage == 2 && wp_f >= 60 && (wp_f % 120) == 0 && wp_f <= 720)
	{
		GameWindow* wp_cb = TheWindowManager ? TheWindowManager->winGetWindowFromId(nullptr,
			TheNameKeyGenerator->nameToKey("ControlBar.wnd:ControlBarParent")) : nullptr;
		Int wp_cbx = -1, wp_cby = -1; Bool wp_cbHidden = TRUE;
		if (wp_cb) { wp_cb->winGetPosition(&wp_cbx, &wp_cby); wp_cbHidden = wp_cb->winIsHidden(); }
		Coord3D wp_cam = {0,0,0};
		if (TheTacticalView) wp_cam = TheTacticalView->get3DCameraPosition();
		fprintf(stderr, "[WP_AUTO] CYCLE2 f=%u loadScrRender=%d breakMovie=%d cbHidden=%d cbPos=(%d,%d) cam=(%.0f,%.0f,%.0f)\n",
			wp_f, (int)TheGlobalData->m_loadScreenRender, (int)TheGlobalData->m_breakTheMovie,
			(int)wp_cbHidden, wp_cbx, wp_cby, wp_cam.x, wp_cam.y, wp_cam.z);
		fflush(stderr);
	}
}

// WarPowers @feature 05/09/2026 Authored-map regression lab.
// Fixtures bypass construction/combat/time, but never set objective
// counters or dispatch victory/defeat. The shipped map scripts must
// advance stages and deliver the normal match-result transition.
void updateMissionLab(const AutotestMode& mode)
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	static UnsignedInt nextCheck = 90;
	static UnsignedInt resultDeadline = 0;
	static Int fixtureIndex = 0;
	Player* owner = ThePlayerList->getLocalPlayer();
	const AsciiString map = TheGameState->getPristineMapName();
	auto counterValue = [&](const char* name) -> Int {
		const TCounter* value = TheScriptEngine->getCounter(name);
		return value ? value->value : -999;
	};
	auto named = [&](const char* name) -> Object* {
		for (Object* obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
			if (obj->getName() == name && !obj->isEffectivelyDead()) return obj;
		return nullptr;
	};
	auto check = [&](Bool valid, const char* detail) -> Bool {
		fprintf(stderr, "[WP_AUTO] MISSION_CHECK %s map=%s phase=%u stage=%d progress=%d: %s\n",
			valid ? "PASS" : "FAIL", map.str(), wp_stage,
			counterValue("WP_ObjectiveStage"), counterValue("WP_ObjectiveProgress"), detail);
		fflush(stderr);
		if (!valid) wp_stage = 99;
		return valid;
	};
	auto fixture = [&](const char* name) {
		Object* cc = named("PlayerCC");
		const ThingTemplate* tt = TheThingFactory->findTemplate(name);
		Object* obj = cc && tt && owner ? TheThingFactory->newObject(tt, owner->getDefaultTeam()) : nullptr;
		if (!check(obj != nullptr, "create explicit prerequisite fixture")) return;
		Coord3D position = *cc->getPosition();
		position.x += 75.0f + (fixtureIndex % 3) * 60.0f;
		position.y -= 90.0f + (fixtureIndex / 3) * 45.0f;
		position.z = TheTerrainLogic->getGroundHeight(position.x, position.y);
		++fixtureIndex;
		obj->setPosition(&position);
		for (BehaviorModule** module = obj->getBehaviorModules(); *module; ++module)
			if ((*module)->getCreate()) (*module)->getCreate()->onBuildComplete();
		TheAI->pathfinder()->addObjectToPathfindMap(obj);
		fprintf(stderr, "[WP_AUTO] MISSION_FIXTURE template=%s id=%u construction=bypassed\n", name, (unsigned)obj->getID());
		fflush(stderr);
	};
	auto destroyTarget = [&](const char* name) {
		Object* target = named(name);
		if (!check(target != nullptr, "named target exists before injected lethal damage")) return;
		fprintf(stderr, "[WP_AUTO] MISSION_FIXTURE destroy=%s combat=bypassed\n", name);
		fflush(stderr);
		target->kill();
	};
	auto expireTimer = [&]() {
		if (!check(counterValue("WP_ObjectiveTimer") > 0, "authored countdown was running")) return;
		ScriptAction* action = newInstance(ScriptAction)(ScriptAction::SET_MILLISECOND_TIMER);
		action->setNextAction(nullptr);
		action->getParameter(0)->friend_setString("WP_ObjectiveTimer");
		action->getParameter(1)->friend_setReal(0.0f);
		TheScriptEngine->friend_executeAction(action);
		deleteInstance(action);
		fprintf(stderr, "[WP_AUTO] MISSION_FIXTURE expire=WP_ObjectiveTimer elapsed-time=bypassed\n");
		fflush(stderr);
	};
	auto expectResult = [&](const char* result) {
		if (wp_stage == 99) return;
		WPArmMissionDiagnosticResult(strcmp(result, "victory") == 0);
		resultDeadline = wp_f + 150;
		fprintf(stderr, "[WP_AUTO] MISSION_EXPECT result=%s map=%s; awaiting actual map-script match result\n", result, map.str());
		fflush(stderr);
		wp_stage = 90;
	};
	if (wp_stage < 99 && wp_f >= nextCheck)
	{
		nextCheck = wp_f + 60;
		const Bool training = strstr(map.str(), "WPTraining") != nullptr;
		const Bool first = strstr(map.str(), "WPOp01") != nullptr;
		const Bool sabotage = strstr(map.str(), "WPOp02") != nullptr;
		const Bool hold = strstr(map.str(), "WPOp03") != nullptr;
		const Bool finale = strstr(map.str(), "WPOp04") != nullptr;
		const Bool rampart = strstr(map.str(), "WPChallengeM") != nullptr;
		const Bool takeover = strstr(map.str(), "WPChallengeJ") != nullptr;
		const Int observedResult = WPGetMissionDiagnosticResult();
		if (observedResult != 0)
		{
			check(observedResult == 1, "actual native match result observed");
			wp_stage = 99;
		}
		else if (wp_stage == 90)
		{
			if (wp_f >= resultDeadline) check(FALSE, "map did not produce its expected result within five seconds");
		}
		else if (!check(training || first || sabotage || hold || finale || rampart || takeover, "supported authored mission")) {}
		else if (wp_stage == 0 && !check(counterValue("WP_ObjectiveStage") == 0, "mission begins at stage zero without completion")) {}
		else if (mode.missionDefeat)
		{
			if (strcmp(mode.env, "mission-defeat-hq") == 0) destroyTarget("PlayerCC");
			else if (hold || rampart) destroyTarget("AlliedRelay");
			else if (takeover) expireTimer();
			else destroyTarget("PlayerCC");
			expectResult("defeat");
		}
		else if (training)
		{
			if (check(counterValue("WP_ObjectiveStage") == (Int)wp_stage, "training advances only after its actual prerequisites"))
			{
				if (wp_stage == 0) fixture("WP_Fabricator");
				else if (wp_stage == 1) { fixture("WP_Exchange"); fixture("WP_PowerArray"); fixture("WP_Porter"); }
				else if (wp_stage == 2) { fixture("WP_VehiclePlant"); fixture("WP_Tank"); fixture("WP_Tank"); }
				else if (wp_stage == 3) fixture("WP_Vigil");
				else { destroyTarget("EnemyCC"); expectResult("victory"); }
				if (wp_stage < 90) ++wp_stage;
			}
		}
		else if (first)
		{
			if (wp_stage == 0) { fixture("WP_Exchange"); fixture("WP_VehiclePlant"); if (wp_stage != 99) wp_stage = 1; }
			else if (check(counterValue("WP_ObjectiveStage") == 1, "foothold objective completed natively"))
			{
				if (wp_stage == 1) { destroyTarget("EnemyCC"); if (wp_stage != 99) wp_stage = 2; }
				else { check(named("ObjectiveRelay") != nullptr, "HQ destruction alone did not win"); destroyTarget("ObjectiveRelay"); expectResult("victory"); }
			}
		}
		else if (sabotage)
		{
			if (check(counterValue("WP_ObjectiveProgress") == (Int)wp_stage, "partial sabotage does not complete the mission"))
			{
				if (wp_stage == 0) { destroyTarget("EnemyCC"); destroyTarget("SupplyOfficeA"); }
				else if (wp_stage == 1) destroyTarget("SupplyOfficeB");
				else { destroyTarget("GridSubstation"); expectResult("victory"); }
				if (wp_stage < 90) ++wp_stage;
			}
		}
		else if (hold || rampart)
		{
			if (wp_stage == 0) { expireTimer(); if (rampart) expectResult("victory"); else if (wp_stage != 99) wp_stage = 1; }
			else if (check(counterValue("WP_ObjectiveStage") == 1 && named("AlliedRelay") != nullptr, "relay held; counterattack still required")) { destroyTarget("EnemyCC"); expectResult("victory"); }
		}
		else if (finale)
		{
			if (wp_stage == 0) { destroyTarget("EnemyCC"); destroyTarget("AirDefense1"); if (wp_stage != 99) wp_stage = 1; }
			else if (check(counterValue("WP_ObjectiveStage") == 0 && counterValue("WP_ObjectiveProgress") == 1, "HQ plus one air-defense site does not win")) { destroyTarget("AirDefense2"); expectResult("victory"); }
		}
		else if (takeover && check(counterValue("WP_ObjectiveTimer") > 0, "headquarters is destroyed before the deadline")) { destroyTarget("EnemyCC"); expectResult("victory"); }
	}
}

// WP_AUTOTEST=economy / powers: explicit fixtures isolate actual supply
// transfers / power effects from construction, balance and player-facing
// targeting tests.
void updateMechanicsLab(const AutotestMode& mode)
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	const Bool wp_economyMode = mode.economy;
	const Bool wp_powerMode = mode.powers;
	static ObjectID depotId = INVALID_ID, cacheId = INVALID_ID, haulerId = INVALID_ID;
	static ObjectID techId = INVALID_ID, targetId = INVALID_ID;
	static UnsignedInt startFrame = 0;
	static Int initialMoney = 0, initialBoxes = 0, maxCargo = 0, initialTroops = 0;
	static Real initialHealth = 0.0f;
	static Bool jackal = FALSE;
	Player* owner = ThePlayerList->getLocalPlayer();
	auto spawnFixture = [&](const char* name, Player* player, Coord3D position) -> Object* {
		const ThingTemplate* tt = TheThingFactory->findTemplate(name);
		Object* obj = tt && player ? TheThingFactory->newObject(tt, player->getDefaultTeam()) : nullptr;
		if (obj) {
			position.z = TheTerrainLogic->getGroundHeight(position.x, position.y);
			obj->setPosition(&position);
			TheAI->pathfinder()->addObjectToPathfindMap(obj);
			// Complete the normal native creation lifecycle. Supply
			// depots register with resource managers only at this step.
			for (BehaviorModule** module = obj->getBehaviorModules(); *module; ++module)
				if ((*module)->getCreate()) (*module)->getCreate()->onBuildComplete();
			fprintf(stderr, "[WP_AUTO] MECHANICS_FIXTURE %s id=%u (construction bypassed)\n", name, (unsigned)obj->getID());
		}
		return obj;
	};
	auto warehouse = [&](Object* cache) -> SupplyWarehouseDockUpdate* {
		return cache ? static_cast<SupplyWarehouseDockUpdate*>(cache->findUpdateModule(NAMEKEY("SupplyWarehouseDockUpdate"))) : nullptr;
	};
	auto ambushTroops = [&]() -> Int {
		Int count = 0;
		for (Object* obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
			if (obj->getControllingPlayer() == owner && !obj->isEffectivelyDead() &&
				(obj->getTemplate()->getName() == "WPJ_Scrapper" || obj->getTemplate()->getName() == "WPJ_Sting")) ++count;
		return count;
	};
	if (wp_stage == 0 && wp_f >= 90 && owner)
	{
		Object* cc = nullptr; Object* cache = nullptr; Player* enemy = nullptr;
		for (Object* obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject()) {
			if (obj->getName() == "HomeSupplyA") cache = obj;
			if (obj->isKindOf(KINDOF_COMMANDCENTER)) {
				if (obj->getControllingPlayer() == owner) cc = obj;
				else if (obj->getControllingPlayer()) enemy = obj->getControllingPlayer();
			}
		}
		if (!cc || (wp_economyMode && !warehouse(cache)) || (wp_powerMode && !enemy)) {
			fprintf(stderr, "[WP_AUTO] MECHANICS FAIL: missing headquarters/cache/enemy fixture context\n");
			wp_stage = 99;
		} else {
			jackal = cc->getTemplate()->getName() == "WPJ_CommandPost";
			Coord3D position = *cc->getPosition(); position.y -= 75.0f;
			if (!jackal) spawnFixture("WP_PowerArray", owner, position);
			if (wp_economyMode) {
				cacheId = cache->getID(); position = *cache->getPosition(); position.x -= 90.0f; position.y -= 35.0f;
				Object* depot = spawnFixture(jackal ? "WPJ_Racket" : "WP_Exchange", owner, position);
				const ThingTemplate* truck = TheThingFactory->findTemplate(jackal ? "WPJ_Scavenger" : "WP_Porter");
				if (depot && truck) {
					depotId = depot->getID();
					GameMessage* select = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
					select->appendBooleanArgument(TRUE); select->appendObjectIDArgument(depotId);
					GameMessage* queue = TheMessageStream->appendMessage(GameMessage::MSG_QUEUE_UNIT_CREATE);
					queue->appendIntegerArgument(truck->getTemplateID()); queue->appendIntegerArgument(1);
					fprintf(stderr, "[WP_AUTO] ECONOMY queued %s through native production\n", truck->getName().str());
					wp_stage = 1;
				} else wp_stage = 99;
			} else {
				position = *cc->getPosition(); position.x -= 80.0f;
				Object* tech = spawnFixture(jackal ? "WPJ_Den" : "WP_Directorate", owner, position);
				position = *cc->getPosition(); position.x += 160.0f;
				Object* target = spawnFixture("WP_MissionRelay", enemy, position);
				position.y += 50.0f;
				spawnFixture(jackal ? "WPJ_Rigger" : "WP_Fabricator", owner, position);
				if (tech && target) {
					techId = tech->getID(); targetId = target->getID(); initialHealth = target->getBodyModule()->getHealth();
					initialTroops = ambushTroops(); wp_stage = 1;
				} else wp_stage = 99;
			}
		}
	}
	else if (wp_economyMode && wp_stage == 1)
	{
		for (Object* obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject())
			if (obj->getControllingPlayer() == owner && obj->getProducerID() == depotId &&
				obj->getTemplate()->getName() == (jackal ? "WPJ_Scavenger" : "WP_Porter")) {
				haulerId = obj->getID(); startFrame = wp_f; initialMoney = owner->getMoney()->countMoney();
				SupplyWarehouseDockUpdate* dock = warehouse(TheGameLogic->findObjectByID(cacheId));
				initialBoxes = dock ? dock->getBoxesStored() : 0; wp_stage = 2;
				fprintf(stderr, "[WP_AUTO] ECONOMY hauler spawned f=%u id=%u stock=%d money=%d\n", wp_f, (unsigned)haulerId, initialBoxes, initialMoney);
				break;
			}
		if (wp_stage == 1 && wp_f >= 1800) { fprintf(stderr, "[WP_AUTO] ECONOMY FAIL: hauler production timeout\n"); wp_stage = 99; }
	}
	else if (wp_economyMode && wp_stage == 2)
	{
		Object* truck = TheGameLogic->findObjectByID(haulerId);
		SupplyTruckAIInterface* ai = truck && truck->getAIUpdateInterface() ? truck->getAIUpdateInterface()->getSupplyTruckAIInterface() : nullptr;
		SupplyWarehouseDockUpdate* dock = warehouse(TheGameLogic->findObjectByID(cacheId));
		Int cargo = ai ? ai->getNumberBoxes() : 0; if (cargo > maxCargo) maxCargo = cargo;
		if ((wp_f - startFrame) % 150 == 0) {
			Int removed = initialBoxes - (dock ? dock->getBoxesStored() : 0);
			Int cash = (Int)owner->getMoney()->countMoney() - initialMoney;
			Int passive = ((wp_f - startFrame) / 150) * (jackal ? 20 : 25);
			fprintf(stderr, "[WP_AUTO] ECONOMY f=%u elapsed=%u stockRemoved=%d cargo=%d maxCargo=%d cashDelta=%d depotPassiveApprox=%d\n",
				wp_f, (wp_f-startFrame)/30, removed, cargo, maxCargo, cash, passive);
			if (wp_f - startFrame >= 1800) {
				fprintf(stderr, "[WP_AUTO] ECONOMY %s: stock transferred beyond carried boxes; cashDelta=%d (passive included)\n",
					ai && dock && maxCargo > 0 && removed > cargo && cash > passive ? "PASS" : "FAIL", cash);
				wp_stage = 99;
				}
			}
	}
	else if (wp_powerMode && wp_stage == 1 && wp_f >= 180)
	{
		Object* tech = TheGameLogic->findObjectByID(techId); Object* target = TheGameLogic->findObjectByID(targetId);
		SpecialPowerModuleInterface* power = tech ? tech->findSpecialPowerModuleInterface(jackal ? SPECIAL_AMBUSH : SPECIAL_ARTILLERY_BARRAGE) : nullptr;
		if (power && target) {
			power->pauseCountdown(FALSE); power->setReadyFrame(0);
			fprintf(stderr, "[WP_AUTO] POWERS invoking %s ready=%d at explored target id=%u (cooldown fixture)\n", power->getPowerName().str(), power->isReady(), (unsigned)targetId);
			power->doSpecialPowerAtLocation(target->getPosition(), 0.0f, 0);
			startFrame = wp_f; wp_stage = 2;
		} else { fprintf(stderr, "[WP_AUTO] POWERS FAIL: missing module or target\n"); wp_stage = 99; }
	}
	else if (wp_powerMode && wp_stage == 2 && wp_f - startFrame >= 180)
	{
		Object* target = TheGameLogic->findObjectByID(targetId); Object* tech = TheGameLogic->findObjectByID(techId);
		Real health = target && target->getBodyModule() ? target->getBodyModule()->getHealth() : 0.0f;
		SpecialPowerModuleInterface* power = tech ? tech->findSpecialPowerModuleInterface(jackal ? SPECIAL_AMBUSH : SPECIAL_ARTILLERY_BARRAGE) : nullptr;
		Int spawned = ambushTroops() - initialTroops;
		Bool effect = jackal ? spawned >= 5 : health < initialHealth;
		fprintf(stderr, "[WP_AUTO] POWERS %s: targetDamage=%.0f newAmbushUnits=%d cooldownReadyFrame=%u\n",
			effect && power && power->getReadyFrame() > wp_f ? "PASS" : "FAIL", initialHealth-health, spawned, power ? power->getReadyFrame() : 0);
		wp_stage = 99;
	}
}

// WP_AUTOTEST=defeat kills the player CC to exercise the WP_Lose map
// script -> DEFEAT screen (Menus/Defeat.wnd)
void updateDefeatLab()
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	const Int wp_localIdx = s_lab.localIdx;
	if (wp_stage == 0 && wp_f >= 300)
	{
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getTemplate()->isKindOf(KINDOF_COMMANDCENTER) &&
				o->getControllingPlayer() &&
				o->getControllingPlayer()->getPlayerIndex() == wp_localIdx)
			{ o->kill(); fprintf(stderr, "[WP_AUTO] f=%u DEFEAT: killed own CC - defeat screen expected\n", wp_f); }
		wp_stage = 1;
	}
}

// WP_AUTOTEST=win kills the ENEMY command structure to exercise the
// WP_Win map script -> VICTORY banner -> score screen
void updateWinLab()
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	const Int wp_localIdx = s_lab.localIdx;
	if (wp_stage == 0 && wp_f >= 300)
	{
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getTemplate()->isKindOf(KINDOF_COMMANDCENTER) &&
				o->getControllingPlayer() &&
				o->getControllingPlayer()->getPlayerIndex() != wp_localIdx)
			{ o->kill(); fprintf(stderr, "[WP_AUTO] f=%u WIN: killed enemy command structure - victory expected\n", wp_f); }
		wp_stage = 1;
	}
}

// WP_AUTOTEST=husk reproduces the user's translucent-remnant repro:
// real dozer constructs a VehiclePlant site next to the enemy guard,
// dozer is recalled, guard kills the 1HP site. Pair with
// WP_SCENE_DUMP=<frame> to census the scene after the death.
void updateHuskLab()
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	const Int wp_localIdx = s_lab.localIdx;
	ObjectID& wp_ccId = s_lab.ccId;
	Coord3D& wp_ccPos = s_lab.ccPos;
	static ObjectID wp_hDozerId = INVALID_ID;
	auto wp_hSelect = [](ObjectID id) {
		GameMessage* sm = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
		sm->appendBooleanArgument(TRUE);
		sm->appendObjectIDArgument(id);
	};
	if (wp_stage == 0 && wp_f >= 90)
	{
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getTemplate()->isKindOf(KINDOF_COMMANDCENTER) &&
				o->getControllingPlayer() &&
				o->getControllingPlayer()->getPlayerIndex() == wp_localIdx)
			{ wp_ccId = o->getID(); wp_ccPos = *o->getPosition(); }
		const ThingTemplate* dzT = TheThingFactory->findTemplate("WP_Fabricator");
		Object* dz = (wp_ccId != INVALID_ID && dzT) ? TheThingFactory->newObject(dzT, ThePlayerList->getLocalPlayer()->getDefaultTeam()) : nullptr;
		if (dz)
		{
			Coord3D tp = wp_ccPos; tp.y += 60.0f;
			tp.z = TheTerrainLogic->getGroundHeight(tp.x, tp.y);
			dz->setPosition(&tp);
			TheAI->pathfinder()->addObjectToPathfindMap(dz);
			wp_hDozerId = dz->getID();
			fprintf(stderr, "[WP_AUTO] f=%u HUSK: dozer id=%u spawned\n", wp_f, (unsigned)wp_hDozerId);
			wp_stage = 1;
		}
	}
	else if (wp_stage == 1 && wp_f >= 600)
	{
		// WarPowers @refactor 07/09/2026 log and stop instead of arming placement
		// with a null template when the dataset lacks WP_VehiclePlant.
		const ThingTemplate* tt = TheThingFactory->findTemplate("WP_VehiclePlant");
		if (!tt)
		{
			fprintf(stderr, "[WP_AUTO] f=%u HUSK FAIL: template WP_VehiclePlant missing from this dataset\n", wp_f);
			wp_stage = 99;
			return;
		}
		// arm placement through the REAL UI flow (preview drawable and all),
		// exactly like clicking the build button does
		wp_hSelect(wp_hDozerId);
		Coord3D loc = { 678.0f, 533.0f, 0.0f };
		loc.z = TheTerrainLogic->getGroundHeight(loc.x, loc.y);
		TheTacticalView->lookAt(&loc);
		Object* dz = TheGameLogic->findObjectByID(wp_hDozerId);
		if (dz && dz->getDrawable())
			TheInGameUI->placeBuildAvailable(tt, dz->getDrawable());
		fprintf(stderr, "[WP_AUTO] f=%u HUSK: placement ARMED via UI (preview live), camera aimed\n", wp_f);
		wp_stage = 20;
	}
	else if (wp_stage == 20 && wp_f >= 615)
	{
		// click the screen center (camera is on the site): raw mouse
		// messages run the real PlaceEventTranslator placement path
		ICoord2D px; px.x = TheDisplay->getWidth() / 2; px.y = TheDisplay->getHeight() / 2;
		GameMessage* dn = TheMessageStream->appendMessage(GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_DOWN);
		dn->appendPixelArgument(px);
		dn->appendIntegerArgument(0);
		dn->appendIntegerArgument(0);
		fprintf(stderr, "[WP_AUTO] f=%u HUSK: raw mouse DOWN at (%d,%d)\n", wp_f, px.x, px.y);
		wp_stage = 21;
	}
	else if (wp_stage == 21 && wp_f >= 617)
	{
		ICoord2D px; px.x = TheDisplay->getWidth() / 2; px.y = TheDisplay->getHeight() / 2;
		GameMessage* up = TheMessageStream->appendMessage(GameMessage::MSG_RAW_MOUSE_LEFT_BUTTON_UP);
		up->appendPixelArgument(px);
		up->appendIntegerArgument(0);
		up->appendIntegerArgument(50);
		fprintf(stderr, "[WP_AUTO] f=%u HUSK: raw mouse UP - placement should commit\n", wp_f);
		wp_stage = 2;
	}
	else if (wp_stage == 2 && wp_f >= 660)
	{
		wp_hSelect(wp_hDozerId);
		GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_DO_MOVETO);
		Coord3D dest = wp_ccPos; dest.x -= 60.0f;
		dest.z = TheTerrainLogic->getGroundHeight(dest.x, dest.y);
		m->appendLocationArgument(dest);
		fprintf(stderr, "[WP_AUTO] f=%u HUSK: dozer recalled; awaiting guard kill\n", wp_f);
		wp_stage = 3;
	}
	else if (wp_stage == 3 && wp_f >= 900)
	{
		Object* site = nullptr;
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getTemplate()->getName() == "WP_VehiclePlant") site = o;
		fprintf(stderr, "[WP_AUTO] f=%u HUSK: site %s\n", wp_f,
			site ? "EXISTS (will kill at f=1100 if guard has not)" : "already dead or never placed - continuing to dump");
		wp_stage = 4;
	}
	else if (wp_stage == 4 && wp_f >= 1100)
	{
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getTemplate()->getName() == "WP_VehiclePlant") { o->kill(); break; }
		fprintf(stderr, "[WP_AUTO] f=%u HUSK: killed fogged site\n", wp_f);
		wp_stage = 5;
	}
	else if (wp_stage == 5 && wp_f >= 1550)
	{
		// object census: is the site object still alive/present?
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getTemplate()->getName() == "WP_VehiclePlant")
				fprintf(stderr, "[WP_AUTO] HUSK census: WP_VehiclePlant id=%u dead=%d destroyed=%d draw=%p\n",
					(unsigned)o->getID(), (int)o->isEffectivelyDead(),
					(int)o->isDestroyed(), (void*)o->getDrawable());
		fprintf(stderr, "[WP_AUTO] f=%u HUSK: done (scene dump should have fired)\n", wp_f);
		TheGameEngine->setQuitting(TRUE);
		wp_stage = 6;
	}
}

// WP_AUTOTEST=ghost verifies the fog-memory lifecycle: spawn a neutral
// structure out of base vision, scout it with a tank, retreat (fog ->
// snapshot), kill it while fogged (orphan ghost), re-scout (ghost must
// free). Read the IG_TRACE [GHOST] breadcrumbs in the log.
void updateGhostLab()
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	const Int wp_localIdx = s_lab.localIdx;
	ObjectID& wp_ccId = s_lab.ccId;
	Coord3D& wp_ccPos = s_lab.ccPos;
	static ObjectID wp_gTankId = INVALID_ID, wp_gTargetId = INVALID_ID;
	auto wp_gSelect = [](ObjectID id) {
		GameMessage* s = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
		s->appendBooleanArgument(TRUE);
		s->appendObjectIDArgument(id);
	};
	auto wp_gMove = [&](ObjectID id, Real x, Real y) {
		wp_gSelect(id);
		GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_DO_MOVETO);
		Coord3D dest = { x, y, 0.0f };
		dest.z = TheTerrainLogic->getGroundHeight(x, y);
		m->appendLocationArgument(dest);
	};
	if (wp_stage == 0 && wp_f >= 90)
	{
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getTemplate()->isKindOf(KINDOF_COMMANDCENTER) &&
				o->getControllingPlayer() &&
				o->getControllingPlayer()->getPlayerIndex() == wp_localIdx)
			{ wp_ccId = o->getID(); wp_ccPos = *o->getPosition(); }
		if (wp_ccId == INVALID_ID)
		{
			// keep waiting next frame
		}
		else
		{
		// neutral structure well outside CC shroud-clear (300)
		const ThingTemplate* tt = TheThingFactory->findTemplate("WPJ_ChopShop");
		Object* target = tt ? TheThingFactory->newObject(tt, ThePlayerList->getNeutralPlayer()->getDefaultTeam()) : nullptr;
		// own scout tank at the CC
		const ThingTemplate* tankT = TheThingFactory->findTemplate("WP_Tank");
		Object* tank = tankT ? TheThingFactory->newObject(tankT, ThePlayerList->getLocalPlayer()->getDefaultTeam()) : nullptr;
		if (!target || !tank)
		{
			fprintf(stderr, "[WP_AUTO] GHOST: spawn failed\n");
			TheGameEngine->setQuitting(TRUE);
			wp_stage = 99;
		}
		else
		{
		Coord3D p = wp_ccPos; p.x -= 370.0f;
		p.z = TheTerrainLogic->getGroundHeight(p.x, p.y);
		target->setPosition(&p);
		TheAI->pathfinder()->addObjectToPathfindMap(target);
		target->handlePartitionCellMaintenance();
		wp_gTargetId = target->getID();
		Coord3D tp = wp_ccPos; tp.y += 60.0f;
		tp.z = TheTerrainLogic->getGroundHeight(tp.x, tp.y);
		tank->setPosition(&tp);
		TheAI->pathfinder()->addObjectToPathfindMap(tank);
		wp_gTankId = tank->getID();
		fprintf(stderr, "[WP_AUTO] f=%u GHOST: spawned target id=%u at -370, tank id=%u\n",
			wp_f, (UnsignedInt)wp_gTargetId, (UnsignedInt)wp_gTankId);
		wp_stage = 1;
		}
		}
	}
	else if (wp_stage == 1 && wp_f >= 150)
	{
		// scout: park 120 short of the target (vision 150 covers it)
		wp_gMove(wp_gTankId, wp_ccPos.x - 300.0f, wp_ccPos.y);
		Coord3D look = wp_ccPos; look.x -= 370.0f;
		TheTacticalView->lookAt(&look);
		fprintf(stderr, "[WP_AUTO] f=%u GHOST: tank scouting target\n", wp_f);
		wp_stage = 2;
	}
	else if (wp_stage == 2 && wp_f >= 500)
	{
		// retreat home -> target cell fogs -> expect [GHOST] snapShot
		wp_gMove(wp_gTankId, wp_ccPos.x + 60.0f, wp_ccPos.y);
		fprintf(stderr, "[WP_AUTO] f=%u GHOST: tank retreating (expect snapShot)\n", wp_f);
		wp_stage = 3;
	}
	else if (wp_stage == 3 && wp_f >= 950)
	{
		Object* target = TheGameLogic->findObjectByID(wp_gTargetId);
		if (target)
			target->kill();
		fprintf(stderr, "[WP_AUTO] f=%u GHOST: killed fogged target (orphan ghost expected)\n", wp_f);
		wp_stage = 4;
	}
	else if (wp_stage == 4 && wp_f >= 1050)
	{
		// re-scout -> expect [GHOST] freeSnapShot + orphan removed
		wp_gMove(wp_gTankId, wp_ccPos.x - 300.0f, wp_ccPos.y);
		fprintf(stderr, "[WP_AUTO] f=%u GHOST: tank re-scouting (expect freeSnapShot + orphan removal)\n", wp_f);
		wp_stage = 5;
	}
	else if (wp_stage == 5 && wp_f >= 1500)
	{
		fprintf(stderr, "[WP_AUTO] f=%u GHOST: sequence complete - check [GHOST] traces above\n", wp_f);
		TheGameEngine->setQuitting(TRUE);
		wp_stage = 6;
	}
}

// WP_AUTOTEST=strike: field 4 tanks and destroy the nearest enemy
// STRUCTURE to the player base (the AI's forward tower) — a
// deterministic building-kill to exercise kill-credit scoring and
// the PLAYER_DESTROYED_N_BUILDINGS_PLAYER punish condition.
void updateStrikeLab()
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	const Int wp_localIdx = s_lab.localIdx;
	ObjectID& wp_ccId = s_lab.ccId;
	Coord3D& wp_ccPos = s_lab.ccPos;
	static ObjectID wp_stFleet[4] = {INVALID_ID, INVALID_ID, INVALID_ID, INVALID_ID};
	static ObjectID wp_stTarget = INVALID_ID;
	if (wp_stage == 0 && wp_f >= 90)
	{
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
		{
			if (!o->getTemplate()->isKindOf(KINDOF_COMMANDCENTER)) continue;
			Int idx = o->getControllingPlayer() ? o->getControllingPlayer()->getPlayerIndex() : -1;
			if (idx == wp_localIdx) { wp_ccId = o->getID(); wp_ccPos = *o->getPosition(); }
		}
		if (wp_ccId != INVALID_ID)
		{
			prepareUnitLab();
			GameMessage* s0 = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
			s0->appendBooleanArgument(TRUE);
			s0->appendObjectIDArgument(wp_ccId);
			const ThingTemplate* tt = TheThingFactory->findTemplate(labTankName());
			if (tt)
				for (Int q = 0; q < 4; ++q)
				{
					GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_QUEUE_UNIT_CREATE);
					m->appendIntegerArgument(tt->getTemplateID());
					m->appendIntegerArgument(1);
				}
			fprintf(stderr, "[WP_AUTO] f=%u STRIKE: queued 4 tanks\n", wp_f);
			wp_stage = 1;
		}
	}
	else if (wp_stage == 1 && (wp_f % 30) == 0 && wp_f >= 900)
	{
		Int wp_n = 0;
		for (Object* o = TheGameLogic->getFirstObject(); o && wp_n < 4; o = o->getNextObject())
			if (o->getTemplate()->getName() == labTankName() && !o->isEffectivelyDead() &&
				o->getControllingPlayer() &&
				o->getControllingPlayer()->getPlayerIndex() == wp_localIdx)
				wp_stFleet[wp_n++] = o->getID();
		if (wp_n >= 4)
		{
			// nearest enemy structure to OUR base = the forward tower
			Real bestSq = 1e18f; Object* tgt = nullptr;
			for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			{
				if (!o->isKindOf(KINDOF_STRUCTURE) || o->isEffectivelyDead()) continue;
				Player* op = o->getControllingPlayer();
				if (!op || op->getPlayerIndex() == wp_localIdx || op->getPlayerTemplate() == nullptr) continue;
				if (op->getPlayerType() != PLAYER_COMPUTER) continue;
				Real dx = o->getPosition()->x - wp_ccPos.x;
				Real dy = o->getPosition()->y - wp_ccPos.y;
				if (dx*dx + dy*dy < bestSq) { bestSq = dx*dx + dy*dy; tgt = o; }
			}
			if (tgt)
			{
				wp_stTarget = tgt->getID();
				GameMessage* s2 = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
				s2->appendBooleanArgument(TRUE);
				for (Int fi = 0; fi < 4; ++fi) s2->appendObjectIDArgument(wp_stFleet[fi]);
				GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_DO_ATTACK_OBJECT);
				m->appendObjectIDArgument(wp_stTarget);
				fprintf(stderr, "[WP_AUTO] f=%u STRIKE: attacking structure id=%u tmpl=%s\n",
					wp_f, (unsigned)wp_stTarget, tgt->getTemplate()->getName().str());
				wp_stage = 2;
			}
		}
	}
	else if (wp_stage == 2 && (wp_f % 30) == 0)
	{
		Object* tgt = TheGameLogic->findObjectByID(wp_stTarget);
		if (!tgt || tgt->isEffectivelyDead())
		{
			fprintf(stderr, "[WP_AUTO] f=%u STRIKE: structure down — watch [WPSCORE]/teamPunish\n", wp_f);
			wp_stage = 3;
		}
	}
}

// WP_AUTOTEST=base drives the dozer loop instead: CC -> Surveyor ->
// construct Power Station -> construct Vehicle Works -> build a tank
// from the factory. Verifies D016 construction end to end.
// WP_AUTOTEST=wedge: identical to base but the SECOND construct order is
// injected while the first structure is still ~90% under construction —
// the exact timing that wedged the dozer's primary state machine
// (null current state; see the self-heal in DozerAIUpdate::update).
// Passing = VehiclePlant still gets built afterwards.
void updateBaseLab(const AutotestMode& mode)
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	const Int wp_localIdx = s_lab.localIdx;
	ObjectID& wp_ccId = s_lab.ccId;
	Coord3D& wp_ccPos = s_lab.ccPos;
	const Bool wp_wedgeMode = mode.wedge;
	// --- D016 base-loop machine (stages 10..16) ---
	static ObjectID wp_dozerId = INVALID_ID;
	static ObjectID wp_ppId = INVALID_ID, wp_wfId = INVALID_ID;
	auto wp_select = [](ObjectID id) {
		GameMessage* s = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
		s->appendBooleanArgument(TRUE);
		s->appendObjectIDArgument(id);
	};
	auto wp_findOurs = [&](const char* tmpl) -> Object* {
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getTemplate()->getName() == tmpl && !o->isEffectivelyDead() &&
				o->getControllingPlayer() &&
				o->getControllingPlayer()->getPlayerIndex() == wp_localIdx)
				return o;
		return nullptr;
	};
	// WarPowers @refactor 07/09/2026 the construct/queue steps used to
	// dereference findTemplate() results unchecked; on a dataset without the
	// template they now log a BASE FAIL and stop instead of crashing.
	auto wp_missingTemplate = [&](const char* name) -> Bool {
		if (TheThingFactory->findTemplate(name)) return FALSE;
		fprintf(stderr, "[WP_AUTO] f=%u BASE FAIL: template %s missing from this dataset\n", wp_f, name);
		wp_stage = 99;
		return TRUE;
	};
	if (wp_stage == 0 && wp_f >= 90)
	{
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
		{
			if (!o->getTemplate()->isKindOf(KINDOF_COMMANDCENTER))
				continue;
			Int idx = o->getControllingPlayer() ? o->getControllingPlayer()->getPlayerIndex() : -1;
			if (idx == wp_localIdx) { wp_ccId = o->getID(); wp_ccPos = *o->getPosition(); }
		}
		if (wp_ccId != INVALID_ID)
		{
			wp_select(wp_ccId);
			const ThingTemplate* tt = TheThingFactory->findTemplate("WP_Fabricator");
			if (tt)
			{
				GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_QUEUE_UNIT_CREATE);
				m->appendIntegerArgument(tt->getTemplateID());
				m->appendIntegerArgument(1);
				fprintf(stderr, "[WP_AUTO] f=%u BASE: queued WP_Fabricator\n", wp_f);
			}
			wp_stage = 10;
		}
	}
	else if (wp_stage == 10 && wp_f >= 300)
	{
		Object* dz = wp_findOurs("WP_Fabricator");
		if (dz)
		{
			if (wp_missingTemplate("WP_PowerArray")) return;
			wp_dozerId = dz->getID();
			wp_select(wp_dozerId);
			const ThingTemplate* tt = TheThingFactory->findTemplate("WP_PowerArray");
			GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_DOZER_CONSTRUCT);
			m->appendIntegerArgument(tt->getTemplateID());
			Coord3D loc = wp_ccPos; loc.x -= 75.0f; loc.y += 65.0f;
			m->appendLocationArgument(loc);
			m->appendRealArgument(0.0f);
			fprintf(stderr, "[WP_AUTO] f=%u BASE: dozer id=%u -> construct PowerArray at (%.0f,%.0f)\n",
				wp_f, (unsigned)wp_dozerId, loc.x, loc.y);
			wp_stage = 11;
		}
		else if (wp_f >= 900)
		{
			fprintf(stderr, "[WP_AUTO] f=%u BASE FAIL: no Fabricator spawned\n", wp_f);
			wp_stage = 99;
		}
	}
	else if (wp_stage == 11 && (wp_f % 30) == 0)
	{
		Object* pp = wp_findOurs("WP_PowerArray");
		if (pp)
		{
			if (wp_ppId == INVALID_ID)
			{
				wp_ppId = pp->getID();
				fprintf(stderr, "[WP_AUTO] f=%u BASE: PowerArray placed id=%u\n", wp_f, (unsigned)wp_ppId);
			}
			Bool wp_orderNow = !pp->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION);
			if (wp_wedgeMode && !wp_orderNow && pp->getConstructionPercent() >= 88.0f)
			{
				fprintf(stderr, "[WP_AUTO] f=%u WEDGE: PowerArray at %.0f%% — injecting second construct NOW\n",
					wp_f, pp->getConstructionPercent());
				wp_orderNow = TRUE;
			}
			if (wp_orderNow)
			{
				if (wp_missingTemplate("WP_VehiclePlant")) return;
				fprintf(stderr, "[WP_AUTO] f=%u BASE: PowerArray %s\n", wp_f,
					pp->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION) ? "still building (wedge inject)" : "CONSTRUCTED");
				wp_select(wp_dozerId);
				const ThingTemplate* tt = TheThingFactory->findTemplate("WP_VehiclePlant");
				GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_DOZER_CONSTRUCT);
				m->appendIntegerArgument(tt->getTemplateID());
				Coord3D loc = wp_ccPos; loc.x += 80.0f; loc.y += 70.0f;
				m->appendLocationArgument(loc);
				m->appendRealArgument(0.0f);
				fprintf(stderr, "[WP_AUTO] f=%u BASE: construct VehiclePlant at (%.0f,%.0f)\n", wp_f, loc.x, loc.y);
				wp_stage = 12;
			}
		}
		else if (wp_f >= 3000)
		{
			fprintf(stderr, "[WP_AUTO] f=%u BASE FAIL: PowerArray never appeared\n", wp_f);
			wp_stage = 99;
		}
	}
	else if (wp_stage == 12 && (wp_f % 30) == 0)
	{
		Object* wf = wp_findOurs("WP_VehiclePlant");
		if (wf)
		{
			if (wp_wfId == INVALID_ID)
			{
				wp_wfId = wf->getID();
				fprintf(stderr, "[WP_AUTO] f=%u BASE: VehiclePlant placed id=%u\n", wp_f, (unsigned)wp_wfId);
			}
			if (!wf->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION))
			{
				if (wp_missingTemplate("WP_Tank")) return;
				fprintf(stderr, "[WP_AUTO] f=%u BASE: VehiclePlant CONSTRUCTED\n", wp_f);
				wp_select(wp_wfId);
				const ThingTemplate* tt = TheThingFactory->findTemplate("WP_Tank");
				GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_QUEUE_UNIT_CREATE);
				m->appendIntegerArgument(tt->getTemplateID());
				m->appendIntegerArgument(1);
				fprintf(stderr, "[WP_AUTO] f=%u BASE: queued WP_Tank from factory\n", wp_f);
				wp_stage = 13;
			}
		}
		else if (wp_f >= 6000)
		{
			fprintf(stderr, "[WP_AUTO] f=%u BASE FAIL: VehiclePlant never appeared\n", wp_f);
			wp_stage = 99;
		}
	}
	else if (wp_stage == 13 && (wp_f % 30) == 0)
	{
		Object* tank = wp_findOurs("WP_Tank");
		if (tank)
		{
			fprintf(stderr, "[WP_AUTO] f=%u BASE: tank id=%u rolled out of the factory — BASE_LOOP_OK\n",
				wp_f, (unsigned)tank->getID());
			wp_stage = 14;
		}
		else if (wp_f >= 7500)
		{
			fprintf(stderr, "[WP_AUTO] f=%u BASE FAIL: no tank from factory\n", wp_f);
			wp_stage = 99;
		}
	}
}

// Default WP_AUTOTEST lab (any unlisted value, plus build and cycle):
// scripted input smoke test that injects the same logic messages real
// mouse input produces: select the local command center, queue a tank,
// select the tank, move it, then attack the enemy command center.
// Verifies the full command chain headlessly.
void updateUnitLab(const AutotestMode& mode)
{
	UnsignedInt& wp_stage = s_lab.stage;
	const UnsignedInt wp_f = s_lab.frame;
	const Int wp_localIdx = s_lab.localIdx;
	ObjectID& wp_ccId = s_lab.ccId;
	ObjectID& wp_enemyCcId = s_lab.enemyCcId;
	ObjectID& wp_tankId = s_lab.tankId;
	ObjectID* wp_fleet = s_lab.fleet;
	Coord3D& wp_ccPos = s_lab.ccPos;
	const Bool wp_buildOnly = mode.buildOnly;
	if (wp_stage == 0 && wp_f >= 90)
	{
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
		{
			if (!o->getTemplate()->isKindOf(KINDOF_COMMANDCENTER))
				continue;
			Int idx = o->getControllingPlayer() ? o->getControllingPlayer()->getPlayerIndex() : -1;
			if (idx == wp_localIdx) { wp_ccId = o->getID(); wp_ccPos = *o->getPosition(); }
			else                    { wp_enemyCcId = o->getID(); }
		}
		if (wp_ccId != INVALID_ID)
		{
			GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
			m->appendBooleanArgument(TRUE);
			m->appendObjectIDArgument(wp_ccId);
			fprintf(stderr, "[WP_AUTO] f=%u selected CC id=%u at (%.0f,%.0f); enemy CC id=%u\n",
				wp_f, (unsigned)wp_ccId, wp_ccPos.x, wp_ccPos.y, (unsigned)wp_enemyCcId);
			wp_stage = 1;
		}
	}
	else if (wp_stage == 1 && wp_f >= 120)
	{
		prepareUnitLab();
		// WP_AUTOTEST_UNIT overrides the fielded template (default WP_Tank)
		// so any new unit class gets a spawn+move+shoot lab for free.
		const char* wp_unitEnv = getenv("WP_AUTOTEST_UNIT");
		const ThingTemplate* tt = TheThingFactory->findTemplate(
			AsciiString(wp_unitEnv && wp_unitEnv[0] ? wp_unitEnv : labTankName()));
		Object* wp_cc = TheGameLogic->findObjectByID(wp_ccId);
		if (tt && wp_cc)
		{
			const CommandSet* cs = TheControlBar ? TheControlBar->findCommandSet(wp_cc->getCommandSetString()) : nullptr;
			const CommandButton* cb = cs ? cs->getCommandButton(0) : nullptr;
			fprintf(stderr, "[WP_AUTO] gates: cmdSetStr='%s' cs=%p cb=%p cbType=%d cbTT=%p canMake=%d\n",
				wp_cc->getCommandSetString().str(), (const void*)cs, (const void*)cb,
				cb ? (int)cb->getCommandType() : -1,
				cb ? (const void*)cb->getThingTemplate() : nullptr,
				(int)TheBuildAssistant->canMakeUnit(wp_cc, tt));
		}
		if (tt)
		{
			Int wp_count = wp_buildOnly ? 1 : 4;   // full mode fields four - a pair loses to the defended base (towers + defenders + return fire)
			for (Int q = 0; q < wp_count; ++q)
			{
				GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_QUEUE_UNIT_CREATE);
				m->appendIntegerArgument(tt->getTemplateID());
				m->appendIntegerArgument(1);
			}
			fprintf(stderr, "[WP_AUTO] f=%u queued %dx %s (templateID=%d)\n", wp_f, wp_count, tt->getName().str(), (int)tt->getTemplateID());
		}
		wp_stage = 2;
	}
	else if (wp_stage == 2 && wp_f >= 330)
	{
		Int found = 0;
		const char* wp_unitEnv2 = getenv("WP_AUTOTEST_UNIT");
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == (wp_unitEnv2 && wp_unitEnv2[0] ? wp_unitEnv2 : labTankName()) &&
				o->getControllingPlayer() && o->getControllingPlayer()->getPlayerIndex() == wp_localIdx)
			{
				wp_fleet[found] = o->getID();
				if (++found >= 4) break;
			}
		}
		Int need = wp_buildOnly ? 1 : 4;
		const ThingTemplate* timedUnit = TheThingFactory->findTemplate(
			wp_unitEnv2 && wp_unitEnv2[0] ? wp_unitEnv2 : labTankName());
		const UnsignedInt productionDeadline = 1020 + (UnsignedInt)(3 * need *
			(timedUnit ? timedUnit->calcTimeToBuild(ThePlayerList->getLocalPlayer()) : 450));
		if (found >= need)
		{
			wp_tankId = wp_fleet[0];
			GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
			m->appendBooleanArgument(TRUE);
			for (Int fi = 0; fi < found; ++fi)
				m->appendObjectIDArgument(wp_fleet[fi]);
			fprintf(stderr, "[WP_AUTO] f=%u fleet ready (%d tanks), selected\n", wp_f, found);
			wp_stage = 3;
		}
		else if (wp_f >= productionDeadline)
		{
			fprintf(stderr, "[WP_AUTO] f=%u FAIL: only %d/%d tanks by production deadline %u\n", wp_f, found, need, productionDeadline);
			wp_stage = 99;
		}
	}
	else if (wp_stage == 3 && wp_buildOnly)
	{
		fprintf(stderr, "[WP_AUTO] build-only mode: tank ready, handing over to the mouse\n");
		wp_stage = 99;
	}
	else if (wp_stage == 3 && wp_f >= 360)
	{
		// stay home: parking a lone tank inside enemy guard range is
		// how the old smoke test started losing once return fire
		// worked. The duel stage issues the real attack order.
		GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_DO_MOVETO);
		Coord3D dest = wp_ccPos;
		dest.x += 60.0f;
		m->appendLocationArgument(dest);
		fprintf(stderr, "[WP_AUTO] f=%u move order to (%.0f,%.0f)\n", wp_f, dest.x, dest.y);
		wp_stage = 4;
	}
	else if (wp_stage == 4 && wp_f >= 550)
	{
		// clear enemy combat vehicles (they guard the CC path) before the push
		static ObjectID wp_foeId = INVALID_ID;
		Object* wp_tank = TheGameLogic->findObjectByID(wp_tankId);
		if (!wp_tank || wp_tank->isEffectivelyDead())
		{
			wp_tank = nullptr;
			for (Int fi = 0; fi < 4; ++fi)   // promote the first survivor to lead
			{
				Object* cand = TheGameLogic->findObjectByID(wp_fleet[fi]);
				if (cand && !cand->isEffectivelyDead())
				{
					wp_tankId = wp_fleet[fi];
					wp_tank = cand;
					break;
				}
			}
		}
		if (!wp_tank || wp_tank->isEffectivelyDead())
		{
			fprintf(stderr, "[WP_AUTO] f=%u FAIL: our tanks died before the CC push\n", wp_f);
			wp_stage = 99;
		}
		else
		{
			Object* foe = nullptr;
			Real bestSq = 1e30f;
			for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			{
				if (!o->getTemplate()->isKindOf(KINDOF_VEHICLE) || o->isEffectivelyDead())
					continue;
				Int idx = o->getControllingPlayer() ? o->getControllingPlayer()->getPlayerIndex() : -1;
				if (idx == wp_localIdx || idx < 0)
					continue;
				Real dx = o->getPosition()->x - wp_tank->getPosition()->x;
				Real dy = o->getPosition()->y - wp_tank->getPosition()->y;
				if (dx*dx + dy*dy < bestSq) { bestSq = dx*dx + dy*dy; foe = o; }
			}
			if (foe && wp_foeId == INVALID_ID)
			{
				wp_foeId = foe->getID();
				GameMessage* s2 = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
				s2->appendBooleanArgument(TRUE);
				for (Int fi = 0; fi < 4; ++fi)
				{
					Object* fm = TheGameLogic->findObjectByID(wp_fleet[fi]);
					if (fm && !fm->isEffectivelyDead())
						s2->appendObjectIDArgument(fm->getID());
				}
				GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_DO_ATTACK_OBJECT);
				m->appendObjectIDArgument(wp_foeId);
				fprintf(stderr, "[WP_AUTO] f=%u duel: attack enemy vehicle id=%u\n", wp_f, (unsigned)wp_foeId);
			}
			else if (wp_foeId != INVALID_ID)
			{
				Object* prev = TheGameLogic->findObjectByID(wp_foeId);
				if (!prev || prev->isEffectivelyDead())
				{
					fprintf(stderr, "[WP_AUTO] f=%u duel won (vehicle id=%u down)\n", wp_f, (unsigned)wp_foeId);
					wp_foeId = INVALID_ID;   // rescan for the next guard
				}
			}
			else if (wp_enemyCcId != INVALID_ID)
			{
				GameMessage* s2 = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
				s2->appendBooleanArgument(TRUE);
				for (Int fi = 0; fi < 4; ++fi)
				{
					Object* fm = TheGameLogic->findObjectByID(wp_fleet[fi]);
					if (fm && !fm->isEffectivelyDead())
						s2->appendObjectIDArgument(fm->getID());
				}
				GameMessage* m = TheMessageStream->appendMessage(GameMessage::MSG_DO_ATTACK_OBJECT);
				m->appendObjectIDArgument(wp_enemyCcId);
				fprintf(stderr, "[WP_AUTO] f=%u attack order on enemy CC id=%u\n", wp_f, (unsigned)wp_enemyCcId);
				wp_stage = 5;
			}
		}
	}
}

// Every 150 frames, in every mode: particle count plus one line per WP_*
// object (health, production queue, disabled state).
void printStatus()
{
	const UnsignedInt wp_f = s_lab.frame;
	UnsignedInt& wp_lastStatus = s_lab.lastStatus;
	if (wp_f >= wp_lastStatus + 150)
	{
		wp_lastStatus = wp_f;
		fprintf(stderr, "[WP_AUTO] f=%u particles=%u\n", wp_f,
			TheParticleSystemManager ? (unsigned)TheParticleSystemManager->getParticleCount() : 0u);
		for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
		{
			if (strncmp(o->getTemplate()->getName().str(), "WP_", 3) != 0)
				continue;
			Real hp = o->getBodyModule() ? o->getBodyModule()->getHealth() : -1.0f;
			ProductionUpdateInterface* pui = o->getProductionUpdateInterface();
			Real pct = -1.0f;
			if (pui && pui->firstProduction())
				pct = pui->firstProduction()->getPercentComplete();
			fprintf(stderr, "[WP_AUTO] f=%u status '%s' id=%u pos=(%.0f,%.0f) hp=%.0f prodQ=%d pct=%.0f disabled=%d\n",
				wp_f, o->getTemplate()->getName().str(), (unsigned)o->getID(),
				o->getPosition()->x, o->getPosition()->y, hp,
				pui ? (int)pui->getProductionCount() : -1, pct,
				(int)o->isDisabled());
		}
		fflush(stderr);
	}
}

void updateAutotest()
{
	static const AutotestMode mode = resolveAutotestMode();
	if (mode.cycle && TheGameLogic && !TheGameLogic->isInGame())
		updateCycleOutOfGame();
	if (!mode.active || !TheGameLogic || !TheGameLogic->isInGame() || !ThePlayerList)
		return;
	s_lab.frame = TheGameLogic->getFrame();
	if (mode.cycle)
		updateCycleInGame();
	s_lab.localIdx = ThePlayerList->getLocalPlayer() ? ThePlayerList->getLocalPlayer()->getPlayerIndex() : -1;
	if (mode.mission || mode.missionDefeat)
		updateMissionLab(mode);
	else if (mode.economy || mode.powers)
		updateMechanicsLab(mode);
	else if (mode.defeat)
		updateDefeatLab();
	else if (mode.win)
		updateWinLab();
	else if (mode.husk)
		updateHuskLab();
	else if (mode.ghost)
		updateGhostLab();
	else if (mode.strike)
		updateStrikeLab();
	else if (mode.base || mode.wedge)
		updateBaseLab(mode);
	else
		updateUnitLab(mode);
	printStatus();
}

} // namespace

//-------------------------------------------------------------------------------------------------
void WPHarness_Update()
{
	updateRetryDiagnostic();
	updateAutotest();
}

//-------------------------------------------------------------------------------------------------
// WP_CLICKTEST: self-driving mouse smoke test. The platform layer fabricates
// real SDL button events at the tank's projected screen position and pushes
// them through the exact path OS clicks take (addSDLEvent -> Mouse::update ->
// raw messages -> translators). Pair with WP_AUTOTEST=build so a tank exists.
void WPHarness_ClickTest(WPHarnessClickFn sendClick, void* context)
{
	static const Bool wp_click = wpEnvEnabled("WP_CLICKTEST");  // set, non-empty and not "0"
	if (!wp_click || !sendClick || !TheGameLogic || !TheGameLogic->isInGame() || !TheTacticalView)
		return;

	const UnsignedInt wp_f = TheGameLogic->getFrame();
	static UnsignedInt wp_stage = 0;
	static Coord3D wp_tankPos = {0,0,0};

	auto wp_sendClick = [&](Int ix, Int iy, Bool down)
	{
		Real wx = 0.0f, wy = 0.0f;
		sendClick(context, ix, iy, down, &wx, &wy);
		fprintf(stderr, "[WP_CLICK] f=%u %s at internal (%d,%d) window (%.0f,%.0f)\n",
			wp_f, down ? "DOWN" : "UP", ix, iy, wx, wy);
		fflush(stderr);
	};

	static ICoord2D wp_pt = {0,0};
	static const Bool wp_uiMode = getenv("WP_CLICKTEST") && strcmp(getenv("WP_CLICKTEST"), "ui") == 0;
	if (wp_uiMode)
	{
		// UI mode: human-path production — click the CC, click the
		// build button in the ControlBar, verify the tank appears.
		if (wp_stage == 0 && wp_f >= 120)
		{
			for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getTemplate()->getName() == "WP_CommandCenter" &&
						o->getControllingPlayer() == ThePlayerList->getLocalPlayer())
				{ wp_tankPos = *o->getPosition(); break; }
			}
			if (TheTacticalView->worldToScreen(&wp_tankPos, &wp_pt))
			{
				fprintf(stderr, "[WP_CLICK] f=%u UI: CC world (%.0f,%.0f) -> screen (%d,%d)\n",
					wp_f, wp_tankPos.x, wp_tankPos.y, wp_pt.x, wp_pt.y);
				wp_sendClick(wp_pt.x, wp_pt.y, TRUE);
				wp_stage = 1;
			}
			else
			{
				static Bool wp_w2sLogged = FALSE;
				if (!wp_w2sLogged)
				{
					fprintf(stderr, "[WP_CLICK] f=%u UI: worldToScreen FAILED for CC (%.0f,%.0f,%.0f) -> (%d,%d)\n",
						wp_f, wp_tankPos.x, wp_tankPos.y, wp_tankPos.z, wp_pt.x, wp_pt.y);
					fflush(stderr);
					wp_w2sLogged = TRUE;
				}
			}
		}
		else if (wp_stage == 1) { wp_sendClick(wp_pt.x, wp_pt.y, FALSE); wp_stage = 2; }
		else if (wp_stage == 2 && wp_f >= 180)
		{
			fprintf(stderr, "[WP_CLICK] f=%u UI: selectCount=%d\n", wp_f, (int)TheInGameUI->getSelectCount());
			GameWindow *wp_btn = TheWindowManager->winGetWindowFromId(nullptr,
				TheNameKeyGenerator->nameToKey("ControlBar.wnd:ButtonCommand01"));
			if (wp_btn)
			{
				Int bx = 0, by = 0, bw = 0, bh = 0;
				wp_btn->winGetScreenPosition(&bx, &by);
				wp_btn->winGetSize(&bw, &bh);
				fprintf(stderr, "[WP_CLICK] f=%u UI: ButtonCommand01 at (%d,%d) %dx%d hidden=%d enabled=%d\n",
					wp_f, bx, by, bw, bh, (int)wp_btn->winIsHidden(),
					(int)((wp_btn->winGetStatus() & WIN_STATUS_ENABLED) != 0));
				wp_pt.x = bx + bw / 2;
				wp_pt.y = by + bh / 2;
				wp_sendClick(wp_pt.x, wp_pt.y, TRUE);
				wp_stage = 3;
			}
			else
			{
				fprintf(stderr, "[WP_CLICK] f=%u UI FAIL: ButtonCommand01 window missing\n", wp_f);
				wp_stage = 99;
			}
		}
		else if (wp_stage == 3) { wp_sendClick(wp_pt.x, wp_pt.y, FALSE); wp_stage = 4; }
		else if (wp_stage == 4 && wp_f >= 240)
		{
			for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getTemplate()->getName() == "WP_CommandCenter" &&
						o->getControllingPlayer() == ThePlayerList->getLocalPlayer())
				{
					ProductionUpdateInterface *wp_pui = o->getProductionUpdateInterface();
					fprintf(stderr, "[WP_CLICK] f=%u UI: CC prodQ=%d\n",
						wp_f, wp_pui ? (int)wp_pui->getProductionCount() : -1);
					break;
				}
			}
			wp_stage = 5;
		}
		else if (wp_stage == 5 && wp_f >= 600)
		{
			Bool wp_found = FALSE;
			for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getTemplate()->getName() == "WP_Tank")
				{
					fprintf(stderr, "[WP_CLICK] f=%u UI BUILD SUCCESS: tank at (%.0f,%.0f)\n",
						wp_f, o->getPosition()->x, o->getPosition()->y);
					wp_found = TRUE;
					break;
				}
			}
			if (!wp_found)
				fprintf(stderr, "[WP_CLICK] f=%u UI BUILD FAIL: no tank spawned\n", wp_f);
			wp_stage = 6;
		}
		fflush(stderr);
	}
	else
	{
		if (wp_stage == 0 && wp_f >= 450)
		{
			// find the tank, click its screen position
			for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getTemplate()->getName() == "WP_Tank") { wp_tankPos = *o->getPosition(); break; }
			}
			if (wp_tankPos.x != 0.0f && TheTacticalView->worldToScreen(&wp_tankPos, &wp_pt))
			{
				fprintf(stderr, "[WP_CLICK] f=%u tank world (%.0f,%.0f,%.0f) -> screen (%d,%d)\n",
					wp_f, wp_tankPos.x, wp_tankPos.y, wp_tankPos.z, wp_pt.x, wp_pt.y);
				wp_sendClick(wp_pt.x, wp_pt.y, TRUE);
				wp_stage = 1;
			}
			else if (wp_f >= 500)
			{
				fprintf(stderr, "[WP_CLICK] f=%u FAIL: no tank or off-screen\n", wp_f);
				wp_stage = 99;
			}
		}
		else if (wp_stage == 1) { wp_sendClick(wp_pt.x, wp_pt.y, FALSE); wp_stage = 2; }
		else if (wp_stage == 2 && wp_f >= 510)
		{
			fprintf(stderr, "[WP_CLICK] f=%u selectCount=%d\n", wp_f, (int)TheInGameUI->getSelectCount());
			// click a ground point 100 world units east of the tank
			Coord3D dest = wp_tankPos; dest.x += 100.0f;
			if (TheTacticalView->worldToScreen(&dest, &wp_pt))
			{
				wp_sendClick(wp_pt.x, wp_pt.y, TRUE);
				wp_stage = 3;
			}
			else wp_stage = 99;
		}
		else if (wp_stage == 3) { wp_sendClick(wp_pt.x, wp_pt.y, FALSE); wp_stage = 4; }
		else if (wp_stage == 4 && wp_f >= 900)
		{
			for (Object* o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getTemplate()->getName() == "WP_Tank")
				{
					fprintf(stderr, "[WP_CLICK] f=%u RESULT tank at (%.0f,%.0f) (started (%.0f,%.0f), move target x+100)\n",
						wp_f, o->getPosition()->x, o->getPosition()->y, wp_tankPos.x, wp_tankPos.y);
					break;
				}
			}
			wp_stage = 5;
		}
	}
}
