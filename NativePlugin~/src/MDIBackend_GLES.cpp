#include "MDIBackend_GLES.h"
#include <cstring>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
static void DebugLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

// On Windows, Unity uses ANGLE or desktop GL. Resolve via GetProcAddress from the GL module.
static void* GLGetProcAddress(const char* name)
{
    // Try opengl32.dll first (desktop GL / ANGLE)
    static HMODULE glModule = nullptr;
    if (!glModule)
    {
        glModule = GetModuleHandleA("libGLESv2.dll"); // ANGLE
        if (!glModule)
            glModule = GetModuleHandleA("opengl32.dll"); // Desktop GL
    }

    if (glModule)
    {
        void* proc = (void*)GetProcAddress(glModule, name);
        if (proc) return proc;
    }

    // Fallback: try wglGetProcAddress
    typedef void* (__stdcall *PFN_wglGetProcAddress)(const char*);
    static PFN_wglGetProcAddress wglGetProc = nullptr;
    static bool wglResolved = false;
    if (!wglResolved)
    {
        wglResolved = true;
        HMODULE oglModule = GetModuleHandleA("opengl32.dll");
        if (oglModule)
            wglGetProc = (PFN_wglGetProcAddress)GetProcAddress(oglModule, "wglGetProcAddress");
    }
    if (wglGetProc)
        return wglGetProc(name);

    return nullptr;
}

#else
#include <dlfcn.h>
#define DebugLog(...) ((void)0)

// On Android / Linux, use eglGetProcAddress
static void* GLGetProcAddress(const char* name)
{
    // Try eglGetProcAddress
    typedef void (*(*PFN_eglGetProcAddress)(const char*))();
    static PFN_eglGetProcAddress eglGetProc = nullptr;
    static bool resolved = false;
    if (!resolved)
    {
        resolved = true;
        void* libEGL = dlopen("libEGL.so", RTLD_LAZY);
        if (libEGL)
            eglGetProc = (PFN_eglGetProcAddress)dlsym(libEGL, "eglGetProcAddress");
    }
    if (eglGetProc)
        return (void*)eglGetProc(name);

    // Fallback: try libGLESv2.so directly
    static void* libGLES = nullptr;
    if (!libGLES)
        libGLES = dlopen("libGLESv2.so", RTLD_LAZY);
    if (libGLES)
        return dlsym(libGLES, name);

    return nullptr;
}
#endif

// -----------------------------------------------------------------------
// GL function resolution
// -----------------------------------------------------------------------

bool MDIBackend_GLES::ResolveGLFunctions()
{
    _glBindBuffer = (PFNGLBINDBUFFERPROC)GLGetProcAddress("glBindBuffer");
    _glDrawElementsIndirect = (PFNGLDRAWELEMENTSINDIRECTPROC)GLGetProcAddress("glDrawElementsIndirect");

    if (!_glBindBuffer || !_glDrawElementsIndirect)
    {
        DebugLog("[MDI] GLES: failed to resolve core GL functions\n");
        return false;
    }

    // Try to get multi-draw indirect extension
    _glMultiDrawElementsIndirectEXT =
        (PFNGLMULTIDRAWELEMENTSINDIRECTEXTPROC)GLGetProcAddress("glMultiDrawElementsIndirectEXT");

    // Also try the non-EXT name (desktop GL 4.3 core)
    if (!_glMultiDrawElementsIndirectEXT)
        _glMultiDrawElementsIndirectEXT =
            (PFNGLMULTIDRAWELEMENTSINDIRECTEXTPROC)GLGetProcAddress("glMultiDrawElementsIndirect");

    return true;
}

bool MDIBackend_GLES::CheckMultiDrawIndirectExtension()
{
    if (_glMultiDrawElementsIndirectEXT)
    {
        // Function pointer resolved — extension is available
        DebugLog("[MDI] GLES: GL_EXT_multi_draw_indirect supported (hardware MDI)\n");
        return true;
    }

    DebugLog("[MDI] GLES: GL_EXT_multi_draw_indirect NOT supported (will use loop fallback)\n");
    return false;
}

// -----------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------

bool MDIBackend_GLES::Initialize(IUnityInterfaces* unityInterfaces)
{
    (void)unityInterfaces;

    if (!ResolveGLFunctions())
        return false;

    _multiDrawIndirectSupported = CheckMultiDrawIndirectExtension();

    _initialized = true;
    DebugLog("[MDI] GLES backend initialized (MDI: %s)\n",
        _multiDrawIndirectSupported ? "hardware" : "loop fallback");
    return true;
}

void MDIBackend_GLES::Shutdown()
{
    _glDrawElementsIndirect = nullptr;
    _glMultiDrawElementsIndirectEXT = nullptr;
    _glBindBuffer = nullptr;
    _initialized = false;
    _multiDrawIndirectSupported = false;
    DebugLog("[MDI] GLES backend shutdown\n");
}

// -----------------------------------------------------------------------
// ExecuteMDI
// -----------------------------------------------------------------------

void MDIBackend_GLES::ExecuteMDI(const MDIParams& params)
{
    if (!_initialized || !params.argsBuffer || params.maxDrawCount == 0)
        return;

    // On OpenGL, GetNativeBufferPtr() returns GLuint buffer name directly
    GLuint argsBufferGL = static_cast<GLuint>(reinterpret_cast<uintptr_t>(params.argsBuffer));
    GLenum indexType = (params.indexFormat == 1) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    const uint32_t stride = 20; // 5 * sizeof(uint32_t)

    // Bind the indirect args buffer
    _glBindBuffer(GL_DRAW_INDIRECT_BUFFER, argsBufferGL);

    if (_multiDrawIndirectSupported)
    {
        // Hardware multi-draw: single call
        _glMultiDrawElementsIndirectEXT(
            GL_TRIANGLES,
            indexType,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(params.argsOffsetBytes)),
            static_cast<GLsizei>(params.maxDrawCount),
            static_cast<GLsizei>(stride)
        );
    }
    else
    {
        // Fallback: loop with single indirect draws (ES 3.1 core)
        uintptr_t offset = params.argsOffsetBytes;
        for (uint32_t i = 0; i < params.maxDrawCount; ++i)
        {
            _glDrawElementsIndirect(
                GL_TRIANGLES,
                indexType,
                reinterpret_cast<const void*>(offset)
            );
            offset += stride;
        }
    }
}

bool MDIBackend_GLES::IsSupported() const
{
    return _initialized;
}
