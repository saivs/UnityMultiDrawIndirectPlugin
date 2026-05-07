#pragma once

#include "MDIBackend.h"
#include "Unity/IUnityGraphics.h"

// Forward-declare Unity's Metal interfaces so this header can be included from
// plain C++ translation units. The full header (Unity/IUnityGraphicsMetal.h)
// hard-errors when included outside Objective-C, so it lives only in the .mm.
struct IUnityGraphicsMetalV2;
struct IUnityGraphicsMetalV1;

class MDIBackend_Metal final : public IMDIBackend
{
public:
    bool Initialize(IUnityInterfaces* unityInterfaces) override;
    void Shutdown() override;
    void ExecuteMDI(const MDIParams& params) override;
    bool IsSupported() const override;

private:
    IUnityGraphicsMetalV1* _metal = nullptr;
    IUnityGraphicsMetalV2* _metalV2 = nullptr;
    bool _initialized = false;
};
