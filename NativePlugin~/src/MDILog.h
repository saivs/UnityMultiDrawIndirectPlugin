#pragma once

#include <stdio.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#endif

inline void DebugLog(const char* fmt, ...)
{
#ifdef _WIN32
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
#else
    (void)fmt;
#endif
}
