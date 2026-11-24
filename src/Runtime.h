// Copyright (C) 2026 Spamoidutout - SPDX-License-Identifier: LGPL-2.1-or-later
// Runtime.h - tiny replacement for the CRT: memory functions, placement new, a log file.
#pragma once
#include "Win32.h"

extern "C" void* __cdecl memset(void* dst, int value, size_t size);
extern "C" void* __cdecl memcpy(void* dst, const void* src, size_t size);

inline void* operator new(size_t, void* where) noexcept { return where; }

namespace Log
{
	// Prepares "<dll path without .dll>.log" next to the DLL. The file is only created
	// (overwritten) when the first line is written, i.e. when something goes wrong; it then
	// starts with `header`.
	void Init(HMODULE dll, const char* header);
	void Close();

	// printf-like, supports: %s %c %X %08X %%.
	void Write(const char* format, ...);
}
