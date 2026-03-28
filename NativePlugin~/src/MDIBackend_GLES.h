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

    PFNGLDRAWELEMENTSINDIRECTPROC _glDrawElementsIndirect = nullptr;
    PFNGLMULTIDRAWELEMENTSINDIRECTEXTPROC _glMultiDrawElementsIndirectEXT = nullptr;
    PFNGLBINDBUFFERPROC _glBindBuffer = nullptr;
    PFNGLGENBUFFERSPROC _glGenBuffers = nullptr;
    PFNGLDELETEBUFFERSPROC _glDeleteBuffers = nullptr;
    PFNGLBUFFERDATAPROC _glBufferData = nullptr;
    PFNGLVERTEXATTRIBIPOINTERPROC _glVertexAttribIPointer = nullptr;
    PFNGLVERTEXATTRIBDIVISORPROC _glVertexAttribDivisor = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC _glEnableVertexAttribArray = nullptr;

    // Per-instance identity buffer [0, 1, 2, ..., _maxInstanceCount-1]
    GLuint _instanceIDBuffer = 0;
    uint32_t _maxInstanceCount = kDefaultMaxInstanceCount;
    static constexpr uint32_t kDefaultMaxInstanceCount = 65536;

    // Unity maps VertexAttribute.TexCoord7 to attribute location 11
    static constexpr GLuint kTexcoord7AttribLocation = 11;

    bool _initialized = false;
    bool _multiDrawIndirectSupported = false;
};
