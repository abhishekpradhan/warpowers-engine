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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// GameEngine.cpp /////////////////////////////////////////////////////////////////////////////////
// Implementation of the Game Engine singleton
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
#include "Common/AudioAffect.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/FramePacer.h"
#include "Common/Radar.h"
#include "Common/Player.h"  // WarPowers @debug WP_AUTOTEST
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"  // WarPowers @debug WP_AUTOTEST
#include "GameLogic/Object.h"  // WarPowers @debug WP_AUTOTEST
#include "GameLogic/Module/BodyModule.h"  // WarPowers @debug WP_AUTOTEST
#include "GameClient/ControlBar.h"  // WarPowers @debug WP_AUTOTEST
#include "GameClient/InGameUI.h"  // WarPowers @debug WP_AUTOTEST=husk
#include "GameClient/Display.h"  // WarPowers @debug WP_AUTOTEST=husk
#include "GameClient/View.h"  // WarPowers @debug WP_AUTOTEST
#include "GameLogic/Module/ProductionUpdate.h"  // WarPowers @debug WP_AUTOTEST
#include "GameLogic/Module/SupplyTruckAIUpdate.h"  // War Powers economy diagnostic
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "Common/ScoreKeeper.h"  // WarPowers @debug WP_AI_TRACE score probe
extern void WPArmMissionDiagnosticResult(Bool victory);
extern Int WPGetMissionDiagnosticResult();
#include "GameLogic/SidesList.h"  // WarPowers @debug WP_AI_TRACE (BuildListInfo)
// WarPowers @debug WP_AI_TRACE: symbolized backtrace for the named-object
// forensics (kept out of the headers). No execinfo under emscripten.
#ifndef __EMSCRIPTEN__
#include <execinfo.h>
void WPPrintBacktrace()
{
	void* wp_frames[24];
	int wp_n = backtrace(wp_frames, 24);
	backtrace_symbols_fd(wp_frames, wp_n, 2);
}
#else
void WPPrintBacktrace() {}
#endif
#include "GameLogic/TerrainLogic.h"  // WarPowers @debug WP_AUTOTEST=ghost
#include "GameLogic/AIPathfind.h"  // WarPowers @debug WP_AUTOTEST=ghost
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/RandomValue.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModuleFactory.h"
#include "Common/Debug.h"
#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/Science.h"
#include "Common/FunctionLexicon.h"
#include "Common/CommandLine.h"
#include "Common/DamageFX.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/Upgrade.h"
#include "Common/OptionPreferences.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/GameLOD.h"
#include "Common/Registry.h"
#include "Common/GameCommon.h"	// FOR THE ALLOW_DEBUG_CHEATS_IN_RELEASE #define

#include "GameLogic/Armor.h"
#include "GameLogic/AI.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"

#include "GameClient/ClientInstance.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"

#include "Common/version.h"


//-------------------------------------------------------------------------------------------------

#ifdef DEBUG_CRC
class DeepCRCSanityCheck : public SubsystemInterface
{
public:
	DeepCRCSanityCheck() {}
	virtual ~DeepCRCSanityCheck() {}

	virtual void init() {}
	virtual void reset();
	virtual void update() {}

protected:
};

DeepCRCSanityCheck *TheDeepCRCSanityCheck = nullptr;

void DeepCRCSanityCheck::reset()
{
	static Int timesThrough = 0;
	static UnsignedInt lastCRC = 0;

	AsciiString fname;
	fname.format("%sCRCAfter%dMaps.dat", TheGlobalData->getPath_UserData().str(), timesThrough);
	UnsignedInt thisCRC = TheGameLogic->getCRC( CRC_RECALC, fname );

	DEBUG_LOG(("DeepCRCSanityCheck: CRC is %X", thisCRC));
	DEBUG_ASSERTCRASH(timesThrough == 0 || thisCRC == lastCRC,
		("CRC after reset did not match beginning CRC!\nNetwork games won't work after this.\nOld: 0x%8.8X, New: 0x%8.8X",
		lastCRC, thisCRC));
	lastCRC = thisCRC;

	timesThrough++;
}
#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
/// The GameEngine singleton instance
GameEngine *TheGameEngine = nullptr;

//-------------------------------------------------------------------------------------------------
SubsystemInterfaceList* TheSubsystemList = nullptr;

//-------------------------------------------------------------------------------------------------
template<class SUBSYSTEM>
void initSubsystem(
	SUBSYSTEM*& sysref,
	AsciiString name,
	SUBSYSTEM* sys,
	Xfer *pXfer,
	const char* path1 = nullptr,
	const char* path2 = nullptr)
{
	sysref = sys;
	TheSubsystemList->initSubsystem(sys, path1, path2, pXfer, name);
}

//-------------------------------------------------------------------------------------------------
extern HINSTANCE ApplicationHInstance;  ///< our application instance
// TheSuperHackers @build fighter19 11/02/2026 COM module (Windows-only)
#ifdef _WIN32
extern CComModule _Module;
#endif

//-------------------------------------------------------------------------------------------------
static void updateTGAtoDDS();

//-------------------------------------------------------------------------------------------------
static void updateWindowTitle()
{
	// TheSuperHackers @tweak Now prints product and version information in the Window title.

	DEBUG_ASSERTCRASH(TheVersion != nullptr, ("TheVersion is null"));
	DEBUG_ASSERTCRASH(TheGameText != nullptr, ("TheGameText is null"));

	UnicodeString title;

	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		UnicodeString str;
		str.format(L"Instance:%.2u", rts::ClientInstance::getInstanceId());
		title.concat(str);
	}

	UnicodeString productString = TheVersion->getUnicodeProductString();

	if (!productString.isEmpty())
	{
		if (!title.isEmpty())
			title.concat(L" ");
		title.concat(productString);
	}

#if RTS_GENERALS
	const WideChar* defaultGameTitle = L"Command and Conquer Generals";
#elif RTS_ZEROHOUR
	const WideChar* defaultGameTitle = L"Command and Conquer Generals Zero Hour";
#endif
	UnicodeString gameTitle = TheGameText->FETCH_OR_SUBSTITUTE("GUI:Command&ConquerGenerals", defaultGameTitle);

	if (!gameTitle.isEmpty())
	{
		UnicodeString gameTitleFinal;
		UnicodeString gameVersion = TheVersion->getUnicodeVersion();

		if (productString.isEmpty())
		{
			gameTitleFinal = gameTitle;
		}
		else
		{
			UnicodeString gameTitleFormat = TheGameText->FETCH_OR_SUBSTITUTE("Version:GameTitle", L"for %ls");
			gameTitleFinal.format(gameTitleFormat.str(), gameTitle.str());
		}

		if (!title.isEmpty())
			title.concat(L" ");
		title.concat(gameTitleFinal.str());
		title.concat(L" ");
		title.concat(gameVersion.str());
	}

	if (!title.isEmpty())
	{
		AsciiString titleA;
		titleA.translate(title);	//get ASCII version for Win 9x

		extern HWND ApplicationHWnd;  ///< our application window handle
		if (ApplicationHWnd) {
// TheSuperHackers @build fighter19 11/02/2026 SetWindowText is Windows-only
#ifdef _WIN32
			//Set it twice because Win 9x does not support SetWindowTextW.
			::SetWindowText(ApplicationHWnd, titleA.str());
			::SetWindowTextW(ApplicationHWnd, title.str());
#else
			// Linux: SDL3 handles window title (set via SDL_SetWindowTitle if needed)
#endif
		}
	}
}

//-------------------------------------------------------------------------------------------------
GameEngine::GameEngine()
{
	// initialize to non garbage values
	m_logicTimeAccumulator = 0.0f;
	m_quitting = FALSE;
	m_isActive = FALSE;

// TheSuperHackers @build fighter19 11/02/2026 COM initialization (Windows-only)
#ifdef _WIN32
	_Module.Init(nullptr, ApplicationHInstance, nullptr);
#endif
}

//-------------------------------------------------------------------------------------------------
GameEngine::~GameEngine()
{
	//extern std::vector<std::string>	preloadTextureNamesGlobalHack;
	//preloadTextureNamesGlobalHack.clear();

	delete TheMapCache;
	TheMapCache = nullptr;

//	delete TheShell;
//	TheShell = nullptr;

	TheGameResultsQueue->endThreads();

	// TheSuperHackers @fix helmutbuhler 03/06/2025
	// Reset all subsystems before deletion to prevent crashing due to cross dependencies.
	reset();

	TheSubsystemList->shutdownAll();
	delete TheSubsystemList;
	TheSubsystemList = nullptr;

	delete TheSkirmishGameInfo;
	TheSkirmishGameInfo = nullptr;

	delete TheChallengeGameInfo;
	TheChallengeGameInfo = nullptr;

	delete TheNetwork;
	TheNetwork = nullptr;

	delete TheCommandList;
	TheCommandList = nullptr;

	delete TheNameKeyGenerator;
	TheNameKeyGenerator = nullptr;

	delete TheFileSystem;
	TheFileSystem = nullptr;

	delete TheGameLODManager;
	TheGameLODManager = nullptr;

	Drawable::killStaticImages();

// TheSuperHackers @build fighter19 11/02/2026 COM termination (Windows-only)
#ifdef _WIN32
	_Module.Term();
#endif

#ifdef PERF_TIMERS
	PerfGather::termPerfDump();
#endif
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isTimeFrozen()
{
	// TheSuperHackers @fix The time can no longer be frozen in Network games. It would disconnect the player.
	if (TheNetwork != nullptr)
		return false;

	if (TheTacticalView != nullptr)
	{
		if (TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished())
			return true;
	}

	if (TheScriptEngine != nullptr)
	{
		if (TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript())
			return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isGameHalted()
{
	if (TheNetwork != nullptr)
	{
		if (TheNetwork->isStalling())
			return true;
	}
	else
	{
		if (TheGameLogic != nullptr && TheGameLogic->isGamePaused())
			return true;
	}

	return false;
}

/** -----------------------------------------------------------------------------------------------
 * Initialize the game engine by initializing the GameLogic and GameClient.
 */
void GameEngine::init()
{
	try {
		//create an INI object to use for loading stuff
		INI ini;

#ifdef DEBUG_LOGGING
		if (TheVersion)
		{
			DEBUG_LOG(("================================================================================"));
			DEBUG_LOG(("Generals version %s", TheVersion->getAsciiVersion().str()));
			DEBUG_LOG(("Build date: %s", TheVersion->getAsciiBuildTime().str()));
			DEBUG_LOG(("Build location: %s", TheVersion->getAsciiBuildLocation().str()));
			DEBUG_LOG(("Build user: %s", TheVersion->getAsciiBuildUser().str()));
			DEBUG_LOG(("Build git revision: %s", TheVersion->getAsciiGitCommitCount().str()));
			DEBUG_LOG(("Build git version: %s", TheVersion->getAsciiGitTagOrHash().str()));
			DEBUG_LOG(("Build git commit time: %s", TheVersion->getAsciiGitCommitTime().str()));
			DEBUG_LOG(("Build git commit author: %s", Version::getGitCommitAuthorName()));
			DEBUG_LOG(("================================================================================"));
		}
#endif

	#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)
		DEBUG_LOG(("Calculating CPU frequency for performance timers."));
		InitPrecisionTimer();
	#endif
	#ifdef PERF_TIMERS
		PerfGather::initPerfDump("AAAPerfStats", PerfGather::PERF_NETTIME);
	#endif




	#ifdef DUMP_PERF_STATS////////////////////////////////////////////////////////////
	__int64 startTime64;//////////////////////////////////////////////////////////////
	__int64 endTime64,freq64;///////////////////////////////////////////////////////////
	GetPrecisionTimerTicksPerSec(&freq64);///////////////////////////////////////////////
	GetPrecisionTimer(&startTime64);////////////////////////////////////////////////////
  char Buf[256];//////////////////////////////////////////////////////////////////////
	#endif//////////////////////////////////////////////////////////////////////////////


		TheSubsystemList = MSGNEW("GameEngineSubsystem") SubsystemInterfaceList;

		TheSubsystemList->addSubsystem(this);

		// initialize the random number system
		InitRandom();

		// Create the low-level file system interface
		TheFileSystem = createFileSystem();

		// not part of the subsystem list, because it should normally never be reset!
		TheNameKeyGenerator = MSGNEW("GameEngineSubsystem") NameKeyGenerator;
		TheNameKeyGenerator->init();


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheNameKeyGenerator  = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// not part of the subsystem list, because it should normally never be reset!
		TheCommandList = MSGNEW("GameEngineSubsystem") CommandList;
		TheCommandList->init();

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheCommandList  = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		XferCRC xferCRC;
		xferCRC.open("lightCRC");


		initSubsystem(TheLocalFileSystem, "TheLocalFileSystem", createLocalFileSystem(), nullptr);


    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheLocalFileSystem  = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheArchiveFileSystem, "TheArchiveFileSystem", createArchiveFileSystem(), nullptr); // this MUST come after TheLocalFileSystem creation

    	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheArchiveFileSystem  = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		DEBUG_ASSERTCRASH(TheWritableGlobalData,("TheWritableGlobalData expected to be created"));
	initSubsystem(TheWritableGlobalData, "TheWritableGlobalData", TheWritableGlobalData, &xferCRC, "Data\\INI\\Default\\GameData", "Data\\INI\\GameData");
	TheWritableGlobalData->parseCustomDefinition();

	// GeneralsX @feature felipebraz 08/06/2026 Auto-create SagePatch.ini in user data dir with defaults.
	// This replaces the run.sh copy approach with engine-managed defaults.
	{
		AsciiString sagePatchPath = TheWritableGlobalData->getPath_UserData();
		sagePatchPath.concat("SagePatch.ini");

		if (!TheLocalFileSystem->doesFileExist(sagePatchPath.str()))
		{
			FILE *f = fopen(sagePatchPath.str(), "w");
			if (f)
			{
				fprintf(f,
				"; -----------------------------------------------------------------------------\n"
				"; SagePatch - Casual QoL overrides for GeneralsX\n"
				";\n"
				"; Loaded by the engine after the BIG-archived Data/INI/GameData.ini, so values\n"
				"; here override (not append to) the originals.\n"
				"; -----------------------------------------------------------------------------\n"
				"\n"
				"GameData\n"
				"  ; Slightly higher than vanilla (310); further out without seeing past the map border.\n"
				"  MaxCameraHeight = 350.0\n"
				"  ; Slightly lower than vanilla (120) so casual zoom-in feels useful.\n"
				"  MinCameraHeight = 100.0\n"
					"  ; Still soft-disabled so the user can push past max without a hard clamp.\n"
					"  EnforceMaxCameraHeight = No\n"
					"  ; Keyboard scroll - vanilla 0.5 is sluggish, double it.\n"
					"  KeyboardScrollSpeedFactor = 1.0\n"
					"  ; ~5% more terrain drawn at max zoom to fix terrain pop-in.\n"
					"  TerrainDrawDistanceScale = 1.05\n"
					// GeneralsX @tweak felipebraz 20/06/2026 Default render FPS limit to 60 FPS in SagePatch.ini
					"  UseFPSLimit = Yes\n"
					"  FramesPerSecondLimit = 60\n"
					"End\n"
				);
				fclose(f);
			}
		}

		if (TheLocalFileSystem->doesFileExist(sagePatchPath.str()))
		{
			// Check and migrate existing SagePatch.ini for 60 FPS
			FILE *f = fopen(sagePatchPath.str(), "rb");
			if (f)
			{
				fseek(f, 0, SEEK_END);
				long size = ftell(f);
				fseek(f, 0, SEEK_SET);
				char *buffer = new char[size + 1];
				fread(buffer, 1, size, f);
				buffer[size] = 0;
				fclose(f);

				if (!strstr(buffer, "FramesPerSecondLimit"))
				{
					char *endPos = strstr(buffer, "End");
					if (endPos != nullptr)
					{
						*endPos = '\0';
						FILE *fw = fopen(sagePatchPath.str(), "wb");
						if (fw)
						{
							fprintf(fw, "%s  ; Migrated 60 FPS defaults\n  UseFPSLimit = Yes\n  FramesPerSecondLimit = 60\nEnd\n", buffer);
							fclose(fw);
						}
					}
				}
				delete[] buffer;
			}

			ini.load(sagePatchPath, INI_LOAD_OVERWRITE, nullptr);
		}
	}

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After  TheWritableGlobalData = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



	#if defined(RTS_DEBUG)
		// If we're in Debug, load the Debug settings as well.
		ini.loadFileDirectory( "Data\\INI\\GameDataDebug", INI_LOAD_OVERWRITE, nullptr );
	#endif

		// special-case: parse command-line parameters after loading global data
		CommandLine::parseCommandLineForEngineInit();

		TheArchiveFileSystem->loadMods();

		// doesn't require resets so just create a single instance here.
		TheGameLODManager = MSGNEW("GameEngineSubsystem") GameLODManager;
		TheGameLODManager->init();

		// after parsing the command line, we may want to perform dds stuff. Do that here.
		if (TheGlobalData->m_shouldUpdateTGAToDDS) {
			// update any out of date targas here.
			updateTGAtoDDS();
		}

		// read the water settings from INI (must do prior to initing GameClient, apparently)
		ini.loadFileDirectory( "Data\\INI\\Default\\Water", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Water", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Default\\Weather", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Weather", INI_LOAD_OVERWRITE, &xferCRC );



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After water INI's = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#ifdef DEBUG_CRC
		initSubsystem(TheDeepCRCSanityCheck, "TheDeepCRCSanityCheck", MSGNEW("GameEngineSubystem") DeepCRCSanityCheck, nullptr);
#endif // DEBUG_CRC
		initSubsystem(TheGameText, "TheGameText", CreateGameTextInterface(), nullptr);
		updateWindowTitle();

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameText = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#if RETAIL_COMPATIBLE_CRC
		if (xferCRC.getCRC() == 0xA1E7F8E6)
			TheNameKeyGenerator->verifyNameKeyID(1);
#endif

		initSubsystem(TheScienceStore,"TheScienceStore", MSGNEW("GameEngineSubsystem") ScienceStore(), &xferCRC, "Data\\INI\\Default\\Science", "Data\\INI\\Science");
		initSubsystem(TheMultiplayerSettings,"TheMultiplayerSettings", MSGNEW("GameEngineSubsystem") MultiplayerSettings(), &xferCRC, "Data\\INI\\Default\\Multiplayer", "Data\\INI\\Multiplayer");
		initSubsystem(TheTerrainTypes,"TheTerrainTypes", MSGNEW("GameEngineSubsystem") TerrainTypeCollection(), &xferCRC, "Data\\INI\\Default\\Terrain", "Data\\INI\\Terrain");
		initSubsystem(TheTerrainRoads,"TheTerrainRoads", MSGNEW("GameEngineSubsystem") TerrainRoadCollection(), &xferCRC, "Data\\INI\\Default\\Roads", "Data\\INI\\Roads");
		initSubsystem(TheGlobalLanguageData,"TheGlobalLanguageData",MSGNEW("GameEngineSubsystem") GlobalLanguage, nullptr); // must be before the game text
		TheGlobalLanguageData->parseCustomDefinition();
	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGlobalLanguageData = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
		initSubsystem(TheAudio,"TheAudio", createAudioManager(TheGlobalData->m_headless), nullptr);
#ifndef __EMSCRIPTEN__
		// (retail CD-presence heuristic — on wasm music archives are optional)
		if (!TheAudio->isMusicAlreadyLoaded())
			setQuitting(TRUE);
#endif

#if RTS_ZEROHOUR && RETAIL_COMPATIBLE_CRC
		TheNameKeyGenerator->syncNameKeyID();
#endif

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheAudio = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFunctionLexicon,"TheFunctionLexicon", createFunctionLexicon(), nullptr);
		initSubsystem(TheModuleFactory,"TheModuleFactory", createModuleFactory(), nullptr);
		initSubsystem(TheMessageStream,"TheMessageStream", createMessageStream(), nullptr);
		initSubsystem(TheSidesList,"TheSidesList", MSGNEW("GameEngineSubsystem") SidesList(), nullptr);
		initSubsystem(TheCaveSystem,"TheCaveSystem", MSGNEW("GameEngineSubsystem") CaveSystem(), nullptr);
		initSubsystem(TheRankInfoStore,"TheRankInfoStore", MSGNEW("GameEngineSubsystem") RankInfoStore(), &xferCRC, nullptr, "Data\\INI\\Rank");
		initSubsystem(ThePlayerTemplateStore,"ThePlayerTemplateStore", MSGNEW("GameEngineSubsystem") PlayerTemplateStore(), &xferCRC, "Data\\INI\\Default\\PlayerTemplate", "Data\\INI\\PlayerTemplate");
		initSubsystem(TheParticleSystemManager,"TheParticleSystemManager", createParticleSystemManager(TheGlobalData->m_headless), nullptr);

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheParticleSystemManager = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFXListStore,"TheFXListStore", MSGNEW("GameEngineSubsystem") FXListStore(), &xferCRC, "Data\\INI\\Default\\FXList", "Data\\INI\\FXList");
		initSubsystem(TheWeaponStore,"TheWeaponStore", MSGNEW("GameEngineSubsystem") WeaponStore(), &xferCRC, nullptr, "Data\\INI\\Weapon");
		initSubsystem(TheObjectCreationListStore,"TheObjectCreationListStore", MSGNEW("GameEngineSubsystem") ObjectCreationListStore(), &xferCRC, "Data\\INI\\Default\\ObjectCreationList", "Data\\INI\\ObjectCreationList");
		initSubsystem(TheLocomotorStore,"TheLocomotorStore", MSGNEW("GameEngineSubsystem") LocomotorStore(), &xferCRC, nullptr, "Data\\INI\\Locomotor");
		initSubsystem(TheSpecialPowerStore,"TheSpecialPowerStore", MSGNEW("GameEngineSubsystem") SpecialPowerStore(), &xferCRC, "Data\\INI\\Default\\SpecialPower", "Data\\INI\\SpecialPower");
		initSubsystem(TheDamageFXStore,"TheDamageFXStore", MSGNEW("GameEngineSubsystem") DamageFXStore(), &xferCRC, nullptr, "Data\\INI\\DamageFX");
		initSubsystem(TheArmorStore,"TheArmorStore", MSGNEW("GameEngineSubsystem") ArmorStore(), &xferCRC, nullptr, "Data\\INI\\Armor");
		initSubsystem(TheBuildAssistant,"TheBuildAssistant", MSGNEW("GameEngineSubsystem") BuildAssistant, nullptr);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheBuildAssistant = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////



		initSubsystem(TheThingFactory,"TheThingFactory", createThingFactory(), &xferCRC, "Data\\INI\\Default\\Object", "Data\\INI\\Object");

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheThingFactory = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


#if RETAIL_COMPATIBLE_CRC
		if (xferCRC.getCRC() == 0x6209AF6E)
			TheNameKeyGenerator->verifyNameKeyID(2265);
#endif

		initSubsystem(TheUpgradeCenter,"TheUpgradeCenter", MSGNEW("GameEngineSubsystem") UpgradeCenter, &xferCRC, "Data\\INI\\Default\\Upgrade", "Data\\INI\\Upgrade");
		initSubsystem(TheGameClient,"TheGameClient", createGameClient(), nullptr);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameClient = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheAI,"TheAI", MSGNEW("GameEngineSubsystem") AI(), &xferCRC,  "Data\\INI\\Default\\AIData", "Data\\INI\\AIData");
		initSubsystem(TheGameLogic,"TheGameLogic", createGameLogic(), nullptr);
		initSubsystem(TheTeamFactory,"TheTeamFactory", MSGNEW("GameEngineSubsystem") TeamFactory(), nullptr);
		initSubsystem(TheCrateSystem,"TheCrateSystem", MSGNEW("GameEngineSubsystem") CrateSystem(), &xferCRC, "Data\\INI\\Default\\Crate", "Data\\INI\\Crate");
		initSubsystem(ThePlayerList,"ThePlayerList", MSGNEW("GameEngineSubsystem") PlayerList(), nullptr);
		initSubsystem(TheRecorder,"TheRecorder", createRecorder(), nullptr);
		initSubsystem(TheRadar,"TheRadar", createRadar(TheGlobalData->m_headless), nullptr);
		initSubsystem(TheVictoryConditions,"TheVictoryConditions", createVictoryConditions(), nullptr);



	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheVictoryConditions = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		AsciiString fname;
		fname.format("Data\\%s\\CommandMap", GetRegistryLanguage().str());
		initSubsystem(TheMetaMap,"TheMetaMap", MSGNEW("GameEngineSubsystem") MetaMap(), nullptr, fname.str(), "Data\\INI\\CommandMap");

#if defined(RTS_DEBUG)
		ini.loadFileDirectory("Data\\INI\\CommandMapDebug", INI_LOAD_MULTIFILE, nullptr);
#endif

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		ini.loadFileDirectory("Data\\INI\\CommandMapDemo", INI_LOAD_MULTIFILE, nullptr);
#endif

		TheMetaMap->generateMetaMap();
		TheMetaMap->verifyMetaMap();


		initSubsystem(TheActionManager,"TheActionManager", MSGNEW("GameEngineSubsystem") ActionManager(), nullptr);
		//initSubsystem((CComObject<WebBrowser> *)TheWebBrowser,"(CComObject<WebBrowser> *)TheWebBrowser", (CComObject<WebBrowser> *)createWebBrowser(), nullptr);
		initSubsystem(TheGameStateMap,"TheGameStateMap", MSGNEW("GameEngineSubsystem") GameStateMap, nullptr );
		initSubsystem(TheGameState,"TheGameState", MSGNEW("GameEngineSubsystem") GameState, nullptr );

		// Create the interface for sending game results
		initSubsystem(TheGameResultsQueue,"TheGameResultsQueue", GameResultsInterface::createNewGameResultsInterface(), nullptr);


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGameResultsQueue = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		xferCRC.close();
		TheWritableGlobalData->m_iniCRC = xferCRC.getCRC();
		DEBUG_LOG(("INI CRC is 0x%8.8X", TheGlobalData->m_iniCRC));

		TheSubsystemList->postProcessLoadAll();

		// GeneralsX @bugfix Copilot 11/05/2026 Prevent uncapped render when FPS limiter is enabled but no valid limit value was loaded.
		if (TheGlobalData->m_useFpsLimit && TheGlobalData->m_framesPerSecondLimit <= 0)
		{
			TheWritableGlobalData->m_framesPerSecondLimit = BaseFps;
		}

		TheFramePacer->setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);

		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_musicOn, AudioAffect_Music);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_soundsOn, AudioAffect_Sound);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_sounds3DOn, AudioAffect_Sound3D);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_speechOn, AudioAffect_Speech);

		// We're not in a network game yet, so set the network singleton to nullptr.
		TheNetwork = nullptr;

		//Create a default ini file for options if it doesn't already exist.
		//OptionPreferences prefs( TRUE );

		// If we turn m_quitting to FALSE here, then we throw away any requests to quit that
		// took place during loading. :-\ - jkmcd
		// If this really needs to take place, please make sure that pressing cancel on the audio
		// load music dialog will still cause the game to quit.
		// m_quitting = FALSE;

		// initialize the MapCache
		TheMapCache = MSGNEW("GameEngineSubsystem") MapCache;
		TheMapCache->updateCache();


	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheMapCache->updateCache = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		if (TheGlobalData->m_buildMapCache)
		{
			// just quit, since the map cache has already updated
			//populateMapListbox(nullptr, true, true);
			m_quitting = TRUE;
		}

		// load the initial shell screen
		TheShell->push( "Menus/MainMenu.wnd" );

		// This allows us to run a map from the command line
		if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
		{
			AsciiString fname = TheGlobalData->m_initialFile;
			fname.toLower();

			if (fname.endsWithNoCase(".map"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = TheGlobalData->m_initialFile;

				// shutdown the top, but do not pop it off the stack
	//			TheShell->hideShell();

				// WarPowers @debug WP_DIFFICULTY=0/1/2 overrides the -file
				// match difficulty (headless testing of the per-difficulty AI).
				GameDifficulty wpDiff = DIFFICULTY_NORMAL;
				{
					const char* wpDiffEnv = getenv("WP_DIFFICULTY");
					if (wpDiffEnv && *wpDiffEnv >= '0' && *wpDiffEnv <= '2')
						wpDiff = (GameDifficulty)(*wpDiffEnv - '0');
				}

				// send a message to the logic for a new game
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(wpDiff);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
		}
		else
		{
			// WarPowers @feature WP_BOOT_MAP: boot straight into a map like
			// -file, but WITHOUT m_initialFile — so the post-match flow
			// returns to the in-engine shell (score screen, redeploy)
			// instead of quitting to desktop. The web page's DEPLOY button
			// uses this; -file keeps its quit-after-match semantics for the
			// autotest harness. Long map path required (Maps\X\X.map).
			const char* wpBootMap = getenv("WP_BOOT_MAP");
			if (wpBootMap && *wpBootMap)
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = wpBootMap;

				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(DIFFICULTY_NORMAL);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
		}

		//
		if (TheMapCache && TheGlobalData->m_shellMapOn)
		{
			AsciiString lowerName = TheGlobalData->m_shellMapName;
			lowerName.toLower();

			MapCache::const_iterator it = TheMapCache->find(lowerName);
			if (it == TheMapCache->end())
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
			}
		}

	}
	catch (ErrorCode ec)
	{
		if (ec == ERROR_INVALID_D3D)
		{
			RELEASE_CRASHLOCALIZED("ERROR:D3DFailurePrompt", "ERROR:D3DFailureMessage");
		}
	}
	catch (INIException e)
	{
		if (e.mFailureMessage)
			RELEASE_CRASH((e.mFailureMessage));
		else
			RELEASE_CRASH(("Uncaught Exception during initialization."));

	}
	catch (...)
	{
		// WarPowers: the INI layer prints file/line before bare-int throws;
		// see INI::load's unknown-block path.
		fprintf(stderr, "FATAL: uncaught exception during initialization (see INI error above)\n");
		fflush(stderr);
		RELEASE_CRASH(("Uncaught Exception during initialization."));
	}

	resetSubsystems();

	HideControlBar();
}

/** -----------------------------------------------------------------------------------------------
	* Reset all necessary parts of the game engine to be ready to accept new game data
	*/
void GameEngine::reset()
{

	WindowLayout *background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(background,("We Couldn't Load Menus/BlankWindow.wnd"));
	background->hide(FALSE);
	background->bringForward();
	background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	Bool deleteNetwork = false;
	if (TheGameLogic->isInMultiplayerGame())
		deleteNetwork = true;

	resetSubsystems();

	if (deleteNetwork)
	{
		DEBUG_ASSERTCRASH(TheNetwork, ("Deleting null TheNetwork!"));
		delete TheNetwork;
		TheNetwork = nullptr;
	}
	if(background)
	{
		background->destroyWindows();
		deleteInstance(background);
		background = nullptr;
	}
}

/// -----------------------------------------------------------------------------------------------
void GameEngine::resetSubsystems()
{
	// TheSuperHackers @fix xezon 09/06/2025 Reset GameLogic first to purge all world objects early.
	// This avoids potentially catastrophic issues when objects and subsystems have cross dependencies.
	TheGameLogic->reset();

	TheSubsystemList->resetAll();
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateGameLogic(UnsignedInt logicTimeQueryFlags)
{
	// This updates the paused game status of the game logic.
	TheGameLogic->preUpdate();

	TheFramePacer->setTimeFrozen(isTimeFrozen());
	TheFramePacer->setGameHalted(isGameHalted());

	if (TheNetwork != nullptr)
	{
		return canUpdateNetworkGameLogic();
	}
	else
	{
		return canUpdateRegularGameLogic(logicTimeQueryFlags);
	}
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateNetworkGameLogic()
{
	DEBUG_ASSERTCRASH(TheNetwork != nullptr, ("TheNetwork is null"));

	if (TheNetwork->isFrameDataReady())
	{
		// Important: The Network is definitely no longer stalling.
		TheFramePacer->setGameHalted(false);

		return true;
	}

	return false;
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateRegularGameLogic(UnsignedInt logicTimeQueryFlags)
{
	const Int logicTimeScaleFps = TheFramePacer->getActualLogicTimeScaleFps(logicTimeQueryFlags);

	if (logicTimeScaleFps <= 0)
	{
		return false;
	}

	const Bool enabled = TheFramePacer->isLogicTimeScaleEnabled();
	const Int maxRenderFps = TheFramePacer->getActualFramesPerSecondLimit();

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	const Bool useFastMode = TheGlobalData->m_TiVOFastMode;
#else	//always allow this cheat key if we're in a replay game.
	const Bool useFastMode = TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame();
#endif

	if (useFastMode || !enabled || logicTimeScaleFps >= maxRenderFps)
	{
		// Logic time scale is uncapped or larger equal Render FPS. Update straight away.
		return true;
	}
	else
	{
		// TheSuperHackers @tweak xezon 06/08/2025
		// The logic time step is now decoupled from the render update.
		const Real targetFrameTime = 1.0f / logicTimeScaleFps;
		m_logicTimeAccumulator += min(TheFramePacer->getUpdateTime(), targetFrameTime);

		if (m_logicTimeAccumulator >= targetFrameTime)
		{
			m_logicTimeAccumulator -= targetFrameTime;
			return true;
		}
	}

	return false;
}

/// -----------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(GameEngine_update)

/** -----------------------------------------------------------------------------------------------
 * Update the game engine by updating the GameClient and GameLogic singletons.
 */
void GameEngine::update()
{
	USE_PERF_TIMER(GameEngine_update)
	{
		{
			// VERIFY CRC needs to be in this code block.  Please to not pull TheGameLogic->update() inside this block.
			VERIFY_CRC

			TheRadar->UPDATE();

			/// @todo Move audio init, update, etc, into GameClient update

			TheAudio->UPDATE();

			// WarPowers @feature music rotation: cycles the Track_WP_* list
			// (Music.ini) with a short silence between tracks. Runs in menu
			// and match alike; respects the mixer's music volume.
			{
				static const char* const s_wpTracks[] = {
					"Track_WP_01", "Track_WP_02", "Track_WP_03",
					"Track_WP_04", "Track_WP_05", "Track_WP_06",
				};
				static AudioHandle s_wpMusicHandle = 0;
				static Int s_wpTrackIdx = -1;
				static Int s_wpMusicGap = 150;   // ~5s before the first track
				if (TheAudio)
				{
					if (s_wpMusicHandle != 0 && TheAudio->isCurrentlyPlaying(s_wpMusicHandle))
					{
						// still playing
					}
					else if (s_wpMusicGap > 0)
					{
						--s_wpMusicGap;
					}
					else
					{
						s_wpTrackIdx = (s_wpTrackIdx + 1) % (Int)ARRAY_SIZE(s_wpTracks);
						AudioEventRTS wpTrack(s_wpTracks[s_wpTrackIdx]);
						s_wpMusicHandle = TheAudio->addAudioEvent(&wpTrack);
						s_wpMusicGap = 240;      // ~8s of quiet between tracks
					}
				}
			}

			// WarPowers @debug WP_AI_TRACE: computer-player forensics for the
			// Phase 4 opponent. Every ~10s print each AI player's economy and
			// army so a headless run shows whether it trains dozers, expands
			// its build list, and produces attack teams.
			{
				static const char* wp_aiEnv = getenv("WP_AI_TRACE");
				if (wp_aiEnv && TheGameLogic && TheGameLogic->isInGame() &&
					ThePlayerList && (TheGameLogic->getFrame() % 300 == 0))
				{
					// scorekeeper probe: every playable player's per-victim
					// building-kill buckets (feeds PLAYER_DESTROYED_N_BUILDINGS_PLAYER)
					for (Int wp_si = 0; wp_si < ThePlayerList->getPlayerCount(); ++wp_si)
					{
						Player* wp_sp = ThePlayerList->getNthPlayer(wp_si);
						if (!wp_sp || wp_sp->getPlayerTemplate() == nullptr) continue;
						ScoreKeeper* wp_sk = wp_sp->getScoreKeeper();
						if (!wp_sk) continue;
						fprintf(stderr, "[WPSCORE] p=%d bldKilled=[%d,%d,%d,%d] total=%d\n",
							wp_si,
							wp_sk->getTotalBuildingsDestroyedOfPlayer(0),
							wp_sk->getTotalBuildingsDestroyedOfPlayer(1),
							wp_sk->getTotalBuildingsDestroyedOfPlayer(2),
							wp_sk->getTotalBuildingsDestroyedOfPlayer(3),
							wp_sk->getTotalBuildingsDestroyed());
					}
					for (Int wp_pi = 0; wp_pi < ThePlayerList->getPlayerCount(); ++wp_pi)
					{
						Player* wp_pl = ThePlayerList->getNthPlayer(wp_pi);
						if (!wp_pl || wp_pl->getPlayerType() != PLAYER_COMPUTER)
							continue;
						if (wp_pl->getPlayerTemplate() == nullptr)
							continue;   // neutral/civilian slots
						Int wp_structs = 0, wp_units = 0, wp_dozers = 0, wp_underCon = 0;
						for (Object* wp_o = TheGameLogic->getFirstObject(); wp_o; wp_o = wp_o->getNextObject())
						{
							if (wp_o->getControllingPlayer() != wp_pl) continue;
							if (wp_o->isEffectivelyDead()) continue;
							if (wp_o->isKindOf(KINDOF_STRUCTURE)) {
								++wp_structs;
								if (wp_o->getStatusBits().test(OBJECT_STATUS_UNDER_CONSTRUCTION)) ++wp_underCon;
							} else if (wp_o->isKindOf(KINDOF_INFANTRY) || wp_o->isKindOf(KINDOF_VEHICLE)) {
								++wp_units;
								if (wp_o->isKindOf(KINDOF_DOZER)) ++wp_dozers;
							}
						}
						fprintf(stderr, "[WPAI] f=%u p=%d money=%d structs=%d(uc=%d) units=%d dozers=%d bldU=%d bldB=%d\n",
							TheGameLogic->getFrame(), wp_pi, wp_pl->getMoney()->countMoney(),
							wp_structs, wp_underCon, wp_units, wp_dozers,
							(int)wp_pl->getCanBuildUnits(), (int)wp_pl->getCanBuildBase());
						// Per-prototype production gates for teams that carry a
						// production condition (the Phase 4 attack teams).
						for (Player::PlayerTeamList::const_iterator wp_t = wp_pl->getPlayerTeams()->begin();
							 wp_t != wp_pl->getPlayerTeams()->end(); ++wp_t)
						{
							TeamPrototype* wp_proto = *wp_t;
							if (wp_proto->getTemplateInfo()->m_productionCondition.isEmpty())
								continue;
							fprintf(stderr, "[WPAI]   team=%s cond=%d inst=%d/%d pri=%d\n",
								wp_proto->getName().str(),
								(int)wp_proto->evaluateProductionCondition(),
								wp_proto->countTeamInstances(),
								wp_proto->getTemplateInfo()->m_maxInstances,
								wp_proto->getTemplateInfo()->m_productionPriority);
							// factory availability for the team's first unit type,
							// replicated from AIPlayer::findFactory (build-list scan)
							if (wp_proto->getTemplateInfo()->m_numUnitsInfo > 0)
							{
								const ThingTemplate* wp_ut = TheThingFactory->findTemplate(
									wp_proto->getTemplateInfo()->m_unitsInfo[0].unitThingName);
								Int wp_blEntries = 0, wp_blLinked = 0, wp_blPU = 0, wp_blCanMake = 0, wp_blIdle = 0;
								for (BuildListInfo* wp_bli = wp_pl->getBuildList(); wp_bli; wp_bli = wp_bli->getNext())
								{
									++wp_blEntries;
									Object* wp_fo = TheGameLogic->findObjectByID(wp_bli->getObjectID());
									if (!wp_fo) continue;
									++wp_blLinked;
									ProductionUpdateInterface* wp_pu = wp_fo->getProductionUpdateInterface();
									if (!wp_pu) continue;
									++wp_blPU;
									if (wp_ut && TheBuildAssistant->isPossibleToMakeUnit(wp_fo, wp_ut) == FALSE) continue;
									++wp_blCanMake;
									if (wp_pu->getProductionCount() == 0) ++wp_blIdle;
								}
								fprintf(stderr, "[WPAI]     unit=%s bl=%d linked=%d pu=%d canMake=%d idle=%d\n",
									wp_ut ? wp_ut->getName().str() : "?", wp_blEntries, wp_blLinked,
									wp_blPU, wp_blCanMake, wp_blIdle);
								// split the canMake failure: command-set scan vs player->canBuild
								if (wp_ut)
								{
									for (BuildListInfo* wp_bli2 = wp_pl->getBuildList(); wp_bli2; wp_bli2 = wp_bli2->getNext())
									{
										Object* wp_fo2 = TheGameLogic->findObjectByID(wp_bli2->getObjectID());
										if (!wp_fo2 || !wp_fo2->getProductionUpdateInterface()) continue;
										const CommandSet* wp_cs = TheControlBar->findCommandSet(wp_fo2->getCommandSetString());
										Int wp_btn = 0;
										if (wp_cs)
											for (Int wp_ci = 0; wp_ci < MAX_COMMANDS_PER_SET; ++wp_ci)
											{
												const CommandButton* wp_cb = wp_cs->getCommandButton(wp_ci);
												if (wp_cb && (wp_cb->getCommandType() == GUI_COMMAND_UNIT_BUILD ||
															  wp_cb->getCommandType() == GUI_COMMAND_DOZER_CONSTRUCT) &&
													wp_cb->getThingTemplate() && wp_cb->getThingTemplate()->isEquivalentTo(wp_ut))
													++wp_btn;
											}
										fprintf(stderr, "[WPAI]       fac=%s set='%s' cs=%d btn=%d canBuild=%d buildable=%d\n",
											wp_fo2->getTemplate()->getName().str(),
											wp_fo2->getCommandSetString().str(),
											wp_cs ? 1 : 0, wp_btn, (int)wp_pl->canBuild(wp_ut),
											(int)wp_ut->getBuildable());
									}
								}
							}
						}
						fflush(stderr);
					}
				}
			}

			TheGameClient->UPDATE();
			// GeneralsX @feature Codex 05/09/2026 Refresh War Powers HUD and
			// browser mission telemetry without changing simulation state.
			extern void WPUpdatePlayerExperience();
			WPUpdatePlayerExperience();
			// WarPowers @debug WP_AUTOTEST: scripted input smoke test. Injects
			// the same logic messages real mouse input produces: select the
			// local command center, queue a tank, select the tank, move it,
			// then attack the enemy command center. Env-gated; verifies the
			// full command chain headlessly.
			{
				static const char* wp_autoEnv = getenv("WP_AUTOTEST");
				static const Bool wp_auto = wp_autoEnv != nullptr;
				// WP_AUTOTEST=build stops after the tank spawns (stages 0-2),
				// leaving move/attack to a human at the mouse.
				static const Bool wp_buildOnly = wp_autoEnv && strcmp(wp_autoEnv, "build") == 0;
				// Explicit fixtures isolate actual supply transfers / power effects
				// from construction, balance and player-facing targeting tests.
				static const Bool wp_economyMode = wp_autoEnv && strcmp(wp_autoEnv, "economy") == 0;
				static const Bool wp_powerMode = wp_autoEnv && strcmp(wp_autoEnv, "powers") == 0;
				static const Bool wp_missionMode = wp_autoEnv && strcmp(wp_autoEnv, "mission") == 0;
				static const Bool wp_missionDefeatMode = wp_autoEnv && strncmp(wp_autoEnv, "mission-defeat", 14) == 0;
				// WP_AUTOTEST=base drives the dozer loop instead: CC -> Surveyor ->
				// construct Power Station -> construct Vehicle Works -> build a tank
				// from the factory. Verifies D016 construction end to end.
				static const Bool wp_baseMode = wp_autoEnv && strcmp(wp_autoEnv, "base") == 0;
				// WP_AUTOTEST=wedge: identical to base but the SECOND construct order is
				// injected while the first structure is still ~90% under construction —
				// the exact timing that wedged the dozer's primary state machine
				// (null current state; see the self-heal in DozerAIUpdate::update).
				// Passing = VehiclePlant still gets built afterwards.
				static const Bool wp_wedgeMode = wp_autoEnv && strcmp(wp_autoEnv, "wedge") == 0;
				// WP_AUTOTEST=strike: field 4 tanks and destroy the nearest enemy
				// STRUCTURE to the player base (the AI's forward tower) — a
				// deterministic building-kill to exercise kill-credit scoring and
				// the PLAYER_DESTROYED_N_BUILDINGS_PLAYER punish condition.
				static const Bool wp_strikeMode = wp_autoEnv && strcmp(wp_autoEnv, "strike") == 0;
				// WP_AUTOTEST=ghost verifies the fog-memory lifecycle: spawn a neutral
				// structure out of base vision, scout it with a tank, retreat (fog ->
				// snapshot), kill it while fogged (orphan ghost), re-scout (ghost must
				// free). Read the IG_TRACE [GHOST] breadcrumbs in the log.
				static const Bool wp_ghostMode = wp_autoEnv && strcmp(wp_autoEnv, "ghost") == 0;
				// WP_AUTOTEST=husk reproduces the user's translucent-remnant repro:
				// real dozer constructs a VehiclePlant site next to the enemy guard,
				// dozer is recalled, guard kills the 1HP site. Pair with
				// WP_SCENE_DUMP=<frame> to census the scene after the death.
				static const Bool wp_huskMode = wp_autoEnv && strcmp(wp_autoEnv, "husk") == 0;
				// WP_AUTOTEST=defeat kills the player CC to exercise the WP_Lose map
				// script -> DEFEAT screen (Menus/Defeat.wnd)
				static const Bool wp_defeatMode = wp_autoEnv && strcmp(wp_autoEnv, "defeat") == 0;
				// WP_AUTOTEST=win kills the ENEMY command structure to exercise the
				// WP_Win map script -> VICTORY banner -> score screen
				static const Bool wp_winMode = wp_autoEnv && strcmp(wp_autoEnv, "win") == 0;
				// WP_AUTOTEST=cycle: play match 1 briefly, quit to the shell,
				// redeploy the same map (the WPShell start path), and print
				// display-state diagnostics through match 2 — headless repro
				// for the second-match-black-screen bug. Pair with
				// WP_SCENE_DUMP=+N for a per-match scene census.
				static const Bool wp_cycleMode = wp_autoEnv && strcmp(wp_autoEnv, "cycle") == 0;
				static Int wp_cycleStage = 0;
				if (wp_cycleMode && TheGameLogic && !TheGameLogic->isInGame())
				{
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
				if (wp_auto && TheGameLogic && TheGameLogic->isInGame() && ThePlayerList)
				{
					const UnsignedInt wp_f = TheGameLogic->getFrame();
					static UnsignedInt wp_stage = 0;
					if (wp_cycleMode)
					{
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
					static ObjectID wp_ccId = INVALID_ID, wp_enemyCcId = INVALID_ID, wp_tankId = INVALID_ID;
					static ObjectID wp_fleet[4] = { INVALID_ID, INVALID_ID, INVALID_ID, INVALID_ID };
					static Coord3D wp_ccPos = {0,0,0};
					const Int wp_localIdx = ThePlayerList->getLocalPlayer() ? ThePlayerList->getLocalPlayer()->getPlayerIndex() : -1;
					// GeneralsX @feature Codex 05/09/2026 Unit/control labs use an
					// explicit production fixture now that real tanks require a
					// factory. The base/wedge labs still exercise dozer construction.
					auto wp_labTankName = [&]() -> const char* {
						Object* cc = TheGameLogic->findObjectByID(wp_ccId);
						return cc && cc->getTemplate()->getName() == "WPJ_CommandPost" ? "WPJ_Mongrel" : "WP_Tank";
					};
					auto wp_prepareUnitLab = [&]() {
						Player* owner = ThePlayerList->getLocalPlayer();
						if (!owner) return;
						const Bool jackal = strcmp(wp_labTankName(), "WPJ_Mongrel") == 0;
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
							Coord3D position = wp_ccPos;
							position.x += 130.0f + 90.0f * fi;
							position.y -= 100.0f;
							position.z = TheTerrainLogic->getGroundHeight(position.x, position.y);
							object->setPosition(&position);
							TheAI->pathfinder()->addObjectToPathfindMap(object);
							fprintf(stderr, "[WP_AUTO] UNIT_LAB_FIXTURE %s (construction tested by base/wedge)\n", fixtures[fi]);
						}
					};

					// GeneralsX @feature Codex 05/09/2026 Authored-map regression lab.
					// Fixtures bypass construction/combat/time, but never set objective
					// counters or dispatch victory/defeat. The shipped map scripts must
					// advance stages and deliver the normal match-result transition.
					if (wp_missionMode || wp_missionDefeatMode)
					{
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
							else if (wp_missionDefeatMode)
							{
								if (strcmp(wp_autoEnv, "mission-defeat-hq") == 0) destroyTarget("PlayerCC");
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
					else if (wp_economyMode || wp_powerMode)
					{
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
					else if (wp_defeatMode)
					{
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
					else if (wp_winMode)
					{
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
					else if (wp_huskMode)
					{
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
							// arm placement through the REAL UI flow (preview drawable and all),
							// exactly like clicking the build button does
							wp_hSelect(wp_hDozerId);
							Coord3D loc = { 678.0f, 533.0f, 0.0f };
							loc.z = TheTerrainLogic->getGroundHeight(loc.x, loc.y);
							TheTacticalView->lookAt(&loc);
							const ThingTemplate* tt = TheThingFactory->findTemplate("WP_VehiclePlant");
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
					else if (wp_ghostMode)
					{
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
					else if (wp_strikeMode)
					{
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
								wp_prepareUnitLab();
								GameMessage* s0 = TheMessageStream->appendMessage(GameMessage::MSG_CREATE_SELECTED_GROUP);
								s0->appendBooleanArgument(TRUE);
								s0->appendObjectIDArgument(wp_ccId);
								const ThingTemplate* tt = TheThingFactory->findTemplate(wp_labTankName());
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
								if (o->getTemplate()->getName() == wp_labTankName() && !o->isEffectivelyDead() &&
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
					else if (wp_baseMode || wp_wedgeMode)
					{
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
					else if (wp_stage == 0 && wp_f >= 90)
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
						wp_prepareUnitLab();
						// WP_AUTOTEST_UNIT overrides the fielded template (default WP_Tank)
						// so any new unit class gets a spawn+move+shoot lab for free.
						const char* wp_unitEnv = getenv("WP_AUTOTEST_UNIT");
						const ThingTemplate* tt = TheThingFactory->findTemplate(
							AsciiString(wp_unitEnv && wp_unitEnv[0] ? wp_unitEnv : wp_labTankName()));
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
							if (o->getTemplate()->getName() == (wp_unitEnv2 && wp_unitEnv2[0] ? wp_unitEnv2 : wp_labTankName()) &&
								o->getControllingPlayer() && o->getControllingPlayer()->getPlayerIndex() == wp_localIdx)
							{
								wp_fleet[found] = o->getID();
								if (++found >= 4) break;
							}
						}
						Int need = wp_buildOnly ? 1 : 4;
						const ThingTemplate* timedUnit = TheThingFactory->findTemplate(
							wp_unitEnv2 && wp_unitEnv2[0] ? wp_unitEnv2 : wp_labTankName());
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

					static UnsignedInt wp_lastStatus = 0;
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
			}
			TheMessageStream->propagateMessages();

			if (TheNetwork != nullptr)
			{
				TheNetwork->UPDATE();
			}
		}

		// TheSuperHackers @info Ignores frozen time because the script engine needs updating in the logic update regardless.
		if (canUpdateGameLogic(FramePacer::IgnoreFrozenTime))
		{
			TheGameLogic->UPDATE();

			if (!TheFramePacer->isTimeFrozen())
			{
				TheGameClient->step();
			}
		}
	}
}

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
extern HWND ApplicationHWnd;

/** -----------------------------------------------------------------------------------------------
 * The "main loop" of the game engine. It will not return until the game exits.
 */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Igroteka @build 06/07/2026 wasm: the browser owns the event loop. Each
// requestAnimationFrame tick runs one engine frame; a synchronous while-loop
// would freeze the tab. Mirrors the essentials of the native loop body below.
static void igrotekaFrameTick(void* arg)
{
	GameEngine* engine = static_cast<GameEngine*>(arg);
	// WarPowers: first tick = engine main loop is live; let the page drop
	// its loading overlay (main() runs the whole synchronous load first).
	static bool wp_notifiedRunning = false;
	if (!wp_notifiedRunning)
	{
		wp_notifiedRunning = true;
		EM_ASM({ if (Module.onEngineRunning) Module.onEngineRunning(); });
	}
	if (engine->getQuitting())
	{
		fprintf(stderr, "INFO: igrotekaFrameTick - quitting, cancelling main loop\n");
		emscripten_cancel_main_loop();
		// EXIT GAME never unwinds to a process exit here (EXIT_RUNTIME=0, RAF
		// loop just stops) — notify the page so it can return to the desktop.
		EM_ASM({ if (Module.onGameExit) Module.onGameExit(); });
		return;
	}
	try
	{
		// WarPowers @fix throttled-tab catch-up: hidden/occluded tabs clamp RAF
		// to ~1Hz, which used to crawl game time to a nearly-frozen trickle
		// while audio kept playing. Run up to 10 updates per tick so game time
		// tracks wall time (30 logic fps target).
		static double wp_lastTickMs = 0.0;
		double wp_nowMs = emscripten_get_now();
		int wp_steps = 1;
		if (wp_lastTickMs > 0.0)
		{
			double wp_dt = wp_nowMs - wp_lastTickMs;
			wp_steps = (int)(wp_dt / (1000.0 / 30.0));
			if (wp_steps < 1) wp_steps = 1;
			if (wp_steps > 10) wp_steps = 10;
		}
		wp_lastTickMs = wp_nowMs;
		for (int wp_i = 0; wp_i < wp_steps && !engine->getQuitting(); wp_i++)
			engine->update();
	}
	catch (INIException e)
	{
		fprintf(stderr, "FATAL: INIException in GameEngine::update: %s\n",
		        e.mFailureMessage ? e.mFailureMessage : "(no message)");
		engine->setQuitting(TRUE);
	}
	catch (...)
	{
		fprintf(stderr, "FATAL: uncaught exception in GameEngine::update\n");
		engine->setQuitting(TRUE);
	}
	if (TheFramePacer)
		TheFramePacer->update();
}
#endif

void GameEngine::execute()
{
#ifdef __EMSCRIPTEN__
	// Hands control to the browser; ticks run via requestAnimationFrame.
	// simulate_infinite_loop=1 unwinds out of main() without returning.
	emscripten_set_main_loop_arg(igrotekaFrameTick, this, 0, 1);
	return;
#endif

#if defined(RTS_DEBUG)
	DWORD startTime = timeGetTime() / 1000;
#endif

	// pretty basic for now
	while( !m_quitting )
	{

		//if (TheGlobalData->m_vTune)
		{
#ifdef PERF_TIMERS
			PerfGather::resetAll();
#endif
		}

		{

#if defined(RTS_DEBUG)
			{
				// enter only if in benchmark mode
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					DWORD currentTime = timeGetTime() / 1000;
					if (TheGlobalData->m_benchmarkTimer < currentTime - startTime)
					{
						if (TheGameLogic->isInGame())
						{
							if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
							{
								TheRecorder->stopRecording();
							}
							TheGameLogic->clearGameData();
						}
						TheGameEngine->setQuitting(TRUE);
					}
				}
			}
#endif

			{
				try
				{
					// compute a frame
					update();
				}
				catch (INIException e)
				{
					// Release CRASH doesn't return, so don't worry about executing additional code.
					if (e.mFailureMessage)
						RELEASE_CRASH((e.mFailureMessage));
					else
						RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
				catch (...)
				{
					// try to save info off
					try
					{
						if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD && TheRecorder->isMultiplayer())
							TheRecorder->cleanUpReplayFile();
					}
					catch (...)
					{
					}
					RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
			}

			// TheFramePacer->update() sleeps here to hold the render FPS cap (RenderFpsPreset /
			// GlobalData::m_framesPerSecondLimit). Because input is serviced once per iteration of
			// this loop (SDL poll + GameClient::UPDATE message processing, above), this pacing wait
			// also throttles the input sampling rate. A low FPS cap therefore makes the mouse feel
			// laggy (hard to click moving units) even though the fixed 30 Hz simulation is unchanged.
			// See RenderFpsPreset::s_fpsValues in FrameRateLimit.cpp for the full explanation.
			TheFramePacer->update();

			// NOTE: TheDisplay->draw() is called via TheGameClient->UPDATE() above.
			// GameClient::update() dispatches TheDisplay->DRAW() each frame.
			// Do NOT add an extra draw() call here - it would double-present per frame.
		}

#ifdef PERF_TIMERS
		if (!m_quitting && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isGamePaused())
		{
			PerfGather::dumpAll(TheGameLogic->getFrame());
			PerfGather::displayGraph(TheGameLogic->getFrame());
			PerfGather::resetAll();
		}
#endif

	}
}

/** -----------------------------------------------------------------------------------------------
	* Factory for the message stream
	*/
MessageStream *GameEngine::createMessageStream()
{
	// if you change this update the tools that use the engine systems
	// like GUIEdit, it creates a message stream to run in "test" mode
	return MSGNEW("GameEngineSubsystem") MessageStream;
}

//-------------------------------------------------------------------------------------------------
FileSystem *GameEngine::createFileSystem()
{
	return MSGNEW("GameEngineSubsystem") FileSystem;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isMultiplayerSession()
{
	return TheRecorder->isMultiplayer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#define CONVERT_EXEC1	"..\\Build\\nvdxt -list buildDDS.txt -dxt5 -full -outdir Art\\Textures > buildDDS.out"

void updateTGAtoDDS()
{
	// Here's the scoop. We're going to traverse through all of the files in the Art\Textures folder
	// and determine if there are any .tga files that are newer than associated .dds files. If there
	// are, then we will re-run the compression tool on them.

	File *fp = TheLocalFileSystem->openFile("buildDDS.txt", File::WRITE | File::CREATE | File::TRUNCATE | File::TEXT);
	if (!fp) {
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory("Art\\Textures\\", "", "*.tga", files, TRUE);
	FilenameList::iterator it;
	for (it = files.begin(); it != files.end(); ++it) {
		AsciiString filenameTGA = *it;
		AsciiString filenameDDS = *it;
		FileInfo infoTGA;
		TheLocalFileSystem->getFileInfo(filenameTGA, &infoTGA);

		// skip the water textures, since they need to be NOT compressed
		filenameTGA.toLower();
		if (strstr(filenameTGA.str(), "caust"))
		{
			continue;
		}
		// and the recolored stuff.
		if (strstr(filenameTGA.str(), "zhca"))
		{
			continue;
		}

		// replace tga with dds
		filenameDDS.truncateBy(3); // tga
		filenameDDS.concat("dds");

		Bool needsToBeUpdated = FALSE;
		FileInfo infoDDS;
		if (TheFileSystem->doesFileExist(filenameDDS.str())) {
			TheFileSystem->getFileInfo(filenameDDS, &infoDDS);
			if (infoTGA.timestampHigh > infoDDS.timestampHigh ||
					(infoTGA.timestampHigh == infoDDS.timestampHigh &&
					 infoTGA.timestampLow > infoDDS.timestampLow)) {
				needsToBeUpdated = TRUE;
			}
		} else {
			needsToBeUpdated = TRUE;
		}

		if (!needsToBeUpdated) {
			continue;
		}

		filenameTGA.concat("\n");
		fp->write(filenameTGA.str(), filenameTGA.getLength());
	}

	fp->close();

// TheSuperHackers @build fighter19 11/02/2026 Windows-only texture conversion
#ifdef _WIN32
	system(CONVERT_EXEC1);
#else
	// Linux: TGA to DDS conversion not needed (or handle differently)
#endif
}

//-------------------------------------------------------------------------------------------------
// System things

// If we're using the Wide character version of MessageBox, then there's no additional
// processing necessary. Please note that this is a sleazy way to get this information,
// but pending a better one, this'll have to do.
// TheSuperHackers @build fighter19 11/02/2026 MessageBox detection (Windows-only)
#ifdef _WIN32
extern const Bool TheSystemIsUnicode = (((void*) (::MessageBox)) == ((void*) (::MessageBoxW)));
#else
extern const Bool TheSystemIsUnicode = true;  // Linux: Always Unicode (UTF-8)
#endif
