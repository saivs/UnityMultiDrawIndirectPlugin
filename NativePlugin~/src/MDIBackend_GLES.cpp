#include "MDIBackend_GLES.h"
#include <cstring>
#include <stdio.h>
#include <vector>

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

    // Identity buffer support
    _glGenBuffers = (PFNGLGENBUFFERSPROC)GLGetProcAddress("glGenBuffers");
    _glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)GLGetProcAddress("glDeleteBuffers");
    _glBufferData = (PFNGLBUFFERDATAPROC)GLGetProcAddress("glBufferData");
    _glVertexAttribIPointer = (PFNGLVERTEXATTRIBIPOINTERPROC)GLGetProcAddress("glVertexAttribIPointer");
    _glVertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORPROC)GLGetProcAddress("glVertexAttribDivisor");
    _glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)GLGetProcAddress("glEnableVertexAttribArray");
    _glGetIntegerv = (PFNGLGETINTEGERVPROC)GLGetProcAddress("glGetIntegerv");
    _glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)GLGetProcAddress("glGetAttribLocation");

    _glGetError = (PFNGLGETERRORPROC)GLGetProcAddress("glGetError");
    _glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)GLGetProcAddress("glGenVertexArrays");
    _glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)GLGetProcAddress("glDeleteVertexArrays");
    _glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)GLGetProcAddress("glBindVertexArray");

    if (!_glGenBuffers || !_glDeleteBuffers || !_glBufferData ||
        !_glVertexAttribIPointer || !_glVertexAttribDivisor || !_glEnableVertexAttribArray ||
        !_glGetIntegerv || !_glGetAttribLocation ||
        !_glGenVertexArrays || !_glDeleteVertexArrays || !_glBindVertexArray)
    {
        DebugLog("[MDI] GLES: failed to resolve GL functions\n");
        return false;
    }

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

void MDIBackend_GLES::CreateInstanceIDBuffer()
{
    std::vector<uint32_t> data(_maxInstanceCount);
    for (uint32_t i = 0; i < _maxInstanceCount; ++i)
        data[i] = i;

    // Save Unity's current GL_ARRAY_BUFFER binding so we can restore it
    GLint prevArrayBuffer = 0;
    _glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    _glGenBuffers(1, &_instanceIDBuffer);
    _glBindBuffer(GL_ARRAY_BUFFER, _instanceIDBuffer);
    _glBufferData(GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(_maxInstanceCount * sizeof(uint32_t)),
        data.data(), GL_STATIC_DRAW);

    // Restore Unity's previous binding
    _glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));

    DebugLog("[MDI] GLES Identity buffer ready: %u entries, %u bytes\n",
             _maxInstanceCount, _maxInstanceCount * (uint32_t)sizeof(uint32_t));
}

void MDIBackend_GLES::BindInstanceIDAttribute()
{
    // Query the currently active shader program to find TEXCOORD7 attribute location.
    // Unity's HLSLcc cross-compiler generates GLSL variable names from HLSL semantics,
    // but the exact name and location vary depending on the shader and compiler version.
    GLint program = 0;
    _glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    if (program == 0)
    {
        DebugLog("[MDI] GLES: BindInstanceIDAttribute — no program bound!\n");
        return;
    }

    // Try known HLSLcc naming conventions for TEXCOORD7
    static const char* candidates[] = {
        "in_TEXCOORD7",      // HLSLcc default for vertex inputs
        "vs_TEXCOORD7",      // alternative HLSLcc prefix
        "TEXCOORD7",         // raw semantic name
    };

    GLint location = -1;
    for (const char* name : candidates)
    {
        location = _glGetAttribLocation(static_cast<GLuint>(program), name);
        if (location >= 0)
        {
            DebugLog("[MDI] GLES: found TEXCOORD7 at location %d (name: %s, program: %d)\n",
                     location, name, program);
            break;
        }
    }

    if (location < 0)
    {
        DebugLog("[MDI] GLES: TEXCOORD7 attribute not found in shader program %d\n", program);
        return;
    }

    // Save Unity's GL_ARRAY_BUFFER binding
    GLint prevArrayBuffer = 0;
    _glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);

    // Bind identity buffer to the discovered location as per-instance data.
    // baseInstance in draw args offsets per-instance attribute reads automatically.
    _glBindBuffer(GL_ARRAY_BUFFER, _instanceIDBuffer);
    _glVertexAttribIPointer(static_cast<GLuint>(location), 1, GL_UNSIGNED_INT, sizeof(uint32_t), nullptr);
    _glVertexAttribDivisor(static_cast<GLuint>(location), 1);
    _glEnableVertexAttribArray(static_cast<GLuint>(location));

    // Restore Unity's previous GL_ARRAY_BUFFER binding
    _glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
}

void MDIBackend_GLES::OnDeviceReset()
{
    // Device reset on OpenGL destroys all GL objects.
    // Mark handles as invalid — they will be lazily recreated.
    _instanceIDBuffer = 0;
    DebugLog("[MDI] GLES: device reset — invalidated identity buffer\n");
}

bool MDIBackend_GLES::ResizeInstanceIDBuffer(uint32_t newMaxCount)
{
    if (newMaxCount == 0) return false;
    if (newMaxCount == _maxInstanceCount && _instanceIDBuffer) return true;

    if (_instanceIDBuffer) { _glDeleteBuffers(1, &_instanceIDBuffer); _instanceIDBuffer = 0; }

    _maxInstanceCount = newMaxCount;
    CreateInstanceIDBuffer();
    return _instanceIDBuffer != 0;
}

bool MDIBackend_GLES::Initialize(IUnityInterfaces* unityInterfaces)
{
    (void)unityInterfaces;

    if (!ResolveGLFunctions())
        return false;

    _multiDrawIndirectSupported = CheckMultiDrawIndirectExtension();
    CreateInstanceIDBuffer();

    _initialized = true;
    DebugLog("[MDI] GLES backend initialized (MDI: %s)\n",
        _multiDrawIndirectSupported ? "hardware" : "loop fallback");
    return true;
}

void MDIBackend_GLES::Shutdown()
{
    if (_instanceIDBuffer && _glDeleteBuffers)
    {
        _glDeleteBuffers(1, &_instanceIDBuffer);
        _instanceIDBuffer = 0;
    }
    _glDrawElementsIndirect = nullptr;
    _glMultiDrawElementsIndirectEXT = nullptr;
    _glBindBuffer = nullptr;
    _glGenBuffers = nullptr;
    _glDeleteBuffers = nullptr;
    _glBufferData = nullptr;
    _glVertexAttribIPointer = nullptr;
    _glVertexAttribDivisor = nullptr;
    _glEnableVertexAttribArray = nullptr;
    _glGetIntegerv = nullptr;
    _glGetAttribLocation = nullptr;
    _glGenVertexArrays = nullptr;
    _glDeleteVertexArrays = nullptr;
    _glBindVertexArray = nullptr;
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

    GLuint argsBufferGL = static_cast<GLuint>(reinterpret_cast<uintptr_t>(params.argsBuffer));
    GLenum indexType = (params.indexFormat == 1) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    const uint32_t stride = 20; // 5 * sizeof(uint32_t)

    // Drain any pre-existing GL errors so our check is clean
    if (_glGetError) while (_glGetError() != GL_NO_ERROR) {}

    // Save Unity's current VAO so we can restore it after MDI draw
    GLint prevVAO = 0;
    _glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);

    GLint prevProgram = 0;
    _glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

    GLint prevFBO = 0;
    _glGetIntegerv(0x8CA6 /*GL_DRAW_FRAMEBUFFER_BINDING*/, &prevFBO);

    static int s_logCounter = 0;
    if (s_logCounter < 10 || s_logCounter % 300 == 0)
        DebugLog("[MDI] GLES ExecuteMDI #%d: prevVAO=%d, program=%d, FBO=%d, args=0x%p, ib=0x%p, draws=%u\n",
                 s_logCounter, prevVAO, prevProgram, prevFBO,
                 params.argsBuffer, params.indexBuffer, params.maxDrawCount);
    s_logCounter++;

    // Lazy (re)creation of identity buffer
    if (_instanceIDBuffer == 0)
        CreateInstanceIDBuffer();

    // Create a fresh VAO every call — Unity may invalidate GL objects
    // on maximize/detach without firing device reset events.
    GLuint tempVAO = 0;
    _glGenVertexArrays(1, &tempVAO);

    // Bind our own VAO — all subsequent state changes are isolated from Unity
    _glBindVertexArray(tempVAO);

    // Bind caller's index buffer
    if (params.indexBuffer)
    {
        GLuint indexBufferGL = static_cast<GLuint>(reinterpret_cast<uintptr_t>(params.indexBuffer));
        _glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferGL);

    }

    // Bind identity buffer to TEXCOORD7 as per-instance data
    if (_instanceIDBuffer && params.instanceIDStride > 0)
    {
        BindInstanceIDAttribute();

    }

    // Bind the indirect args buffer
    _glBindBuffer(GL_DRAW_INDIRECT_BUFFER, argsBufferGL);


    if (_multiDrawIndirectSupported)
    {
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

    // Restore Unity's state and clean up
    _glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    _glBindVertexArray(static_cast<GLuint>(prevVAO));
    _glDeleteVertexArrays(1, &tempVAO);
}

bool MDIBackend_GLES::IsSupported() const
{
    return _initialized;
}
