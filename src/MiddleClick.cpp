// Copyright (C) 2026 Spamoidutout - SPDX-License-Identifier: LGPL-2.1-or-later
// SC4 Middle Click Thingy - the middle mouse button cycles through the most used tools:
//
//   another tool is open      -> Escape (the game's "Cancel" command)
//   no tool is open           -> query tool
//   the query tool is open    -> bulldozer, if this click quickly follows the middle click
//                                that opened it (within 3 s, no left or right click between);
//                                Escape otherwise
//   the bulldozer is open     -> Escape, and the cycle starts again
//
// The next step comes from the tool that is really open, so the cycle stays right when the
// keyboard or the interface is used in between.
//
// Only a short middle click ON THE CITY counts: a click on an interface window (a game panel,
// the bar of SC4 Advisor Control, which uses the middle click on its portraits...), a drag, a
// long press or a click while a dialog is open are left alone.
//
// How: the game never passes the middle button on (no event 9 / 12 in its mouse message), so
// the mod catches the Windows messages of the button (WM_MBUTTONDOWN / UP) with a mouse hook
// on the game's thread. The hook runs when the game's message loop takes the message out of
// the queue, the same place where the game handles its keyboard shortcuts, so the tool changes
// at once, through the same commands as those shortcuts (cISC4View3DWin::ProcessCommand).
// The position is the last one the game's mouse message (0xFA0C6390) gave.
#include "GZ.h"
#include "Runtime.h"

namespace
{
	constexpr uint32_t kDirectorID = 0x7C3E91A5;

	// Messages.
	constexpr uint32_t kMsgMouse           = 0xFA0C6390;
	constexpr uint32_t kMsgPostCityInit    = 0x26D31EC1;
	constexpr uint32_t kMsgPreCityShutdown = 0x26D31EC2;
	constexpr uint32_t kIID_MouseMessage   = 0x1A0C615F;

	// Windows of the city view.
	constexpr uint32_t kWin_SC4App       = 0x6104489A;
	constexpr uint32_t kWin_SC4View3DWin = 0x9A47B417;
	constexpr uint32_t kIID_View3DWin    = 0xFA47B3F9;

	// Commands of the city view (the keyboard shortcuts' commands).
	constexpr uint32_t kCmdCancel    = 0x6A935CF1;   // Escape
	constexpr uint32_t kCmdQueryTool = 0x6A935CF3;   // query tool
	constexpr uint32_t kCmdDemolish  = 0x6A935D36;   // bulldozer

	// ID of the query tool (cISC4ViewInputControl::GetID).
	constexpr uint32_t kToolQuery = 0xC7AF928E;

	// A press and a release make a click only if the mouse barely moved and the button was not
	// held long: a middle-button drag or hold is left to other mods.
	constexpr int32_t  kMaxMovePixels = 8;
	constexpr uint32_t kMaxHoldMs     = 800;

	// The query tool goes on to the bulldozer only within this delay after the middle click
	// that opened it; later (or after a left/right click) the middle click is Escape.
	constexpr uint32_t kChainMs = 3000;

	// Events of the game's mouse message: left and right button down.
	constexpr uint32_t kLeftDown = 7, kRightDown = 8;

	HMODULE gModule = nullptr;

	int32_t Abs(int32_t v) { return v < 0 ? -v : v; }

	// Is the cursor really over the game window (not another window on top or beside it)?
#ifdef SC4_TEST
	bool gTestOverGame = true;
	bool MouseOverGame() { return gTestOverGame; }
#else
	bool MouseOverGame()
	{
		POINT pt;
		HWND fg = GetForegroundWindow();
		if (!fg || !GetCursorPos(&pt)) return false;
		HWND under = WindowFromPoint(pt);
		return under && GetAncestor(under, GA_ROOT) == GetAncestor(fg, GA_ROOT);
	}
#endif

	// ---- game windows under the mouse ----------------------------------------------------
	constexpr int kMaxDepth = 10;
	struct ChildList { void* parent; void* list[64]; uint32_t count; };
	ChildList gChildren[kMaxDepth];   // one list per depth, static: keeps the stack frame small

	bool CollectDirectChildren(void*, uint32_t, void* child, void* context)
	{
		ChildList* cl = static_cast<ChildList*>(context);
		// EnumChildren also reaches deeper windows: keep the direct children only.
		if (child && VCall<void*>(child, Slot::Win_GetParentWin) == cl->parent && cl->count < 64)
			cl->list[cl->count++] = child;
		return true;
	}

	ChildList& Children(void* parent, int depth)
	{
		ChildList& cl = gChildren[depth];
		cl.parent = parent;
		cl.count = 0;
		VCall<bool>(parent, Slot::Win_EnumChildren, GZIID_cIGZWin,
		            static_cast<EnumChildrenCallback>(&CollectDirectChildren), static_cast<void*>(&cl));
		return cl;
	}

	// A window's rectangle is relative to its parent: add up the offsets to the root.
	void AbsolutePos(void* w, int32_t& x, int32_t& y)
	{
		x = y = 0;
		for (int depth = 0; w && depth < 16; ++depth)
		{
			x += VCall<int32_t>(w, Slot::Win_GetL);
			y += VCall<int32_t>(w, Slot::Win_GetT);
			w = VCall<void*>(w, Slot::Win_GetParentWin);
		}
	}

	// Is there an interface window under (x, y) inside `parent`? (No: the point is on the city.)
	// - A window that has children (a container) only counts through a visible child under the
	//   point: its empty parts are transparent. E.g. the game's bottom panel 0xAA32BCE6 is 428 px
	//   high while the visible panel is only its lower part. The city view is a container too.
	// - A window without children (a button, a picture, a panel's background...) counts, unless
	//   it covers the whole screen (a transparent overlay over the city).
	bool InterfaceUnder(void* parent, int32_t x, int32_t y, int depth = 0)
	{
		if (!parent || depth >= kMaxDepth) return false;
		static int32_t screenW, screenH;   // the main window, root of the walk
		if (depth == 0)
		{
			screenW = VCall<int32_t>(parent, Slot::Win_GetW);
			screenH = VCall<int32_t>(parent, Slot::Win_GetH);
		}
		ChildList& cl = Children(parent, depth);
		for (uint32_t i = 0; i < cl.count; ++i)
		{
			void* c = cl.list[i];
			if (!VCall<bool>(c, Slot::Win_IsVisible)) continue;
			const int32_t w = VCall<int32_t>(c, Slot::Win_GetW), h = VCall<int32_t>(c, Slot::Win_GetH);
			int32_t ax, ay;
			AbsolutePos(c, ax, ay);
			if (x < ax || x >= ax + w || y < ay || y >= ay + h) continue;

			if (depth + 1 < kMaxDepth && Children(c, depth + 1).count > 0)
			{
				if (InterfaceUnder(c, x, y, depth + 1)) return true;
				continue;   // an empty part of a container
			}
			if (ax <= 0 && ay <= 0 && ax + w >= screenW && ay + h >= screenH) continue;   // covers the whole screen
			return true;
		}
		return false;
	}
}

#ifdef SC4_TEST
extern "C" __declspec(dllexport) void __cdecl TestSetMouseOverGame(bool overGame) { gTestOverGame = overGame; }
#endif

static LRESULT __stdcall MouseHookProc(int code, WPARAM wParam, LPARAM lParam);

class MiddleClickDirector final : public cIGZCOMDirector, public cIGZFrameWorkHooks, public cIGZMessageTarget2
{
public:
	MiddleClickDirector()
		: refCount(0), com(nullptr), framework(nullptr),
		  ms2(nullptr), commandServer(nullptr), winMgr(nullptr), app(nullptr), view3D(nullptr),
		  disabled(false), hooked(false), subscribed(false), mouseHook(nullptr),
		  haveMouse(false), mouseX(0), mouseY(0), middleDown(false), pressX(0), pressY(0), pressTick(0),
		  chainArmed(false), chainTick(0)
	{
	}

	// ---- cIGZUnknown -----------------------------------------------------------------
	bool QueryInterface(uint32_t riid, void** ppvObj) override
	{
		switch (riid)
		{
		case GZIID_cIGZCOMDirector:    *ppvObj = static_cast<cIGZCOMDirector*>(this); break;
		case GZIID_cIGZFrameWorkHooks: *ppvObj = static_cast<cIGZFrameWorkHooks*>(this); break;
		case GZIID_cIGZMessageTarget2: *ppvObj = static_cast<cIGZMessageTarget2*>(this); break;
		case GZIID_cIGZUnknown:        *ppvObj = static_cast<cIGZUnknown*>(static_cast<cIGZCOMDirector*>(this)); break;
		default: return false;
		}
		AddRef();
		return true;
	}
	uint32_t AddRef() override { return ++refCount; }
	uint32_t Release() override { return RemoveRef(); }

	// ---- cIGZCOMDirector -------------------------------------------------------------
	bool InitializeCOM(cIGZCOM* pCOM, const cIGZString&) override
	{
		if (!pCOM) return false;
		com = pCOM;
		framework = VCall<cIGZFrameWork*>(pCOM, Slot::COM_FrameWork);
		return true;
	}

	bool OnStart(cIGZCOM*) override
	{
		Log::Init(gModule, "SC4 Middle Click Thingy 1.0.0");   // no file unless an error is written
		if (framework)
		{
			hooked = VCall<bool>(framework, Slot::FW_AddHook, static_cast<cIGZFrameWorkHooks*>(this));
		}
		if (!hooked) Log::Write("ERROR: could not hook the framework, the mod stays inactive");
		return true;
	}

	void EnumClassObjects(ClassObjectEnumerationCallback, void*) override {}
	bool GetClassObject(uint32_t, uint32_t, void**) override { return false; }
	bool CanUnloadNow() override { return false; }
	bool OnUnload() override { return true; }
	uint32_t RefCount() override { return refCount; }
	uint32_t RemoveRef() override { if (refCount > 0) --refCount; return refCount; }
	cIGZFrameWork* FrameWork() override { return framework; }
	cIGZCOM* GZCOM() override { return com; }
	uint32_t GetDirectorID() const override { return kDirectorID; }
	bool GetLibraryPath(cIGZString&) override { return false; }
	uint32_t GetHeapAllocatedSize() override { return 0; }

	// ---- cIGZFrameWorkHooks ----------------------------------------------------------
	bool PreFrameWorkInit() override { return true; }
	bool PreAppInit() override { return true; }

	bool PostAppInit() override
	{
		__try
		{
			Setup();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			Fail("PostAppInit", _exception_code());
		}
		return true;
	}

	bool PreAppShutdown() override
	{
		__try
		{
			Teardown();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			Log::Write("exception 0x%08X during shutdown (ignored)", _exception_code());
		}
		Log::Close();
		return true;
	}

	bool PostAppShutdown() override { return true; }
	bool PostSystemServiceShutdown() override { return true; }
	bool AbortiveQuit() override { return true; }
	bool OnInstall() override { return true; }

	// ---- cIGZMessageTarget2 ----------------------------------------------------------
	// Reads and notes only: nothing is opened or closed while the game dispatches.
	bool DoMessage(cIGZMessage2* msg) override
	{
		if (disabled || !msg) return true;
		__try
		{
			const uint32_t type = VCall<uint32_t>(msg, Slot::Msg_GetType);
			if (type == kMsgMouse)                OnMouseMessage(msg);
			else if (type == kMsgPostCityInit)    AcquireView3D();
			else if (type == kMsgPreCityShutdown) ReleaseView3D();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			Fail("DoMessage", _exception_code());
		}
		return true;
	}

	// ---- mouse hook (game thread, game message loop) ---------------------------------
	HHOOK Hook() const { return mouseHook; }

	void OnWindowsMouse(uint32_t message)
	{
		if (disabled) return;
		__try
		{
			if (!view3D) { middleDown = false; return; }
			if (message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK)   // a quick second click is a DBLCLK
			{
				middleDown = true;
				pressX = mouseX;
				pressY = mouseY;
				pressTick = GetTickCount();
			}
			else if (message == WM_MBUTTONUP && middleDown)
			{
				middleDown = false;
				OnMiddleClick();
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			Fail("the mouse hook", _exception_code());
		}
	}

private:
	void* GetService(uint32_t id, uint32_t iid)
	{
		void* p = nullptr;
		if (!VCall<bool>(framework, Slot::FW_GetSystemService, id, iid, &p)) return nullptr;
		return p;
	}

	void Setup()
	{
		ms2           = GetService(Service::kMessageServer2, Service::kIID_MessageServer2);
		commandServer = GetService(Service::kCommandServer, Service::kIID_CommandServer);
		winMgr        = GetService(Service::kWinMgr, Service::kIID_WinMgr);
		app           = GetService(Service::kSC4App, Service::kIID_SC4App);
		if (!ms2 || !commandServer || !winMgr || !app)
		{
			Log::Write("ERROR: missing game service (ms2 %c, commands %c, windows %c, app %c), the mod stays inactive",
			           ms2 ? 'y' : 'n', commandServer ? 'y' : 'n', winMgr ? 'y' : 'n', app ? 'y' : 'n');
			disabled = true;
			return;
		}

		// Subscribe once for the whole session.
		cIGZMessageTarget2* target = this;
		bool ok = VCall<bool>(ms2, Slot::MS2_AddNotification, target, kMsgMouse);
		ok &= VCall<bool>(ms2, Slot::MS2_AddNotification, target, kMsgPostCityInit);
		ok &= VCall<bool>(ms2, Slot::MS2_AddNotification, target, kMsgPreCityShutdown);
		subscribed = true;

		// PostAppInit runs on the game's main thread, the one that owns its window.
		mouseHook = SetWindowsHookExA(WH_MOUSE, &MouseHookProc, nullptr, GetCurrentThreadId());

		if (!ok || !mouseHook)
		{
			Log::Write("ERROR: setup failed (messages %c, mouse hook %c), the mod stays inactive", ok ? 'y' : 'n', mouseHook ? 'y' : 'n');
			disabled = true;
			return;
		}
	}

	void Teardown()
	{
		if (mouseHook) UnhookWindowsHookEx(mouseHook);
		mouseHook = nullptr;
		ReleaseView3D();
		if (ms2 && subscribed)
		{
			cIGZMessageTarget2* target = this;
			VCall<bool>(ms2, Slot::MS2_RemoveNotification, target, kMsgMouse);
			VCall<bool>(ms2, Slot::MS2_RemoveNotification, target, kMsgPostCityInit);
			VCall<bool>(ms2, Slot::MS2_RemoveNotification, target, kMsgPreCityShutdown);
			subscribed = false;
		}
		GZRelease(app);           app = nullptr;
		GZRelease(winMgr);        winMgr = nullptr;
		GZRelease(commandServer); commandServer = nullptr;
		GZRelease(ms2);           ms2 = nullptr;
		if (hooked) VCall<bool>(framework, Slot::FW_RemoveHook, static_cast<cIGZFrameWorkHooks*>(this));
		hooked = false;
	}

	void AcquireView3D()
	{
		ReleaseView3D();
		void* mainWin = VCall<void*>(app, Slot::App_GetMainWindow);
		void* appWin = mainWin ? VCall<void*>(mainWin, Slot::Win_GetChildWindowFromID, kWin_SC4App) : nullptr;
		void* v = nullptr;
		if (appWin && VCall<bool>(appWin, Slot::Win_GetChildAs, kWin_SC4View3DWin, kIID_View3DWin, &v) && v)
			view3D = v;   // held (GetChildAs adds a reference)
		else
			Log::Write("ERROR: the city view was not found, middle click inactive in this city");
		middleDown = false;
		chainArmed = false;
	}

	void ReleaseView3D()
	{
		if (view3D)
		{
			void* v = view3D;
			view3D = nullptr;
			GZRelease(v);
		}
		middleDown = false;
	}

	// Every mouse message gives the position (the middle button itself never comes this way).
	// A left or right click means the open tool was used: no more chaining to the bulldozer.
	void OnMouseMessage(cIGZMessage2* msg)
	{
		void* mouse = nullptr;
		if (!VCall<bool>(msg, Slot::QueryInterface, kIID_MouseMessage, &mouse) || !mouse) return;
		const uint32_t event = VCall<uint32_t>(mouse, Slot::Mouse_EventType);
		if (event == kLeftDown || event == kRightDown) chainArmed = false;
		GZPoint pt = { 0, 0 };
		VCall<GZPoint*>(mouse, Slot::Mouse_Position, &pt);   // returned through a hidden pointer
		GZRelease(mouse);
		mouseX = pt.x;
		mouseY = pt.y;
		haveMouse = true;
	}

	void OnMiddleClick()
	{
		if (!haveMouse) return;
		if (Abs(mouseX - pressX) > kMaxMovePixels || Abs(mouseY - pressY) > kMaxMovePixels) return;   // a drag
		if (GetTickCount() - pressTick > kMaxHoldMs) return;                                         // a long press
		if (!MouseOverGame()) return;

		// A modal dialog (e.g. the query tool's building window) runs its own message loop
		// while the tool that opened it waits: never close tools then.
		if (VCall<int32_t>(winMgr, Slot::WM_GetModalNestCount) > 0) return;

		// On the city only: not on a game panel, nor on another mod's window.
		if (InterfaceUnder(VCall<void*>(app, Slot::App_GetMainWindow), mouseX, mouseY)) return;

		void* tool = VCall<void*>(view3D, Slot::V3D_GetCurrentViewInputControl);   // no reference added
		const uint32_t now = GetTickCount();
		const bool chain = chainArmed && now - chainTick <= kChainMs;
		uint32_t cmd;
		if (!tool)                                                          cmd = kCmdQueryTool;
		else if (chain && VCall<uint32_t>(tool, Slot::VIC_GetID) == kToolQuery) cmd = kCmdDemolish;
		else                                                                cmd = kCmdCancel;
		chainArmed = (cmd == kCmdQueryTool);
		chainTick = now;

		void* in = nullptr;
		void* out = nullptr;
		VCall<bool>(commandServer, Slot::Cmd_CreateCommandParameterSet, &in);
		VCall<bool>(commandServer, Slot::Cmd_CreateCommandParameterSet, &out);
		if (in && out)
			VCall<bool>(view3D, Slot::V3D_ProcessCommand, cmd, in, out);   // the sets go by reference
		else
			Log::Write("ERROR: could not create the command parameters");
		GZRelease(out);
		GZRelease(in);
	}

	void Fail(const char* where, uint32_t code)
	{
		disabled = true;
		middleDown = false;
		Log::Write("ERROR: exception 0x%08X in %s, the mod is now inactive until the game restarts", code, where);
	}

	uint32_t refCount;
	cIGZCOM* com;
	cIGZFrameWork* framework;
	void* ms2;
	void* commandServer;
	void* winMgr;
	void* app;
	void* view3D;
	bool disabled;
	bool hooked;
	bool subscribed;
	HHOOK mouseHook;
	bool haveMouse;
	int32_t mouseX, mouseY;   // last position given by the game's mouse message
	bool middleDown;
	int32_t pressX, pressY;
	uint32_t pressTick;
	bool chainArmed;          // the query tool was just opened by a middle click...
	uint32_t chainTick;       // ...at this time
};

// ---- DLL entry points --------------------------------------------------------------------
namespace
{
	alignas(8) uint8_t gDirectorStorage[sizeof(MiddleClickDirector)];
	MiddleClickDirector* gDirector = nullptr;
}

// Only notes or acts, never swallows: every message goes on to the game and other hooks.
static LRESULT __stdcall MouseHookProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION && gDirector &&
	    (wParam == WM_MBUTTONDOWN || wParam == WM_MBUTTONUP || wParam == WM_MBUTTONDBLCLK))
	{
		gDirector->OnWindowsMouse(wParam);
	}
	return CallNextHookEx(gDirector ? gDirector->Hook() : nullptr, code, wParam, lParam);
}

#ifdef SC4_TEST
extern "C" __declspec(dllexport) void __cdecl TestMouseHook(uint32_t message) { MouseHookProc(HC_ACTION, message, 0); }
extern "C" __declspec(dllexport) bool __cdecl TestHookInstalled() { return gDirector && gDirector->Hook() != nullptr; }
#endif

extern "C" __declspec(dllexport) cIGZCOMDirector* GZDllGetGZCOMDirector()
{
	if (!gDirector) gDirector = new (gDirectorStorage) MiddleClickDirector();
	return gDirector;
}

extern "C" BOOL __stdcall DllEntry(HMODULE module, DWORD reason, void*)
{
	if (reason == DLL_PROCESS_ATTACH) gModule = module;
	else if (reason == DLL_PROCESS_DETACH) Log::Close();
	return 1;
}
