#pragma once

#include "MDIBackend.h"
#include "gles/gles_minimal.h"

class MDIBackend_GLES final : public IMDIBackend
{
public:
    bool Initialize(IUnityInterfaces* unityInterfaces) override;
    void Shutdown() override;
    void ExecuteMDI(const MDIParams& params) override;
    bool IsSupported() const override;
    bool ResizeInstanceIDBuffer(uint32_t newMaxCount) override;
    uint32_t GetMaxInstanceCount() const override { return _maxInstanceCount; }

private:
    bool ResolveGLFunctions();
    bool CheckMultiDrawIndirectExtension();
    void CreateInstanceIDBuffer();
    void BindInstanceIDAttribute();

    static GLuint GetDrawMode(uint32_t topology);

    PFNGLDRAWELEMENTSINDIRECTPROC _glDrawElementsIndirect = nullptr;
    PFNGLMULTIDRAWELEMENTSINDIRECTEXTPROC _glMultiDrawElementsIndirectEXT = nullptr;
    PFNGLBINDBUFFERPROC _glBindBuffer = nullptr;
    PFNGLGENBUFFERSPROC _glGenBuffers = nullptr;
    PFNGLDELETEBUFFERSPROC _glDeleteBuffers = nullptr;
    PFNGLBUFFERDATAPROC _glBufferData = nullptr;
    PFNGLVERTEXATTRIBIPOINTERPROC _glVertexAttribIPointer = nullptr;
    PFNGLVERTEXATTRIBDIVISORPROC _glVertexAttribDivisor = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC _glEnableVertexAttribArray = nullptr;
    PFNGLGETINTEGERVPROC _glGetIntegerv = nullptr;
    PFNGLGETATTRIBLOCATIONPROC _glGetAttribLocation = nullptr;
    PFNGLGETERRORPROC _glGetError = nullptr;
    PFNGLGENVERTEXARRAYSPROC _glGenVertexArrays = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC _glDeleteVertexArrays = nullptr;
    PFNGLBINDVERTEXARRAYPROC _glBindVertexArray = nullptr;
    PFNGLISVERTEXARRAYPROC _glIsVertexArray = nullptr;

    // Per-instance identity buffer [0, 1, 2, ..., _maxInstanceCount-1]
    GLuint _instanceIDBuffer = 0;

    // Cached VAO for MDI draws — validated via glIsVertexArray before use
    GLuint _mdiVAO = 0;

    // Cached TEXCOORD7 attribute location per shader program
    GLuint _cachedProgram = 0;
    GLint _cachedTexcoord7Location = -1;
    uint32_t _maxInstanceCount = kDefaultMaxInstanceCount;
    static constexpr uint32_t kDefaultMaxInstanceCount = 65536;

    bool _initialized = false;
    bool _multiDrawIndirectSupported = false;
};
