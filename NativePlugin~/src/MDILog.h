#pragma once

#include <stdio.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

inline void DebugLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef _WIN32
    OutputDebugStringA(buf);
#elif defined(__ANDROID__)
    // Surfaces in `adb logcat -s GfxPluginMDI` alongside Unity logs.
    __android_log_print(ANDROID_LOG_INFO, "GfxPluginMDI", "%s", buf);
#else
    fputs(buf, stderr);
    fflush(stderr);
#endif
}
