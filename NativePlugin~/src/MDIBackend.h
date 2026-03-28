#pragma once

#include <cstdint>
#include "Unity/IUnityInterface.h"

// Shared parameter struct for MDI calls
struct MDIParams
{
    void*    argsBuffer;          // Native GPU buffer pointer (ID3D12Resource*, VkBuffer, etc.)
    void*    indexBuffer;         // Native index buffer pointer (for D3D11 IASetIndexBuffer rebind)
    void*    instanceIDBuffer;    // Per-instance vertex buffer (D3D11 only, nullptr on other APIs)
    uint32_t argsOffsetBytes;     // Byte offset into the args buffer
    uint32_t maxDrawCount;        // Number of draw commands to execute
    uint32_t indexFormat;         // 0 = R16_UINT, 1 = R32_UINT
    uint32_t instanceIDStride;    // Stride of per-instance data in bytes (0 = not used)
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

    // Resize the per-instance identity buffer (D3D11/D3D12 only, no-op on other APIs)
    virtual bool ResizeInstanceIDBuffer(uint32_t newMaxCount) { (void)newMaxCount; return true; }

    // Current identity buffer capacity
    virtual uint32_t GetMaxInstanceCount() const { return 0; }
};
