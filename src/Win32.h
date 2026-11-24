// Copyright (C) 2026 Spamoidutout - SPDX-License-Identifier: LGPL-2.1-or-later
// Win32.h - the few Win32 declarations the mod needs (no windows.h, no CRT).
#pragma once

typedef unsigned int       uint32_t;
typedef int                int32_t;
typedef unsigned short     uint16_t;
typedef unsigned char      uint8_t;
typedef unsigned int       uintptr_t;
typedef unsigned int       size_t;
typedef int                intptr_t;

typedef int                BOOL;
typedef unsigned int       DWORD;
typedef unsigned short     WORD;
typedef void*              HANDLE;
typedef void*              HMODULE;
typedef void*              HWND;
struct POINT { int32_t x, y; };
typedef void*              HHOOK;
typedef unsigned int       WPARAM;
typedef int                LPARAM;
typedef int                LRESULT;
typedef LRESULT (__stdcall* HOOKPROC)(int code, WPARAM wParam, LPARAM lParam);

#define WINAPI __stdcall
#define IMPORT extern "C" __declspec(dllimport)

struct SYSTEMTIME
{
	WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

IMPORT HANDLE WINAPI CreateFileA(const char* name, DWORD access, DWORD share, void* security,
                                 DWORD creation, DWORD flags, HANDLE templateFile);
IMPORT BOOL   WINAPI WriteFile(HANDLE file, const void* buffer, DWORD size, DWORD* written, void* overlapped);
IMPORT BOOL   WINAPI CloseHandle(HANDLE h);
IMPORT DWORD  WINAPI GetModuleFileNameA(HMODULE module, char* path, DWORD size);
IMPORT DWORD  WINAPI GetTickCount(void);
IMPORT void   WINAPI GetLocalTime(SYSTEMTIME* time);
IMPORT DWORD  WINAPI GetCurrentThreadId(void);

// user32
IMPORT HWND   WINAPI GetForegroundWindow(void);
IMPORT BOOL   WINAPI GetCursorPos(POINT* pt);
IMPORT HWND   WINAPI WindowFromPoint(POINT pt);
IMPORT HWND   WINAPI GetAncestor(HWND hwnd, unsigned int flags);
IMPORT HHOOK  WINAPI SetWindowsHookExA(int type, HOOKPROC proc, HMODULE module, DWORD threadId);
IMPORT BOOL   WINAPI UnhookWindowsHookEx(HHOOK hook);
IMPORT LRESULT WINAPI CallNextHookEx(HHOOK hook, int code, WPARAM wParam, LPARAM lParam);

#define WH_MOUSE         7
#define HC_ACTION        0
#define WM_MBUTTONDOWN   0x0207
#define WM_MBUTTONUP     0x0208
#define WM_MBUTTONDBLCLK 0x0209

#define GA_ROOT    2

#define GENERIC_WRITE          0x40000000u
#define FILE_SHARE_READ        0x00000001u
#define CREATE_ALWAYS          2u
#define FILE_ATTRIBUTE_NORMAL  0x80u
#define INVALID_HANDLE_VALUE   ((HANDLE)(intptr_t)-1)

#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0

#define EXCEPTION_EXECUTE_HANDLER 1
// _exception_code() is a clang builtin inside an __except filter.
