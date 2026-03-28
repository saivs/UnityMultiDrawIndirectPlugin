#pragma once

#include <stdio.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#endif

// File-based debug log for MDI plugin.
// Writes to mdi_log.txt next to the DLL itself (absolute path).
// Also sends to OutputDebugString.

inline void DebugLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

#ifdef _WIN32
    OutputDebugStringA(buf);
#endif

    static FILE* logFile = nullptr;
    if (!logFile)
    {
#ifdef _WIN32
        // Build absolute path next to the DLL
        char dllPath[MAX_PATH] = {};
        HMODULE hm = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&DebugLog), &hm);
        if (hm)
        {
            GetModuleFileNameA(hm, dllPath, MAX_PATH);
            // Replace DLL filename with log filename
            char* lastSlash = strrchr(dllPath, '\\');
            if (!lastSlash) lastSlash = strrchr(dllPath, '/');
            if (lastSlash)
                strcpy(lastSlash + 1, "mdi_log.txt");
            else
                strcpy(dllPath, "mdi_log.txt");
            logFile = fopen(dllPath, "w");
        }
        else
#endif
        {
            logFile = fopen("mdi_log.txt", "w");
        }
        if (logFile)
            fprintf(logFile, "=== MDI Plugin Log ===\n");
    }
    if (logFile)
    {
        fputs(buf, logFile);
        fflush(logFile);
    }
}
