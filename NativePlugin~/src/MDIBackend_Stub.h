#pragma once

#include "MDIBackend.h"

// Stub backend for unsupported graphics APIs.
// All MDI calls are no-ops; C# side will use the fallback loop.
class MDIBackend_Stub final : public IMDIBackend
{
public:
    bool Initialize(IUnityInterfaces*) override { return false; }
    void Shutdown() override {}
    void ExecuteMDI(const MDIParams&) override {}
    bool IsSupported() const override { return false; }
};
