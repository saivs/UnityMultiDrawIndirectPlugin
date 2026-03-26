#include "MDIBackend_D3D12.h"

#ifdef _WIN32

#include <stdio.h>

static void DebugLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

// -----------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------

bool MDIBackend_D3D12::Initialize(IUnityInterfaces* unityInterfaces)
{
    // Require v7 — v6 has a different vtable layout and cannot be cast
    _d3d12 = unityInterfaces->Get<IUnityGraphicsD3D12v7>();
    if (!_d3d12)
    {
        DebugLog("[MDI] D3D12 v7 interface not available\n");
        return false;
    }

    _device = _d3d12->GetDevice();
    if (!_device) return false;

    // Command signature for ExecuteIndirect with DrawIndexed args
    // ByteStride = 5 * sizeof(uint32) = 20, matches Unity's IndirectDrawIndexedArgs
    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
    sigDesc.ByteStride = 20;
    sigDesc.NumArgumentDescs = 1;
    sigDesc.pArgumentDescs = &argDesc;
    sigDesc.NodeMask = 0;
    // pRootSignature = nullptr: DrawIndexed-only signatures don't need a root signature

    HRESULT hr = _device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&_cmdSignature));
    if (FAILED(hr))
    {
        DebugLog("[MDI] CreateCommandSignature failed: 0x%08X\n", hr);
        return false;
    }

    _initialized = true;
    DebugLog("[MDI] D3D12 backend initialized\n");
    return true;
}

void MDIBackend_D3D12::ConfigureEvents(IUnityInterfaces* unityInterfaces, int baseEventID, int count)
{
    auto* d3d12 = unityInterfaces->Get<IUnityGraphicsD3D12v7>();
    if (!d3d12) return;

    for (int i = 0; i < count; ++i)
    {
        UnityD3D12PluginEventConfig config = {};
        // Render thread access — enables CommandRecordingState
        config.graphicsQueueAccess = kUnityD3D12GraphicsQueueAccess_DontCare;
        // ExecuteIndirect with DrawIndexed-only signature does NOT modify
        // root signature, PSO, descriptor heaps, or vertex/index buffer bindings.
        // NOT setting ModifiesCommandBuffersState avoids Unity doing state save/restore
        // which can corrupt its internal command list tracking.
        config.flags = 0;
        // The preceding DrawProceduralIndirect already bound render targets.
        // Setting this to false avoids Unity ending/restarting render passes around our event.
        config.ensureActiveRenderTextureIsBound = false;
        d3d12->ConfigureEvent(baseEventID + i, &config);
    }

    DebugLog("[MDI] Configured D3D12 events [%d .. %d)\n", baseEventID, baseEventID + count);
}

void MDIBackend_D3D12::Shutdown()
{
    if (_cmdSignature)
    {
        _cmdSignature->Release();
        _cmdSignature = nullptr;
    }
    _device = nullptr;
    _d3d12 = nullptr;
    _initialized = false;
    DebugLog("[MDI] D3D12 backend shutdown\n");
}

// -----------------------------------------------------------------------
// ExecuteMDI
// -----------------------------------------------------------------------

void MDIBackend_D3D12::ExecuteMDI(const MDIParams& params)
{
    if (!_initialized || !params.argsBuffer || params.maxDrawCount == 0)
        return;

    // Get Unity's currently recording command list
    UnityGraphicsD3D12RecordingState recordingState = {};
    if (!_d3d12->CommandRecordingState(&recordingState))
        return;

    ID3D12GraphicsCommandList* cmdList = recordingState.commandList;
    if (!cmdList) return;

    auto* argsResource = static_cast<ID3D12Resource*>(params.argsBuffer);

    // The args buffer is already in INDIRECT_ARGUMENT state from the
    // preceding DrawProceduralIndirect call in the same command buffer.
    // PSO, root signature, descriptor heaps, render targets — all bound
    // by that same prime draw. We just issue additional draws.
    cmdList->ExecuteIndirect(
        _cmdSignature,
        params.maxDrawCount,
        argsResource,
        params.argsOffsetBytes,
        nullptr, 0
    );
}

bool MDIBackend_D3D12::IsSupported() const
{
    return _initialized && _cmdSignature != nullptr;
}

#endif // _WIN32
