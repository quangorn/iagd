#include "stdafx.h"
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include "MessageType.h"
#include <detours.h>
#include "InventorySack_AddItem.h"
#include "Exports.h"

#define STASH_1 0
#define STASH_2 1
#define STASH_3 2
#define STASH_4 3
#define STASH_5 4
#define STASH_6 5
#define STASH_PRIVATE 1000

HANDLE InventorySack_AddItem::m_hEvent;
DataQueue* InventorySack_AddItem::m_dataQueue;

InventorySack_AddItem::GameInfo_GameInfo_Param InventorySack_AddItem::dll_GameInfo_GameInfo_Param;
InventorySack_AddItem::GameInfo_GetHardcore InventorySack_AddItem::dll_GameInfo_GetHardcore;
GetPrivateStash InventorySack_AddItem::privateStashHook;
InventorySack_AddItem::InventorySack_AddItem_Drop InventorySack_AddItem::dll_InventorySack_AddItem_Drop;
InventorySack_AddItem::InventorySack_AddItem_Vec2 InventorySack_AddItem::dll_InventorySack_AddItem_Vec2;

void InventorySack_AddItem::EnableHook() {
	// GameInfo::
	dll_GameInfo_GameInfo_Param = (GameInfo_GameInfo_Param)GetProcAddress(::GetModuleHandle(L"Engine.dll"), GAMEINFO_CONSTRUCTOR_ARGS);
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach((PVOID*)&dll_GameInfo_GameInfo_Param, Hooked_GameInfo_GameInfo_Param);
	DetourTransactionCommit();

	dll_InventorySack_AddItem_Drop = (InventorySack_AddItem_Drop)GetProcAddress(::GetModuleHandle(L"Game.dll"), "?AddItem@InventorySack@GAME@@QEAA_NPEAVItem@2@_N1@Z");
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach((PVOID*)&dll_InventorySack_AddItem_Drop, Hooked_InventorySack_AddItem_Drop);
	DetourTransactionCommit();


	dll_InventorySack_AddItem_Vec2 = (InventorySack_AddItem_Vec2)GetProcAddress(::GetModuleHandle(L"Game.dll"), "?AddItem@InventorySack@GAME@@QEAA_NAEBVVec2@2@PEAVItem@2@_N@Z");
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach((PVOID*)&dll_InventorySack_AddItem_Vec2, Hooked_InventorySack_AddItem_Vec2);
	DetourTransactionCommit();

	
	privateStashHook.EnableHook();

	// bool GAME::GameInfo::GetHardcore(void)
	dll_GameInfo_GetHardcore = (GameInfo_GetHardcore)GetProcAddress(::GetModuleHandle(L"Engine.dll"), GET_HARDCORE);


}

InventorySack_AddItem::InventorySack_AddItem(DataQueue* dataQueue, HANDLE hEvent) {
	InventorySack_AddItem::m_dataQueue = dataQueue;
	InventorySack_AddItem::m_hEvent = hEvent;
	privateStashHook = GetPrivateStash(dataQueue, hEvent);
}

InventorySack_AddItem::InventorySack_AddItem() {
	InventorySack_AddItem::m_hEvent = NULL;
}

void InventorySack_AddItem::DisableHook() {
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());


	DetourDetach((PVOID*)&dll_GameInfo_GameInfo_Param, Hooked_GameInfo_GameInfo_Param);
	
	DetourTransactionCommit();

	privateStashHook.DisableHook();
}



// Since were creating from an existing object we'll need to call Get() on isHardcore and ModLabel
void* __fastcall InventorySack_AddItem::Hooked_GameInfo_GameInfo_Param(void* This , void* info) {
	void* result = dll_GameInfo_GameInfo_Param(This, info);

	bool isHardcore = dll_GameInfo_GetHardcore(This);
	DataItemPtr dataEvent(new DataItem(TYPE_GameInfo_IsHardcore_via_init, sizeof(isHardcore), (char*)&isHardcore));
	m_dataQueue->push(dataEvent);
	SetEvent(m_hEvent);

	return result;
}

void DoLog(const wchar_t* message);
void DoLog(std::wstring message);


/// <summary>
/// Called primarily when dropping an item into a closed stash (eg drop item from tab 3 to tab 4)
/// </summary>
/// <param name="This"></param>
/// <param name="item"></param>
/// <param name="findPosition"></param>
/// <param name="SkipPlaySound"></param>
/// <returns></returns>
void* __fastcall InventorySack_AddItem::Hooked_InventorySack_AddItem_Drop(void* This, GAME::Item *item, bool findPosition, bool SkipPlaySound) {
	if (HandleItem(This, item)) {
		return (void*)1;
	}

	void* v = dll_InventorySack_AddItem_Drop(This, item, findPosition, SkipPlaySound);
	return v;
}

/// <summary>
/// Regular "add to tab/stash" move. Ctrl+click and regular item drop
/// </summary>
/// <param name="This"></param>
/// <param name="position"></param>
/// <param name="item"></param>
/// <param name="SkipPlaySound"></param>
/// <returns></returns>
void* __fastcall InventorySack_AddItem::Hooked_InventorySack_AddItem_Vec2(void* This, void* position, GAME::Item* item, bool SkipPlaySound) {
	if (HandleItem(This, item)) {
		return (void*)1;
	}

	return dll_InventorySack_AddItem_Vec2(This, position, item, SkipPlaySound);
}

/// <summary>
/// Checks if the provided replica info contains any of the exclusion parameters
/// We are not interested in potions, quest items, components or soulbound items.
/// </summary>
/// <param name="item"></param>
/// <returns></returns>
bool IsRelevant(GAME::ItemReplicaInfo& item) {
	if (item.stackSize > 1) {
		return false;
	}

	if (item.baseRecord.find("/storyelements/") != std::string::npos) {
		return false;
	}

	if (item.baseRecord.find("/materia/") != std::string::npos) {
		return false;
	}

	if (item.baseRecord.find("/questitems/") != std::string::npos) {
		return false;
	}

	if (item.baseRecord.find("/crafting/") != std::string::npos) {
		return false;
	}

	// Transmute
	if (!item.enchantmentRecord.empty()) {
		return false;
	}

	// The item has a materia, it's probably fine to loot, but lets err on the safe side and assume its not.
	if (!item.materiaRecord.empty()) {
		return false;
	}

	return true;
}

bool InventorySack_AddItem::HandleItem(void* stash, GAME::Item* item) {
	GAME::GameEngine* gameEngine = fnGetgGameEngine();
	GAME::ItemReplicaInfo replica;
	fnItemGetItemReplicaInfo(item, replica);
	if (!IsRelevant(replica)) {
		// DoLog(L"Not looting: Item is not relevant");
		return false;
	}

	void* realPtr = fnGetPlayerTransfer(gameEngine);
	if (realPtr == nullptr) {
		// DoLog(L"Not looting: Unable to locate transfer stash");
		return false;
	}

	std::vector<GAME::InventorySack*>* sacks = (std::vector<GAME::InventorySack*>*)realPtr;
	//DoLog(L"There are: " + std::to_wstring(sacks->size()) + L"Inventory sacks");

	if (sacks->size() == 0) {
		// DoLog(L"Not looting: No transfer stash tabs");
		return false;
	}

	auto lastSackPtr = sacks->at(sacks->size() - 1);
	if ((void*)lastSackPtr != stash) {
		// DoLog(L"Not looting: Item is not in transfer stash");
		return false;
	}

	std::wstringstream stream;
	stream << "Item: ";

	GAME::GameInfo* gameInfo = fnGetGameInfo(gameEngine);
	if (gameInfo != NULL) {
		stream << "ModName: " << fnGetModName(gameInfo) << "\n";
		stream << "Hardcore: " << fnGetHardcore(gameInfo) << "\n";
		stream << "GameInfo is not null\n";
	}
	else {
		stream << "GameInfo is null!!!\n";
	}

	stream << "\n";


	stream << GAME::itemReplicaToString(replica) << "\n";

	DoLog(stream.str().c_str());

	return false; // Not handled
}