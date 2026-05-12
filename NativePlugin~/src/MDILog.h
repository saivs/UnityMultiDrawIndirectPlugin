#pragma once

#include <stdio.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

// Optional C# callback (registered via MDI_SetLogCallback) that routes plugin
// logs into Unity's Debug.Log. When unset, falls back to platform-default
// output (OutputDebugStringA on Windows, stderr elsewhere).
//
// Define MDI_DEBUG_LOG (e.g. via CMake) to compile in the diagnostic logging
// scattered through the backends. Without it, DebugLog is an empty inline
// that the optimizer eliminates entirely — release builds emit no log spam
// and pay no runtime cost.
using MDILogCallback = void (*)(const char* msg);
inline MDILogCallback g_mdiLogCallback = nullptr;

#ifdef MDI_DEBUG_LOG

inline void DebugLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (g_mdiLogCallback)
    {
        g_mdiLogCallback(buf);
        return;
    }

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

#else  // !MDI_DEBUG_LOG

inline void DebugLog(const char* /*fmt*/, ...) {}

#endif // MDI_DEBUG_LOG
