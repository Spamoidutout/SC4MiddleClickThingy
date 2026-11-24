// Copyright (C) 2026 Spamoidutout - SPDX-License-Identifier: LGPL-2.1-or-later
// Harness.cpp - a fake SimCity 4 that loads SC4MiddleClickThingy.dll and drives it.
// Build: build/build.sh. Run: cd test/out && WINEDEBUG=-all wine harness.exe
#include "GZ.h"
#include "Runtime.h"

IMPORT HMODULE WINAPI LoadLibraryA(const char* name);
IMPORT void* WINAPI GetProcAddress(HMODULE module, const char* name);
IMPORT void WINAPI ExitProcess(uint32_t code);
IMPORT HANDLE WINAPI GetStdHandle(DWORD id);
IMPORT void WINAPI Sleep(DWORD ms);

typedef void (__cdecl* TestSetMouseOverGameFn)(bool overGame);
static TestSetMouseOverGameFn gSetOverGame = nullptr;
typedef void (__cdecl* TestMouseHookFn)(uint32_t message);
typedef bool (__cdecl* TestHookInstalledFn)();
static TestMouseHookFn gHook = nullptr;
static TestHookInstalledFn gHookInstalled = nullptr;

// ---- console output ------------------------------------------------------------------
static int gFailures = 0, gChecks = 0;
static void Print(const char* s)
{
	DWORD n = 0, w = 0;
	while (s[n]) ++n;
	WriteFile(GetStdHandle((DWORD)-11), s, n, &w, nullptr);
}
static void Check(bool ok, const char* what)
{
	++gChecks;
	if (!ok) ++gFailures;
	Print(ok ? "  PASS  " : "  FAIL  ");
	Print(what);
	Print("\n");
}

// Filler virtual slots (never called).
#define FILL(n) virtual void fill##n() {}

// ---- fake objects --------------------------------------------------------------------
struct RefCounted : public cIGZUnknown
{
	int refs = 1;
	bool QueryInterface(uint32_t, void**) override { return false; }
	uint32_t AddRef() override { return ++refs; }
	uint32_t Release() override { return --refs; }
};

struct FakeParamSet : public RefCounted
{
	static int live;
	FakeParamSet() { ++live; }
	uint32_t Release() override { if (--refs == 0) { --live; } return refs; }
};
int FakeParamSet::live = 0;
alignas(8) static uint8_t gSetStorage[64][sizeof(FakeParamSet)];
static int gSetCount = 0;

struct FakeCommandServer : public RefCounted
{
	virtual bool Init() { return true; }                                      // 3
	virtual bool Shutdown() { return true; }                                  // 4
	virtual bool CreateCommandParameterSet(void** out)                        // 5
	{
		if (gSetCount >= 64) { *out = nullptr; return false; }
		FakeParamSet* s = new (gSetStorage[gSetCount++]) FakeParamSet();
		*out = s;
		return true;
	}
};

struct FakeControl : public RefCounted
{
	uint32_t id;
	explicit FakeControl(uint32_t id) : id(id) {}
	virtual bool Init() { return true; }       // 3
	virtual bool Shutdown() { return true; }   // 4
	virtual uint32_t GetID() { return id; }    // 5
};
static FakeControl gQueryTool(0xC7AF928E), gBulldozer(0x46DDB5F1), gRoadTool(0x12345678);

struct FakeView3D : public RefCounted
{
	FakeControl* top = nullptr;
	uint32_t lastCommand = 0;
	int commands = 0;
	bool crash = false;
	bool paramsOk = true;
	void* window = nullptr;
	virtual void* AsIGZWin() { return window; }   // 3
	FILL(4) FILL(5) FILL(6)
	virtual bool ProcessCommand(uint32_t cmd, cIGZUnknown& in, cIGZUnknown& out)   // 7
	{
		if (crash) { volatile int* p = nullptr; *p = 1; }
		paramsOk = paramsOk && FakeParamSet::live >= 2 && &in != &out;
		lastCommand = cmd;
		++commands;
		if (cmd == 0x6A935CF1) top = nullptr;                                    // Cancel
		else if (cmd == 0x6A935CF3) { if (top != &gQueryTool) top = &gQueryTool; } // QueryTool
		else if (cmd == 0x6A935D36) { if (top != &gBulldozer) top = &gBulldozer; } // DemolishTool
		return false;   // the game always returns false
	}
	FILL(8) FILL(9) FILL(10) FILL(11) FILL(12) FILL(13) FILL(14) FILL(15) FILL(16) FILL(17)
	FILL(18) FILL(19) FILL(20) FILL(21) FILL(22) FILL(23)
	virtual FakeControl* GetCurrentViewInputControl() { return top; }           // 24
};
static FakeView3D gView3D;

struct FakeWin : public RefCounted
{
	uint32_t id = 0;
	int32_t l = 0, t = 0, w = 0, h = 0;
	bool visible = true;
	FakeWin* parent = nullptr;
	FakeWin* children[8] = {};
	int count = 0;
	void Set(uint32_t i, int32_t x, int32_t y, int32_t ww, int32_t hh, FakeWin* p)
	{
		id = i; l = x; t = y; w = ww; h = hh; parent = p;
		if (p) p->children[p->count++] = this;
	}
	FILL(3) FILL(4) FILL(5) FILL(6) FILL(7) FILL(8) FILL(9) FILL(10)
	virtual FakeWin* GetParentWin() { return parent; }   // 11
	FILL(12) FILL(13) FILL(14) FILL(15) FILL(16) FILL(17) FILL(18) FILL(19) FILL(20) FILL(21)
	FILL(22) FILL(23) FILL(24) FILL(25) FILL(26) FILL(27) FILL(28) FILL(29) FILL(30) FILL(31)
	// Like the game, the enumeration also reaches deeper windows.
	virtual bool EnumChildren(uint32_t, EnumChildrenCallback cb, void* ctx)   // 32
	{
		for (int i = 0; i < count; ++i)
		{
			cb(this, children[i]->id, children[i], ctx);
			for (int j = 0; j < children[i]->count; ++j) cb(this, children[i]->children[j]->id, children[i]->children[j], ctx);
		}
		return true;
	}
	FILL(33)
	virtual void* GetChildWindowFromID(uint32_t i)   // 34
	{
		for (int k = 0; k < count; ++k) if (children[k]->id == i) return children[k];
		return nullptr;
	}
	FILL(35)
	virtual bool GetChildAs(uint32_t i, uint32_t iid, void** out)   // 36
	{
		if (i == 0x9A47B417 && iid == 0xFA47B3F9 && GetChildWindowFromID(i)) { gView3D.AddRef(); *out = &gView3D; return true; }
		return false;
	}
	FILL(37) FILL(38) FILL(39) FILL(40)
	virtual int32_t GetW() { return w; }   // 41
	virtual int32_t GetH() { return h; }   // 42
	virtual int32_t GetL() { return l; }   // 43
	virtual int32_t GetT() { return t; }   // 44
	FILL(45) FILL(46) FILL(47) FILL(48) FILL(49) FILL(50) FILL(51) FILL(52) FILL(53) FILL(54)
	FILL(55) FILL(56) FILL(57) FILL(58) FILL(59) FILL(60) FILL(61) FILL(62)
	virtual uint32_t GetID() { return id; }   // 63
	FILL(64) FILL(65) FILL(66) FILL(67) FILL(68) FILL(69) FILL(70)
	virtual bool IsVisible() { return visible; }   // 71
};
static FakeWin gMainWin, gAppWin, gViewWin, gAdvisorBar, gAppOverlay, gBottomPanel, gViewOverlay, gHiddenPanel;
static FakeWin gPortrait, gBarBack, gPolls, gPollsGraph, gPollsPanel, gPollsBack, gPollsButton;

static void BuildWindows()
{
	gMainWin.Set(0x11111111, 0, 0, 1280, 1024, nullptr);
	gAppWin.Set(0x6104489A, 0, 0, 1280, 1024, &gMainWin);
	gViewWin.Set(0x9A47B417, 0, 0, 1280, 1024, &gAppWin);
	gAdvisorBar.Set(0x5A0B0001, 1050, 850, 200, 100, &gAppWin);     // SC4 Advisor Control's bar
	gBarBack.Set(0x5A0B0002, 0, 0, 200, 100, &gAdvisorBar);         //   its background (covers the bar)
	gPortrait.Set(0x5A0B0003, 20, 10, 80, 80, &gAdvisorBar);        //   a portrait
	gAppOverlay.Set(0, 0, 0, 1280, 1024, &gAppWin);                 // full-size transparent overlay
	gBottomPanel.Set(0x0987B48F, 0, 924, 480, 100, &gViewWin);      // a game panel
	gViewOverlay.Set(0, 0, 0, 1280, 1024, &gViewWin);
	gHiddenPanel.Set(0x22222222, 0, 0, 400, 400, &gViewWin);
	gHiddenPanel.visible = false;
	// The game's bottom panel: a 546x428 container, only its lower part is drawn.
	gPolls.Set(0xAA32BCE6, 494, 600, 546, 428, &gViewWin);
	gPollsGraph.Set(0x33333333, 0, 0, 546, 300, &gPolls);           //   a hidden graph above the panel
	gPollsGraph.visible = false;
	gPollsPanel.Set(0x44444444, 0, 350, 546, 78, &gPolls);          //   the visible panel...
	gPollsBack.Set(0x44444445, 0, 0, 546, 78, &gPollsPanel);        //   ...its background
	gPollsButton.Set(0x44444446, 10, 10, 40, 40, &gPollsPanel);     //   ...a button
	gView3D.window = &gViewWin;
}

struct FakeApp : public RefCounted
{
	FILL(3) FILL(4) FILL(5) FILL(6) FILL(7) FILL(8) FILL(9) FILL(10) FILL(11) FILL(12) FILL(13)
	FILL(14) FILL(15) FILL(16) FILL(17) FILL(18) FILL(19) FILL(20) FILL(21) FILL(22) FILL(23)
	FILL(24) FILL(25)
	virtual void* GetMainWindow() { return &gMainWin; }   // 26
};
static FakeApp gApp;

struct FakeWinMgr : public RefCounted
{
	int32_t modal = 0;
	FILL(3) FILL(4) FILL(5) FILL(6) FILL(7) FILL(8) FILL(9) FILL(10) FILL(11) FILL(12) FILL(13)
	FILL(14) FILL(15) FILL(16) FILL(17) FILL(18) FILL(19) FILL(20) FILL(21) FILL(22) FILL(23)
	FILL(24) FILL(25) FILL(26) FILL(27) FILL(28) FILL(29) FILL(30) FILL(31) FILL(32) FILL(33)
	FILL(34) FILL(35) FILL(36) FILL(37) FILL(38) FILL(39) FILL(40) FILL(41) FILL(42)
	virtual int32_t GetModalNestCount() { return modal; }   // 43
};
static FakeWinMgr gWinMgr;

struct FakeMS2 : public RefCounted
{
	cIGZMessageTarget2* targets[8] = {};
	uint32_t types[8] = {};
	int count = 0;
	virtual bool MessageSend(cIGZMessage2* msg)   // 3
	{
		uint32_t type = VCall<uint32_t>(msg, Slot::Msg_GetType);
		for (int i = 0; i < count; ++i)
			if (targets[i] && types[i] == type) targets[i]->DoMessage(msg);
		return true;
	}
	virtual bool MessagePost(cIGZMessage2*, bool) { return true; }   // 4
	virtual bool AddNotification(cIGZMessageTarget2* t, uint32_t type)   // 5
	{
		if (count >= 8) return false;
		targets[count] = t; types[count] = type; ++count;
		return true;
	}
	virtual bool RemoveNotification(cIGZMessageTarget2* t, uint32_t type)   // 6
	{
		for (int i = 0; i < count; ++i)
			if (targets[i] == t && types[i] == type) { targets[i] = nullptr; return true; }
		return false;
	}
	int Active() { int n = 0; for (int i = 0; i < count; ++i) if (targets[i]) ++n; return n; }
};
static FakeMS2 gMS2;

struct FakeFramework : public RefCounted
{
	cIGZFrameWorkHooks* hooks = nullptr;
	FILL(3) FILL(4)
	virtual bool GetSystemService(uint32_t srv, uint32_t iid, void** out)                          // 5
	{
		cIGZUnknown* p = nullptr;
		if (srv == Service::kMessageServer2 && iid == Service::kIID_MessageServer2) p = &gMS2;
		else if (srv == Service::kCommandServer && iid == Service::kIID_CommandServer) p = &gCmdServer;
		else if (srv == Service::kWinMgr && iid == Service::kIID_WinMgr) p = &gWinMgr;
		else if (srv == Service::kSC4App && iid == Service::kIID_SC4App) p = &gApp;
		if (!p) return false;
		p->AddRef();
		*out = p;
		return true;
	}
	FILL(6)
	virtual bool AddHook(cIGZFrameWorkHooks* h) { hooks = h; return true; }                          // 7
	virtual bool RemoveHook(cIGZFrameWorkHooks* h) { if (hooks == h) hooks = nullptr; return true; } // 8
	FILL(9) FILL(10)
	static FakeCommandServer gCmdServer;
};
FakeCommandServer FakeFramework::gCmdServer;
static FakeFramework gFramework;

struct FakeCOM : public RefCounted
{
	virtual bool GetClassObject(uint32_t, uint32_t, void**) { return false; }   // 3
	virtual void* FrameWork() { return &gFramework; }                         // 4
};
static FakeCOM gCOM;

// Messages.
struct FakeMouse : public RefCounted
{
	uint32_t event = 0;
	GZPoint pos = { 0, 0 };
	FILL(3) FILL(4) FILL(5) FILL(6) FILL(7) FILL(8)
	virtual uint32_t EventType() { return event; }                                 // 9
	virtual GZPoint* Position(GZPoint* out) { *out = pos; return out; }           // 10 (hidden return pointer)
};
static FakeMouse gMouse;

struct FakeMessage : public RefCounted
{
	uint32_t type = 0;
	bool QueryInterface(uint32_t iid, void** out) override
	{
		if (type == 0xFA0C6390 && iid == 0x1A0C615F) { gMouse.AddRef(); *out = &gMouse; return true; }
		return false;
	}
	FILL(3)
	virtual uint32_t GetType() { return type; }   // 4
};
static FakeMessage gMsg;

static void Send(uint32_t type) { gMsg.type = type; gMS2.MessageSend(reinterpret_cast<cIGZMessage2*>(&gMsg)); }
static void Mouse(uint32_t event, int32_t x, int32_t y)
{
	gMouse.event = event; gMouse.pos.x = x; gMouse.pos.y = y;
	Send(0xFA0C6390);
}
static void Move(int32_t x, int32_t y) { Mouse(13, x, y); }

// A Windows message of the middle button, through the mod's mouse hook. No tick: the
// action must happen at once.
static void Button(bool down, bool overGame = true, uint32_t downMessage = 0x0207)
{
	gSetOverGame(overGame);
	gHook(down ? downMessage : 0x0208);
}

// A middle click at (x, y); returns the number of commands sent.
static int Click(int32_t x = 640, int32_t y = 400, bool overGame = true)
{
	int before = gView3D.commands;
	Move(x, y);
	Button(true, overGame);
	Button(false, overGame);
	return gView3D.commands - before;
}

typedef cIGZCOMDirector* (*GetDirectorFn)();

// No CRT: nobody runs the constructors of the global fakes, so build them here.
static void ConstructFakes()
{
	new (&gQueryTool) FakeControl(0xC7AF928E);
	new (&gBulldozer) FakeControl(0x46DDB5F1);
	new (&gRoadTool) FakeControl(0x12345678);
	new (&gView3D) FakeView3D();
	FakeWin* wins[] = { &gMainWin, &gAppWin, &gViewWin, &gAdvisorBar, &gAppOverlay, &gBottomPanel, &gViewOverlay, &gHiddenPanel,
	                    &gPortrait, &gBarBack, &gPolls, &gPollsGraph, &gPollsPanel, &gPollsBack, &gPollsButton };
	for (FakeWin* w : wins) new (w) FakeWin();
	new (&gApp) FakeApp();
	new (&gWinMgr) FakeWinMgr();
	new (&gMS2) FakeMS2();
	new (&FakeFramework::gCmdServer) FakeCommandServer();
	new (&gFramework) FakeFramework();
	new (&gCOM) FakeCOM();
	new (&gMouse) FakeMouse();
	new (&gMsg) FakeMessage();
	BuildWindows();
}

extern "C" void HarnessMain()
{
	ConstructFakes();
	Print("SC4 Middle Click Thingy - fake game test\n");
	HMODULE dll = LoadLibraryA("SC4MiddleClickThingy.dll");
	Check(dll != nullptr, "the DLL loads");
	if (!dll) ExitProcess(1);
	GetDirectorFn get = reinterpret_cast<GetDirectorFn>(GetProcAddress(dll, "GZDllGetGZCOMDirector"));
	Check(get != nullptr, "GZDllGetGZCOMDirector is exported");
	if (!get) ExitProcess(1);
	gSetOverGame = reinterpret_cast<TestSetMouseOverGameFn>(GetProcAddress(dll, "TestSetMouseOverGame"));
	gHook = reinterpret_cast<TestMouseHookFn>(GetProcAddress(dll, "TestMouseHook"));
	gHookInstalled = reinterpret_cast<TestHookInstalledFn>(GetProcAddress(dll, "TestHookInstalled"));
	Check(gSetOverGame && gHook && gHookInstalled, "test build: test functions are exported");
	if (!gSetOverGame || !gHook || !gHookInstalled) ExitProcess(1);

	cIGZCOMDirector* dir = get();
	Check(dir == get(), "the director is a single object");
	Check(dir->GetDirectorID() == 0x7C3E91A5, "director ID");
	void* q = nullptr;
	Check(dir->QueryInterface(GZIID_cIGZMessageTarget2, &q) && q, "QueryInterface(cIGZMessageTarget2)");
	if (q) static_cast<cIGZUnknown*>(q)->Release();

	char dummyString[16] = {};
	Check(dir->InitializeCOM(reinterpret_cast<cIGZCOM*>(&gCOM), *reinterpret_cast<cIGZString*>(dummyString)), "InitializeCOM");
	Check(dir->FrameWork() == reinterpret_cast<cIGZFrameWork*>(&gFramework), "the framework is kept");
	dir->OnStart(reinterpret_cast<cIGZCOM*>(&gCOM));
	Check(gFramework.hooks != nullptr, "OnStart adds the framework hooks");
	if (!gFramework.hooks) ExitProcess(1);

	gFramework.hooks->PostAppInit();
	Check(gMS2.Active() == 3, "PostAppInit subscribes to 3 messages");
	Check(gHookInstalled(), "the Windows mouse hook is installed (SetWindowsHookEx, game thread)");

	Print("-- before a city is open\n");
	Check(Click() == 0, "middle click outside a city does nothing");

	Print("-- city opened\n");
	int viewRefs = gView3D.refs;
	Send(0x26D31EC1);
	Check(gView3D.refs == viewRefs + 1, "the city view is held");

	Print("-- the cycle (clicks on the city)\n");
	gView3D.top = nullptr;
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935CF3 && gView3D.top == &gQueryTool, "no tool -> query tool");
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935D36 && gView3D.top == &gBulldozer, "query tool -> bulldozer");
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935CF1 && gView3D.top == nullptr, "bulldozer -> Escape");
	Check(Click() == 1 && gView3D.top == &gQueryTool, "and again: query tool");
	gView3D.top = &gRoadTool;
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935CF1 && gView3D.top == nullptr, "another tool (road) -> Escape");
	gView3D.top = &gBulldozer;   // picked with the keyboard: the cycle follows the real tool
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935CF1, "bulldozer picked from the keyboard -> Escape");

	Print("-- query tool -> bulldozer only right after the middle click that opened it\n");
	gView3D.top = nullptr;
	Check(Click() == 1 && gView3D.top == &gQueryTool, "no tool -> query tool");
	Sleep(3200);
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935CF1 && gView3D.top == nullptr, "3.2 s later: Escape, not the bulldozer");
	Check(Click() == 1 && gView3D.top == &gQueryTool, "query tool again");
	Mouse(7, 640, 400); Mouse(10, 640, 400);   // a building queried with the left button
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935CF1, "after a left click with the tool: Escape");
	Check(Click() == 1 && gView3D.top == &gQueryTool, "query tool again");
	Mouse(8, 640, 400); Mouse(11, 640, 400);
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935CF1, "after a right click: Escape");
	gView3D.top = &gQueryTool;   // opened with the keyboard or the button
	Check(Click() == 1 && gView3D.lastCommand == 0x6A935CF1, "query tool opened without the middle click -> Escape");
	Check(Click() == 1 && gView3D.top == &gQueryTool, "query tool");
	Sleep(1500);
	Check(Click() == 1 && gView3D.top == &gBulldozer, "1.5 s later: still the bulldozer");
	gView3D.top = nullptr;
	Check(Click(200, 200) == 1, "a hidden panel under the mouse does not block the click");
	Check(Click(1279, 1023) == 1, "click in the far corner of the city");

	Print("-- quick clicks: each one counts, at once\n");
	gView3D.top = nullptr;
	Move(640, 400);
	Button(true); Button(false);
	Button(true, true, 0x0209); Button(false);   // Windows makes the second quick click a DBLCLK
	Check(gView3D.top == &gBulldozer && gView3D.lastCommand == 0x6A935D36, "double click: query tool then bulldozer, no tick in between");
	Button(true, true, 0x0209); Button(false);
	Check(gView3D.top == nullptr, "third quick click: Escape");

	Print("-- empty parts of a container: the city\n");
	gView3D.top = nullptr;
	Check(Click(795, 629) == 1, "above the bottom panel, in its empty 428 px container (real log case)");
	Check(Click(753, 678) == 1, "same, over its hidden graph");
	Check(Click(700, 940) == 1, "between the panel and the top of its container");

	Print("-- interface windows: left alone\n");
	int c = gView3D.commands;
	Check(Click(1100, 900) == 0, "middle click on a portrait of SC4 Advisor Control is left to it");
	Check(Click(1220, 930) == 0, "middle click on the background of its bar too");
	Check(Click(520, 980) == 0, "middle click on a button of the bottom panel is ignored");
	Check(Click(900, 1000) == 0, "middle click on the background of the bottom panel is ignored");
	Check(Click(100, 950) == 0, "middle click on a game panel is ignored");
	Check(gView3D.commands == c, "no command sent");

	Print("-- what must be ignored\n");
	c = gView3D.commands;
	gView3D.top = nullptr;
	Move(100, 100); Button(true); Move(150, 100); Button(false);
	Check(gView3D.commands == c, "middle drag (50 px) is ignored");
	Move(100, 100); Button(true); Sleep(900); Button(false);
	Check(gView3D.commands == c, "middle hold (900 ms) is ignored");
	gHook(0x0208);
	Check(gView3D.commands == c, "a release without a press is ignored");
	Check(Click(640, 400, false) == 0, "the cursor over another window is ignored");
	Mouse(7, 640, 400); Mouse(10, 640, 400); Mouse(8, 640, 400); Mouse(11, 640, 400);
	Mouse(9, 640, 400); Mouse(12, 640, 400);
	Check(gView3D.commands == c, "the game's own mouse events never trigger anything");
	gWinMgr.modal = 1;
	Check(Click() == 0, "a click while a dialog is open is ignored");
	gWinMgr.modal = 0;
	Check(gView3D.commands == c, "and it is not replayed after the dialog");
	Check(Click() == 1, "clicks work again after the dialog");

	Check(gView3D.paramsOk, "ProcessCommand gets two distinct parameter sets");
	Check(FakeParamSet::live == 0, "every parameter set is released");

	Print("-- city closed\n");
	Send(0x26D31EC2);
	Check(gView3D.refs == viewRefs, "the city view is released");
	Check(Click() == 0, "middle click after leaving the city does nothing");

	Print("-- crash protection\n");
	Send(0x26D31EC1);
	gView3D.crash = true;
	Click();
	gView3D.crash = false;
	Check(Click() == 0, "after an exception in the game, the mod turns itself off");

	Print("-- shutdown\n");
	gFramework.hooks->PreAppShutdown();
	Check(gMS2.Active() == 0, "notifications removed");
	Check(gFramework.hooks == nullptr, "hooks removed");
	Check(!gHookInstalled(), "the Windows mouse hook is removed");
	Check(gMS2.refs == 1 && gApp.refs == 1 && gWinMgr.refs == 1 && FakeFramework::gCmdServer.refs == 1, "services released");

	Print(gFailures ? "RESULT: FAILED\n" : "RESULT: all checks passed\n");
	ExitProcess(gFailures ? 1 : 0);
}
