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

private:
    bool ResolveGLFunctions();
    bool CheckMultiDrawIndirectExtension();

    PFNGLDRAWELEMENTSINDIRECTPROC _glDrawElementsIndirect = nullptr;
    PFNGLMULTIDRAWELEMENTSINDIRECTEXTPROC _glMultiDrawElementsIndirectEXT = nullptr;
    PFNGLBINDBUFFERPROC _glBindBuffer = nullptr;

    bool _initialized = false;
    bool _multiDrawIndirectSupported = false;
};
