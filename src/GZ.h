// Copyright (C) 2026 Spamoidutout - SPDX-License-Identifier: LGPL-2.1-or-later
// GZ.h - the game interfaces this mod uses, without the gzcom-dll SDK headers (they pull
// in the STL). Slot numbers are checked against gzcom-dll (github.com/nsgomez/gzcom-dll)
// and the OpenSC4 specs of the 1.1.641 executable.
//
// - Interfaces the mod IMPLEMENTS are declared as classes, in the exact gzcom-dll order.
// - Interfaces the mod only CALLS go through VCall(object, slot, args...): a handful of
//   slots, each one written down below, instead of copying long class declarations.
#pragma once
#include "Win32.h"

// ---------------------------------------------------------------------------------------
// Implemented interfaces (exact vtable order).
// ---------------------------------------------------------------------------------------
class cIGZString;
class cIGZCOM;
class cIGZFrameWork;
class cIGZMessage2;

class cIGZUnknown
{
public:
	virtual bool QueryInterface(uint32_t riid, void** ppvObj) = 0;   // 0
	virtual uint32_t AddRef() = 0;                                   // 1
	virtual uint32_t Release() = 0;                                  // 2
};

class cIGZCOMDirector : public cIGZUnknown
{
public:
	typedef void (*ClassObjectEnumerationCallback)(uint32_t clsid, uint32_t reserved, void* pContext);
	virtual bool InitializeCOM(cIGZCOM* pCOM, const cIGZString& sLibraryPath) = 0;     // 3
	virtual bool OnStart(cIGZCOM* pCOM) = 0;                                           // 4
	virtual void EnumClassObjects(ClassObjectEnumerationCallback cb, void* ctx) = 0;   // 5
	virtual bool GetClassObject(uint32_t rclsid, uint32_t riid, void** ppvObj) = 0;    // 6
	virtual bool CanUnloadNow() = 0;                                                   // 7
	virtual bool OnUnload() = 0;                                                       // 8
	virtual uint32_t RefCount() = 0;                                                   // 9
	virtual uint32_t RemoveRef() = 0;                                                  // 10
	virtual cIGZFrameWork* FrameWork() = 0;                                            // 11
	virtual cIGZCOM* GZCOM() = 0;                                                      // 12
	virtual uint32_t GetDirectorID() const = 0;                                        // 13
	virtual bool GetLibraryPath(cIGZString& sLibraryPath) = 0;                         // 14
	virtual uint32_t GetHeapAllocatedSize() = 0;                                       // 15
};

class cIGZFrameWorkHooks : public cIGZUnknown
{
public:
	virtual bool PreFrameWorkInit() = 0;           // 3
	virtual bool PreAppInit() = 0;                 // 4
	virtual bool PostAppInit() = 0;                // 5
	virtual bool PreAppShutdown() = 0;             // 6
	virtual bool PostAppShutdown() = 0;            // 7
	virtual bool PostSystemServiceShutdown() = 0;  // 8
	virtual bool AbortiveQuit() = 0;               // 9
	virtual bool OnInstall() = 0;                  // 10
};

class cIGZMessageTarget2 : public cIGZUnknown
{
public:
	virtual bool DoMessage(cIGZMessage2* pMessage) = 0;   // 3
};

// Interface IDs.
constexpr uint32_t GZIID_cIGZUnknown        = 0x00000001;
constexpr uint32_t GZIID_cIGZCOMDirector    = 0xA21EE941;
constexpr uint32_t GZIID_cIGZFrameWorkHooks = 0x03FA40BF;
constexpr uint32_t GZIID_cIGZMessageTarget2 = 0x452294AA;

// ---------------------------------------------------------------------------------------
// Called interfaces: virtual call by slot number (all __thiscall, callee pops).
// ---------------------------------------------------------------------------------------
template <typename R, typename... A>
inline R VCall(void* obj, int slot, A... args)
{
	typedef R(__thiscall * Fn)(void*, A...);
	return (*reinterpret_cast<Fn**>(obj))[slot](obj, args...);
}

inline void GZRelease(void* obj) { if (obj) VCall<uint32_t>(obj, 2); }

namespace Slot
{
	// cIGZUnknown
	constexpr int QueryInterface = 0, Release = 2;

	// cIGZCOM
	constexpr int COM_FrameWork = 4;

	// cIGZFrameWork
	constexpr int FW_GetSystemService = 5, FW_AddHook = 7, FW_RemoveHook = 8;

	// cIGZMessage2
	constexpr int Msg_GetType = 4;

	// cIGZMouseMessage (cIGZMessage2 + Initialize 8): 9 = event, 10 = position (hidden
	// return pointer). Same slots as the SC4 RightClick kit and AdvisorControl (checked in
	// game). The game never sends the middle button (no event 9 or 12, despite the SDK).
	constexpr int Mouse_EventType = 9, Mouse_Position = 10;

	// cIGZMessageServer2
	constexpr int MS2_AddNotification = 5, MS2_RemoveNotification = 6;

	// cIGZCommandServer
	constexpr int Cmd_CreateCommandParameterSet = 5;

	// cIGZWinMgr
	constexpr int WM_GetModalNestCount = 43;

	// cISC4App
	constexpr int App_GetMainWindow = 26;

	// cIGZWin (same slots as the SC4 RightClick kit, checked in game)
	constexpr int Win_GetParentWin = 11, Win_EnumChildren = 32, Win_GetChildWindowFromID = 34,
	              Win_GetChildAs = 36, Win_GetW = 41, Win_GetH = 42, Win_GetL = 43, Win_GetT = 44,
	              Win_GetID = 63, Win_IsVisible = 71;

	// cISC4View3DWin
	constexpr int V3D_AsIGZWin = 3, V3D_ProcessCommand = 7, V3D_GetCurrentViewInputControl = 24;

	// cISC4ViewInputControl
	constexpr int VIC_GetID = 5;
}

// System services: framework->GetSystemService(service ID, interface ID, &out), AddRef'd.
namespace Service
{
	constexpr uint32_t kMessageServer2 = 0x04FA845B, kIID_MessageServer2 = 0x652294C7;
	constexpr uint32_t kCommandServer  = 0xEB903A32, kIID_CommandServer  = 0xCB90253A;
	constexpr uint32_t kWinMgr         = 0xA417445E, kIID_WinMgr         = 0x000005A4;
	constexpr uint32_t kSC4App         = 0x00000066, kIID_SC4App         = 0x26CE01C0;
}

struct GZPoint { int32_t x, y; };

constexpr uint32_t GZIID_cIGZWin = 0x22BA0121;
// cIGZWin::EnumChildren callback (cdecl).
typedef bool (*EnumChildrenCallback)(void* parent, uint32_t childID, void* child, void* context);
