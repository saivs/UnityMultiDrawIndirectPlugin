#pragma once

#include "MDIBackend.h"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi.h>
#include "Unity/IUnityGraphicsD3D12.h"

class MDIBackend_D3D12 final : public IMDIBackend
{
public:
    bool Initialize(IUnityInterfaces* unityInterfaces) override;
    void Shutdown() override;
    void ExecuteMDI(const MDIParams& params) override;
    bool IsSupported() const override;

    // Must be called during UnityPluginLoad BEFORE device init.
    // Configures D3D12 event IDs so CommandRecordingState is accessible.
    void ConfigureEvents(IUnityInterfaces* unityInterfaces, int baseEventID, int count);

private:
    IUnityGraphicsD3D12v7* _d3d12       = nullptr;
    ID3D12Device*          _device       = nullptr;
    ID3D12CommandSignature* _cmdSignature = nullptr;
    bool                   _initialized  = false;
};

#endif // _WIN32
