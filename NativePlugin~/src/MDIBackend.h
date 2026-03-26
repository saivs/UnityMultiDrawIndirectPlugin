#pragma once

#include <cstdint>
#include "Unity/IUnityInterface.h"

// Shared parameter struct for MDI calls
struct MDIParams
{
    void*    argsBuffer;      // Native GPU buffer pointer (ID3D12Resource*, VkBuffer, etc.)
    void*    indexBuffer;     // Native index buffer pointer (for D3D11 IASetIndexBuffer rebind)
    uint32_t argsOffsetBytes; // Byte offset into the args buffer
    uint32_t maxDrawCount;    // Number of draw commands to execute
    uint32_t indexFormat;     // 0 = R16_UINT, 1 = R32_UINT
};

static constexpr int MDI_MAX_PENDING = 256;

// Abstract backend interface — implement per graphics API
struct IMDIBackend
{
    virtual ~IMDIBackend() = default;

    // Called once when the graphics device is created
    virtual bool Initialize(IUnityInterfaces* unityInterfaces) = 0;

    // Called when the graphics device is destroyed
    virtual void Shutdown() = 0;

    // Execute multi-draw indirect using params from the given slot
    virtual void ExecuteMDI(const MDIParams& params) = 0;

    // Returns true if the backend is ready to issue MDI calls
    virtual bool IsSupported() const = 0;
};
