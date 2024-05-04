#include "stdafx.h"
#include <chrono>
#include <codecvt>
#include <windows.h>
#include <stdlib.h>
#include "DataQueue.h"
#include "MessageType.h"
#include "StateRequestNpcAction.h"
#include "StateRequestMoveAction.h"
#include "CloudGetNumFiles.h"
#include "CloudRead.h"
#include "CloudWrite.h"
#include "InventorySack_AddItem.h"
#include "NpcDetectionHook.h"
#include "SaveTransferStash.h"
#include "Exports.h"
#include "CanUseDismantle.h"
#include "EquipmentSeedInfo.h"
#include "OnDemandSeedInfo.h"
#include "GameEngineUpdate.h"
#include "ItemRelicSeedInfo.h"
#include "HookLog.h"
#include "SetTransferOpen.h"
#include "SetHardcore.h"
HookLog g_log;

#pragma region Variables
// Switches hook logging on/off
#if 1
#define LOG(streamdef) \
{ \
    std::wstring msg = (((std::wostringstream&)(std::wostringstream().flush() << streamdef)).str()); \
	g_log.out(logStartupTime() + msg); \
    msg += _T("\n"); \
    OutputDebugString(msg.c_str()); \
}
#else
#define LOG(streamdef) \
    __noop;
#endif




DWORD g_lastThreadTick = 0;
HANDLE g_hEvent;
HANDLE g_thread;

DataQueue g_dataQueue;
InventorySack_AddItem* g_InventorySack_AddItemInstance = NULL;

HWND g_targetWnd = NULL;

#pragma endregion

#pragma region CORE


std::wstring logStartupTime() {
	__time64_t rawtime;
	struct tm timeinfo;
	wchar_t buffer[80];

	_time64(&rawtime);
	localtime_s(&timeinfo, &rawtime);

	wcsftime(buffer, sizeof(buffer), L"%Y-%m-%d %H:%M:%S ", &timeinfo);
	std::wstring str(buffer);

	return str;
}

std::string logStartupTimeChar() {
	__time64_t rawtime;
	struct tm timeinfo;
	char buffer[80];

	_time64(&rawtime);
	localtime_s(&timeinfo, &rawtime);

	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", &timeinfo);
	std::string str(buffer);

	return str;
}


void LogToFile(const wchar_t* message) {
	g_log.out(logStartupTime() + message);
}
void LogToFile(const char* message) {
	g_log.out((logStartupTimeChar() + std::string(message)).c_str());
}
void LogToFile(const std::string message) {
	g_log.out((logStartupTimeChar() + message).c_str());
}
void LogToFile(std::wstring message) {
	g_log.out(logStartupTime() + message);
}
void LogToFile(std::wstringstream message) {
	g_log.out(logStartupTime() + message.str());
}


/// Thread function that dispatches queued message blocks to the IA application.
void WorkerThreadMethod() {
    while ((g_hEvent != NULL) && (WaitForSingleObject(g_hEvent,INFINITE) == WAIT_OBJECT_0)) {
        if (g_hEvent == NULL) {
            break;
        }

        DWORD tick = GetTickCount();
        if (tick < g_lastThreadTick) {
            // Overflow
            g_lastThreadTick = tick;
        }

        if ((tick - g_lastThreadTick > 1000) || (g_targetWnd == NULL)) {
            // We either don't have a valid window target OR it has been more than 1 sec since we last update the target.
            g_targetWnd = FindWindow( L"GDIAWindowClass", NULL);
            g_lastThreadTick = GetTickCount();
            LOG(L"FindWindow returned: " << g_targetWnd);

			if (g_InventorySack_AddItemInstance != NULL) {
				g_InventorySack_AddItemInstance->SetActive(g_targetWnd != NULL);
			}
        }

        while (!g_dataQueue.empty()) {
            DataItemPtr item = g_dataQueue.pop();

            if (g_targetWnd == NULL) {
                // We have data, but no target window, so just delete the message
                continue;
            }

            COPYDATASTRUCT data;
            data.dwData = item->type();
            data.lpData = item->data();
            data.cbData = item->size();

            // To avoid blocking the main thread, we should not have a lock on the queue while we process the message.
			SendMessage( g_targetWnd, WM_COPYDATA, 0, ( LPARAM ) &data );
			auto lastErrorCode = GetLastError();
			if (lastErrorCode != 0)
				LOG(L"After SendMessage error code is " << lastErrorCode);
        }

    }
}

OnDemandSeedInfo* listener = nullptr;
unsigned __stdcall WorkerThreadMethodWrap(void* argss) {


	LogToFile(L"Starting seed info thread..");
	
	if (listener != nullptr) {
		listener->Start();
	}
	WorkerThreadMethod();
	return 0;
}

void StartWorkerThread() {
	LOG(L"Starting worker thread..");
	unsigned int pid;
	g_thread = (HANDLE)_beginthreadex(NULL, 0, &WorkerThreadMethodWrap, NULL, 0, &pid);
	

	DataItemPtr item(new DataItem(TYPE_REPORT_WORKER_THREAD_LAUNCHED, 0, NULL));
	g_dataQueue.push(item);
	SetEvent(g_hEvent);
	LOG(L"Started worker thread..");
}


void EndWorkerThread() {
	LOG(L"Ending worker thread..");
	if (g_hEvent != NULL) {
		SetEvent(g_hEvent);
		HANDLE h = g_hEvent;
		g_hEvent = NULL;
		CloseHandle(h);

		//WaitForSingleObject(g_thread, INFINITE);
		CloseHandle(g_thread);
	}
}

#pragma endregion

static void ConfigurePlayerPositionHooks(std::vector<BaseMethodHook*>& hooks) {
	LogToFile(L"Configuring player position hooks..");
	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_MOVETO));
	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_IDLE));
	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_LONG_IDLE));
	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_MOVE_AND_SKILL));
	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_MOVE_ACTOR));

	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_MOVE_TO_SKILL));
	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_PICKUP));
	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_TALK_TO_NPC));
	hooks.push_back(new StateRequestMoveAction(&g_dataQueue, g_hEvent, REQUEST_MOVE_ACTION_SKILL));

	// For these, the target position could actually be the smuggler.
	LogToFile(L"Configuring player position hooks for move-to-npc..");
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_IDLE));
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_MOVETO));
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_JUMP_TO_SKILL));
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_LONG_IDLE));
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_MOVE_AND_SKILL));
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_MOVE_TO_ACTOR));	
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_MOVE_TO_SKILL));
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_PICKUP_ITEM));
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_TALK_TO_NPC));
	hooks.push_back(new StateRequestNpcAction(&g_dataQueue, g_hEvent, REQUEST_NPC_ACTION_MOVE_TO_NPC));	
}

// Cloud detection (is cloud enabled?) hooks
static void ConfigureCloudDetectionHooks(std::vector<BaseMethodHook*>& hooks) {
	LogToFile(L"Configuring cloud detection hooks..");
	hooks.push_back(new CloudGetNumFiles(&g_dataQueue, g_hEvent));
	hooks.push_back(new CloudRead(&g_dataQueue, g_hEvent));
	hooks.push_back(new CloudWrite(&g_dataQueue, g_hEvent));
}

static void ConfigureStashDetectionHooks(std::vector<BaseMethodHook*>& hooks) {
	// Stash detection hooks
	LogToFile(L"Configuring stash detection hooks..");
	hooks.push_back(new NpcDetectionHook(&g_dataQueue, g_hEvent));
	hooks.push_back(new CanUseDismantle(&g_dataQueue, g_hEvent));	
	hooks.push_back(new SaveTransferStash(&g_dataQueue, g_hEvent));
	hooks.push_back(new SetTransferOpen(&g_dataQueue, g_hEvent));


	try {
		LogToFile(L"Configuring instaloot hook..");
		g_InventorySack_AddItemInstance = new InventorySack_AddItem(&g_dataQueue, g_hEvent);
		hooks.push_back(g_InventorySack_AddItemInstance); // Includes GetPrivateStash internally
	} catch (std::exception& ex) {
		// For now just let it be. Known issue inside InventorySack_AddItem

		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		std::wstring wide = converter.from_bytes(ex.what());

		LogToFile(L"ERROR Configuring instaloot hook.." + wide);
		
	} catch (...) {
		// For now just let it be. Known issue inside InventorySack_AddItem
		LogToFile(L"ERROR Configuring instaloot hook.. (triple-dot)");
	}

	LogToFile(L"Configuring hc detection hook..");
	hooks.push_back(new SetHardcore(&g_dataQueue, g_hEvent));
}
/*
bool GetProductAndVersion()
{
	// get the filename of the executable containing the version resource
	TCHAR szFilename[MAX_PATH + 1] = { 0 };
	if (GetModuleFileName(NULL, szFilename, MAX_PATH) == 0)
	{
		LogToFile("GetModuleFileName failed with error");
		return false;
	}

	// allocate a block of memory for the version info
	DWORD dummy;
	DWORD dwSize = GetFileVersionInfoSize(szFilename, &dummy);
	if (dwSize == 0)
	{
		LogToFile(L"GetFileVersionInfoSize failed with error");
		return false;
	}
	std::vector<BYTE> data(dwSize);

	// load the version info
	if (!GetFileVersionInfo(szFilename, NULL, dwSize, &data[0]))
	{
		LogToFile("GetFileVersionInfo failed with error");
		return false;
	}

	// get the name and version strings
	LPVOID pvProductName = NULL;
	unsigned int iProductNameLen = 0;
	LPVOID pvProductVersion = NULL;
	unsigned int iProductVersionLen = 0;

	// replace "040904e4" with the language ID of your resources
	if (!VerQueryValue(&data[0], _T("\\StringFileInfo\\040904e4\\ProductName"), &pvProductName, &iProductNameLen) ||
		!VerQueryValue(&data[0], _T("\\StringFileInfo\\040904e4\\ProductVersion"), &pvProductVersion, &iProductVersionLen))
	{
		LogToFile("Can't obtain ProductName and ProductVersion from resources");
		return false;
	}

	LogToFile((wchar_t*)pvProductVersion);

	//strProductName.SetString((LPCSTR)pvProductName, iProductNameLen);
	//strProductVersion.SetString((LPCSTR)pvProductVersion, iProductVersionLen);

	return true;
}
*/

std::vector<BaseMethodHook*> hooks;
int ProcessAttach(HINSTANCE _hModule) {
	//GetProductAndVersion();
	LogToFile(std::string("DLL Compiled: ") + std::string(__DATE__) + std::string(" ") + std::string(__TIME__));
	LogToFile(L"Attatching to process..");


	GAME::GameEngine* gameEngine = fnGetGameEngine();
	if (gameEngine == nullptr) {
		LogToFile(L"Could not find game engine ptr, aborting DLL injection..");
		return FALSE;
	}
	if (IsGameLoading(gameEngine)) {
		LogToFile(L"Game is still loading, aborting DLL injection..");
		return FALSE;
	}
	if (IsGameWaiting(gameEngine, true)) { // TODO: When on a PC with IDA installed, figure out what the boolean is.
		LogToFile(L"Game is waiting, aborting DLL injection..");
		return FALSE;
	}
	if (!IsGameEngineOnline(gameEngine)) {
		LogToFile(L"Game engine is not yet online, aborting DLL injection..");
		return FALSE;
	}


	g_hEvent = CreateEvent(NULL,FALSE,FALSE, L"IA_Worker");

#ifdef PLAYTEST
	LogToFile(L"DLL for GD 1.2");
#else
	LogToFile(L"DLL for GD 1.1.x");
#endif


	LogToFile(L"Creating seed info container class..");
	listener = new OnDemandSeedInfo(&g_dataQueue, g_hEvent);

	LogToFile(L"Preparing hooks..");
	// Player position hooks
	ConfigurePlayerPositionHooks(hooks);
	ConfigureCloudDetectionHooks(hooks);
	ConfigureStashDetectionHooks(hooks);

	LogToFile(L"Preparing replica hooks..");
	hooks.push_back(new EquipmentSeedInfo(&g_dataQueue, g_hEvent));

	if (listener != nullptr) {
		hooks.push_back(listener);
	}
	hooks.push_back(new ItemRelicSeedInfo(&g_dataQueue, g_hEvent));
	// hooks.push_back(new GameEngineUpdate(&g_dataQueue, g_hEvent));	 // Debug/test only
	
	LogToFile(L"Starting hook enabling.. " + std::to_wstring(hooks.size()) + L" hooks.");
	for (unsigned int i = 0; i < hooks.size(); i++) {
		LOG(L"Enabling hook..");
		hooks[i]->EnableHook();
	}
	LogToFile(L"Hooking complete..");

	
    StartWorkerThread();
	LogToFile(L"Initialization complete..");

	g_log.setInitialized(true);
    return TRUE;
}


#pragma region Attach_Detatch
int ProcessDetach( HINSTANCE _hModule ) {
	// Signal that we are shutting down
	// This message is not at all guaranteed to get sent.

	LOG(L"Detatching DLL..");
	OutputDebugString(L"ProcessDetach");


	for (unsigned int i = 0; i < hooks.size(); i++) {
		hooks[i]->DisableHook();
		delete hooks[i];
	}
	hooks.clear();

	if (listener != nullptr) {
		listener->Stop();
		delete listener;
		listener = nullptr;
	}

    EndWorkerThread();

	LOG(L"DLL detatched..");
    return TRUE;
}

void Dump_ItemStats();
BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
        return ProcessAttach( hModule );

	case DLL_PROCESS_DETACH:
        return ProcessDetach( hModule );
	}
    return TRUE;
}
#pragma endregion


