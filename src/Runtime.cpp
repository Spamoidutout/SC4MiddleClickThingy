// Copyright (C) 2026 Spamoidutout - SPDX-License-Identifier: LGPL-2.1-or-later
// Runtime.cpp - see Runtime.h.
#include "Runtime.h"

extern "C" void* __cdecl memset(void* dst, int value, size_t size)
{
	volatile uint8_t* d = static_cast<volatile uint8_t*>(dst);   // volatile: keep clang from calling memset here
	while (size--) *d++ = static_cast<uint8_t>(value);
	return dst;
}

extern "C" void* __cdecl memcpy(void* dst, const void* src, size_t size)
{
	volatile uint8_t* d = static_cast<volatile uint8_t*>(dst);
	const uint8_t* s = static_cast<const uint8_t*>(src);
	while (size--) *d++ = *s++;
	return dst;
}

namespace
{
	HANDLE gFile = INVALID_HANDLE_VALUE;
	char gPath[300];               // the log file, created at the first line written
	const char* gHeader = nullptr; // first line of the file
	char gLine[512];   // static: keeps the stack frame small

	struct Out
	{
		char* p;
		char* end;
		void Put(char c) { if (p < end) *p++ = c; }
		void Puts(const char* s) { if (!s) s = "(null)"; while (*s) Put(*s++); }
	};

	void PutNumber(Out& o, uint32_t value, uint32_t base, int width, bool zero)
	{
		char digits[16];
		int n = 0;
		do { digits[n++] = "0123456789ABCDEF"[value % base]; value /= base; } while (value && n < 16);
		for (int len = n; len < width; ++len) o.Put(zero ? '0' : ' ');
		while (n) o.Put(digits[--n]);
	}

	void Format(Out& o, const char* f, __builtin_va_list args)
	{
		for (; *f; ++f)
		{
			if (*f != '%') { o.Put(*f); continue; }
			++f;
			bool zero = false;
			int width = 0;
			if (*f == '0') { zero = true; ++f; }
			while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
			switch (*f)
			{
			case 's': o.Puts(__builtin_va_arg(args, const char*)); break;
			case 'c': o.Put(static_cast<char>(__builtin_va_arg(args, int))); break;
			case 'X': PutNumber(o, __builtin_va_arg(args, uint32_t), 16, width, zero); break;
			case '%': o.Put('%'); break;
			case 0: return;
			default: o.Put('%'); o.Put(*f); break;
			}
		}
	}
}

void Log::Init(HMODULE dll, const char* header)
{
	DWORD n = GetModuleFileNameA(dll, gPath, sizeof(gPath) - 8);
	if (n == 0 || n >= sizeof(gPath) - 8) { gPath[0] = 0; return; }
	// Replace the extension (".dll") with ".log".
	DWORD dot = n;
	for (DWORD i = n; i > 0; --i)
	{
		if (gPath[i - 1] == '.') { dot = i - 1; break; }
		if (gPath[i - 1] == '\\' || gPath[i - 1] == '/') break;
	}
	const char* ext = ".log";
	for (int i = 0; i < 5; ++i) gPath[dot + i] = ext[i];
	gHeader = header;
}

void Log::Close()
{
	if (gFile != INVALID_HANDLE_VALUE) CloseHandle(gFile);
	gFile = INVALID_HANDLE_VALUE;
}

void Log::Write(const char* format, ...)
{
	if (gFile == INVALID_HANDLE_VALUE)
	{
		if (!gPath[0]) return;
		gFile = CreateFileA(gPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		gPath[0] = 0;   // one try only
		if (gFile == INVALID_HANDLE_VALUE) return;
		if (gHeader)
		{
			DWORD n = 0, written = 0;
			while (gHeader[n]) ++n;
			WriteFile(gFile, gHeader, n, &written, nullptr);
			WriteFile(gFile, "\r\n", 2, &written, nullptr);
		}
	}
	SYSTEMTIME t;
	GetLocalTime(&t);
	Out o = { gLine, gLine + sizeof(gLine) - 2 };
	PutNumber(o, t.wHour, 10, 2, true);   o.Put(':');
	PutNumber(o, t.wMinute, 10, 2, true); o.Put(':');
	PutNumber(o, t.wSecond, 10, 2, true); o.Put(' ');
	__builtin_va_list args;
	__builtin_va_start(args, format);
	Format(o, format, args);
	__builtin_va_end(args);
	*o.p++ = '\r';
	*o.p++ = '\n';
	DWORD written = 0;
	WriteFile(gFile, gLine, static_cast<DWORD>(o.p - gLine), &written, nullptr);
}
