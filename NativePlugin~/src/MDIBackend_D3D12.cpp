#include "MDIBackend_D3D12.h"

#ifdef _WIN32

#include <vector>
#include <cstring>
#include "MDILog.h"
#include "InlineHook.h"

// -----------------------------------------------------------------------
// PSO hook: inject per-instance TEXCOORD7 into graphics PSOs with TEXCOORD7
// -----------------------------------------------------------------------

static constexpr uint32_t kInstanceVBSlot = 15;

// Legacy API
using PFN_CreateGraphicsPipelineState = HRESULT(STDMETHODCALLTYPE*)(
    ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);

// Stream-based API (ID3D12Device2)
using PFN_CreatePipelineState = HRESULT(STDMETHODCALLTYPE*)(
    ID3D12Device*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);

// Pipeline Library hook — Unity may cache PSOs via ID3D12PipelineLibrary
using PFN_CreatePipelineLibrary = HRESULT(STDMETHODCALLTYPE*)(
    ID3D12Device*, const void*, SIZE_T, REFIID, void**);
using PFN_LoadGraphicsPipeline = HRESULT(STDMETHODCALLTYPE*)(
    void*, LPCWSTR, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);

static PFN_CreateGraphicsPipelineState g_origCreateGraphicsPSO  = nullptr;
static PFN_CreatePipelineState         g_origCreatePipelineState = nullptr;
static bool g_deviceHooked = false;

static uint32_t g_psoLegacyCallCount  = 0;
static uint32_t g_psoStreamCallCount  = 0;
static uint32_t g_psoInjectedCount    = 0;
static uint32_t g_psoSkippedCount     = 0;
static uint32_t g_pipelineLibCallCount = 0;
static uint32_t g_loadGfxPipelineCallCount = 0;

// InlineHookData, InstallInlineHook, RemoveInlineHook from InlineHook.h

static InlineHookData g_hookLegacy;
static InlineHookData g_hookStream;
static InlineHookData g_hookPipelineLib;
static InlineHookData g_hookLoadGfxPipeline;

// -----------------------------------------------------------------------
// Shared: check if TEXCOORD7 already exists in input layout
// -----------------------------------------------------------------------

// Check if TEXCOORD7 exists in input layout at all
static bool HasTexcoord7(const D3D12_INPUT_ELEMENT_DESC* elements, UINT count)
{
    for (UINT i = 0; i < count; ++i)
    {
        if (elements[i].SemanticIndex == 7 &&
            elements[i].SemanticName && strcmp(elements[i].SemanticName, "TEXCOORD") == 0)
            return true;
    }
    return false;
}

// Check if TEXCOORD7 already exists AND is correctly configured
static bool IsTexcoord7Correct(const D3D12_INPUT_ELEMENT_DESC* elements, UINT count)
{
    for (UINT i = 0; i < count; ++i)
    {
        if (elements[i].SemanticIndex == 7 &&
            elements[i].SemanticName && strcmp(elements[i].SemanticName, "TEXCOORD") == 0)
        {
            return elements[i].InputSlot == kInstanceVBSlot &&
                   elements[i].InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA &&
                   elements[i].InstanceDataStepRate == 1;
        }
    }
    return false;
}

// Build input layout: copy all elements, patch existing TEXCOORD7 to per-instance on slot 15.
// Only modifies PSOs that already have TEXCOORD7 (from our MDI Mesh vertex layout).
// PSOs without TEXCOORD7 (skybox, post-processing, etc.) are left untouched.
static void BuildInjectedLayout(const D3D12_INPUT_ELEMENT_DESC* src, UINT srcCount,
                                std::vector<D3D12_INPUT_ELEMENT_DESC>& out)
{
    out.clear();

    for (UINT i = 0; i < srcCount; ++i)
    {
        if (src[i].SemanticIndex == 7 &&
            src[i].SemanticName && strcmp(src[i].SemanticName, "TEXCOORD") == 0)
        {
            // Replace existing TEXCOORD7: change to per-instance on slot 15
            D3D12_INPUT_ELEMENT_DESC patched = src[i];
            patched.InputSlot            = kInstanceVBSlot;
            patched.AlignedByteOffset    = 0;
            patched.Format               = DXGI_FORMAT_R32_UINT;
            patched.InputSlotClass       = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
            patched.InstanceDataStepRate = 1;
            out.push_back(patched);
        }
        else
        {
            out.push_back(src[i]);
        }
    }
}

// -----------------------------------------------------------------------
// Hook: CreateGraphicsPipelineState (legacy, vtable[10])
// -----------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE Hook_CreateGraphicsPipelineState(
    ID3D12Device* self,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    void** ppPipelineState)
{
    g_psoLegacyCallCount++;

    // Call original via trampoline
    auto callOrig = reinterpret_cast<PFN_CreateGraphicsPipelineState>(g_hookLegacy.trampoline);

    if (!pDesc)
        return callOrig(self, pDesc, riid, ppPipelineState);

    // Only modify PSOs that already have TEXCOORD7 (from our MDI Mesh).
    // Skip PSOs without TEXCOORD7 (skybox, post-processing, etc.)
    if (!HasTexcoord7(pDesc->InputLayout.pInputElementDescs, pDesc->InputLayout.NumElements))
    {
        g_psoSkippedCount++;
        return callOrig(self, pDesc, riid, ppPipelineState);
    }

    if (IsTexcoord7Correct(pDesc->InputLayout.pInputElementDescs, pDesc->InputLayout.NumElements))
    {
        g_psoSkippedCount++;
        return callOrig(self, pDesc, riid, ppPipelineState);
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    BuildInjectedLayout(pDesc->InputLayout.pInputElementDescs,
                        pDesc->InputLayout.NumElements, elements);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC modifiedDesc = *pDesc;
    modifiedDesc.InputLayout.pInputElementDescs = elements.data();
    modifiedDesc.InputLayout.NumElements        = static_cast<UINT>(elements.size());

    HRESULT hr = callOrig(self, &modifiedDesc, riid, ppPipelineState);

    g_psoInjectedCount++;
    if (g_psoInjectedCount <= 5)
        DebugLog("[MDI] PSO legacy hook: patched TEXCOORD7 to per-instance (slot %u), "
                 "elements=%u, hr=0x%08X\n",
                 kInstanceVBSlot, (unsigned)elements.size(), hr);

    return hr;
}

// -----------------------------------------------------------------------
// Hook: CreatePipelineState (stream-based, vtable[46])
//
// Scans the pipeline state stream for the INPUT_LAYOUT subobject,
// modifies it to include our per-instance TEXCOORD7 element.
// -----------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE Hook_CreatePipelineState(
    ID3D12Device* self,
    const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc,
    REFIID riid,
    void** ppPipelineState)
{
    g_psoStreamCallCount++;

    // Call original via trampoline
    auto callOrig = reinterpret_cast<PFN_CreatePipelineState>(g_hookStream.trampoline);

    if (!pDesc || !pDesc->pPipelineStateSubobjectStream || pDesc->SizeInBytes == 0)
        return callOrig(self, pDesc, riid, ppPipelineState);

    // Scan stream for INPUT_LAYOUT subobject (type 12).
    // Stream is a sequence of alignas(void*) subobjects.
    // Each starts with D3D12_PIPELINE_STATE_SUBOBJECT_TYPE (uint32).
    // For INPUT_LAYOUT, the D3D12_INPUT_LAYOUT_DESC follows at offset sizeof(void*)
    // (because it contains a pointer → 8-byte aligned on x64).
    auto* stream = static_cast<const uint8_t*>(pDesc->pPipelineStateSubobjectStream);
    size_t streamSize = pDesc->SizeInBytes;
    constexpr size_t kAlign = sizeof(void*);  // 8 on x64

    size_t layoutOffset = SIZE_MAX;

    for (size_t off = 0; off + kAlign + sizeof(D3D12_INPUT_LAYOUT_DESC) <= streamSize; off += kAlign)
    {
        auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(stream + off);
        if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
        {
            auto* layout = reinterpret_cast<const D3D12_INPUT_LAYOUT_DESC*>(stream + off + kAlign);
            if (layout->NumElements < 64)
            {
                layoutOffset = off;
                break;
            }
        }
    }

    if (layoutOffset == SIZE_MAX)
    {
        // No INPUT_LAYOUT found — might be compute PSO or mesh shader
        if (g_psoStreamCallCount <= 3)
            DebugLog("[MDI] PSO stream hook (#%u): no INPUT_LAYOUT found, streamSize=%zu\n",
                     g_psoStreamCallCount, streamSize);
        return callOrig(self, pDesc, riid, ppPipelineState);
    }

    auto* origLayout = reinterpret_cast<const D3D12_INPUT_LAYOUT_DESC*>(
        stream + layoutOffset + kAlign);

    if (g_psoStreamCallCount <= 5)
    {
        DebugLog("[MDI] PSO stream hook (#%u): found INPUT_LAYOUT at offset %zu, "
                 "NumElements=%u\n",
                 g_psoStreamCallCount, layoutOffset, origLayout->NumElements);
        for (UINT i = 0; i < origLayout->NumElements && i < 16; ++i)
        {
            const auto& e = origLayout->pInputElementDescs[i];
            DebugLog("[MDI]   [%u] %s%u slot=%u fmt=%u class=%d stepRate=%u\n",
                     i, e.SemanticName ? e.SemanticName : "?", e.SemanticIndex,
                     e.InputSlot, e.Format, e.InputSlotClass, e.InstanceDataStepRate);
        }
    }

    // Only modify PSOs that already have TEXCOORD7 (from our MDI Mesh)
    if (!HasTexcoord7(origLayout->pInputElementDescs, origLayout->NumElements))
    {
        g_psoSkippedCount++;
        return callOrig(self, pDesc, riid, ppPipelineState);
    }

    if (IsTexcoord7Correct(origLayout->pInputElementDescs, origLayout->NumElements))
    {
        g_psoSkippedCount++;
        return callOrig(self, pDesc, riid, ppPipelineState);
    }

    // Build patched layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    BuildInjectedLayout(origLayout->pInputElementDescs, origLayout->NumElements, elements);

    // Copy stream and patch the INPUT_LAYOUT in the copy
    std::vector<uint8_t> modifiedStream(stream, stream + streamSize);
    auto* patchedLayout = reinterpret_cast<D3D12_INPUT_LAYOUT_DESC*>(
        modifiedStream.data() + layoutOffset + kAlign);
    patchedLayout->pInputElementDescs = elements.data();
    patchedLayout->NumElements        = static_cast<UINT>(elements.size());

    D3D12_PIPELINE_STATE_STREAM_DESC modifiedDesc;
    modifiedDesc.SizeInBytes                   = pDesc->SizeInBytes;
    modifiedDesc.pPipelineStateSubobjectStream = modifiedStream.data();

    HRESULT hr = callOrig(self, &modifiedDesc, riid, ppPipelineState);

    g_psoInjectedCount++;
    if (g_psoInjectedCount <= 5)
        DebugLog("[MDI] PSO stream hook: patched TEXCOORD7 to per-instance (slot %u), "
                 "elements=%u, hr=0x%08X\n",
                 kInstanceVBSlot, (unsigned)elements.size(), hr);

    return hr;
}

// -----------------------------------------------------------------------
// Hook: ID3D12PipelineLibrary::LoadGraphicsPipeline (vtable[9])
// Unity may use pipeline libraries to cache PSOs, bypassing Create*PSO.
// -----------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE Hook_LoadGraphicsPipeline(
    void* self,   // ID3D12PipelineLibrary*
    LPCWSTR pName,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    void** ppPipelineState)
{
    g_loadGfxPipelineCallCount++;

    auto callOrig = reinterpret_cast<PFN_LoadGraphicsPipeline>(g_hookLoadGfxPipeline.trampoline);

    if (!pDesc)
        return callOrig(self, pName, pDesc, riid, ppPipelineState);

    // Only modify PSOs that already have TEXCOORD7 (from our MDI Mesh)
    if (!HasTexcoord7(pDesc->InputLayout.pInputElementDescs, pDesc->InputLayout.NumElements))
    {
        g_psoSkippedCount++;
        return callOrig(self, pName, pDesc, riid, ppPipelineState);
    }

    if (IsTexcoord7Correct(pDesc->InputLayout.pInputElementDescs, pDesc->InputLayout.NumElements))
    {
        g_psoSkippedCount++;
        return callOrig(self, pName, pDesc, riid, ppPipelineState);
    }

    // Patch TEXCOORD7 in the layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    BuildInjectedLayout(pDesc->InputLayout.pInputElementDescs,
                        pDesc->InputLayout.NumElements, elements);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC modifiedDesc = *pDesc;
    modifiedDesc.InputLayout.pInputElementDescs = elements.data();
    modifiedDesc.InputLayout.NumElements        = static_cast<UINT>(elements.size());

    // Try loading with modified desc — will likely miss cache (different layout).
    // On cache miss, falls through to CreateGraphicsPipelineState which our hook also catches.
    HRESULT hr = callOrig(self, pName, &modifiedDesc, riid, ppPipelineState);

    if (FAILED(hr))
    {
        // Cache miss — fallback: the caller (Unity) will call CreateGraphicsPipelineState,
        // where our other hook injects TEXCOORD7.
        if (g_loadGfxPipelineCallCount <= 5)
            DebugLog("[MDI] LoadGraphicsPipeline cache miss (modified desc), hr=0x%08X\n", hr);
        return callOrig(self, pName, pDesc, riid, ppPipelineState);
    }

    g_psoInjectedCount++;
    if (g_psoInjectedCount <= 5)
        DebugLog("[MDI] LoadGraphicsPipeline: injected TEXCOORD7, elements=%u, hr=0x%08X\n",
                 (unsigned)elements.size(), hr);
    return hr;
}

// -----------------------------------------------------------------------
// Hook: ID3D12Device1::CreatePipelineLibrary (vtable[44])
// Intercept to hook LoadGraphicsPipeline on the returned library object.
// -----------------------------------------------------------------------

static HRESULT STDMETHODCALLTYPE Hook_CreatePipelineLibrary(
    ID3D12Device* self,
    const void* pLibraryBlob,
    SIZE_T blobLength,
    REFIID riid,
    void** ppPipelineLibrary)
{
    g_pipelineLibCallCount++;

    auto callOrig = reinterpret_cast<PFN_CreatePipelineLibrary>(g_hookPipelineLib.trampoline);
    HRESULT hr = callOrig(self, pLibraryBlob, blobLength, riid, ppPipelineLibrary);

    if (SUCCEEDED(hr) && ppPipelineLibrary && *ppPipelineLibrary)
    {
        DebugLog("[MDI] CreatePipelineLibrary succeeded (#%u), library=%p, blobLen=%zu\n",
                 g_pipelineLibCallCount, *ppPipelineLibrary, blobLength);

        // Hook LoadGraphicsPipeline on the returned library object
        if (!g_hookLoadGfxPipeline.target)
        {
            void** libVtable = *reinterpret_cast<void***>(*ppPipelineLibrary);
            void* fnLoadGfx = libVtable[9];  // LoadGraphicsPipeline
            DebugLog("[MDI] PipelineLibrary vtable=%p, LoadGraphicsPipeline=%p\n",
                     libVtable, fnLoadGfx);

            bool hooked = InstallInlineHook(
                fnLoadGfx,
                reinterpret_cast<void*>(&Hook_LoadGraphicsPipeline),
                g_hookLoadGfxPipeline);
            DebugLog("[MDI] LoadGraphicsPipeline inline hook: %d\n", hooked);
        }
    }
    else if (g_pipelineLibCallCount <= 3)
    {
        DebugLog("[MDI] CreatePipelineLibrary failed (#%u), hr=0x%08X\n",
                 g_pipelineLibCallCount, hr);
    }

    return hr;
}

// -----------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------

bool MDIBackend_D3D12::Initialize(IUnityInterfaces* unityInterfaces)
{
    _d3d12 = unityInterfaces->Get<IUnityGraphicsD3D12v7>();
    if (!_d3d12)
    {
        DebugLog("[MDI] D3D12 v7 interface not available\n");
        return false;
    }

    _device = _d3d12->GetDevice();
    if (!_device) return false;

    // Basic command signature: DrawIndexed only (no root signature needed)
    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
    sigDesc.ByteStride       = 20;
    sigDesc.NumArgumentDescs = 1;
    sigDesc.pArgumentDescs   = &argDesc;
    sigDesc.NodeMask         = 0;

    HRESULT hr = _device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&_cmdSignature));
    if (FAILED(hr))
    {
        DebugLog("[MDI] CreateCommandSignature failed: 0x%08X\n", hr);
        return false;
    }

    InstallDeviceHook();
    CreateInstanceIDBuffer();

    _initialized = true;
    DebugLog("[MDI] D3D12 backend initialized (PSO hooks + per-instance VB)\n");
    return true;
}

void MDIBackend_D3D12::InstallDeviceHook()
{
    if (g_deviceHooked || !_device) return;

    // Get function pointers from vtable (Unity caches these, so vtable
    // patching alone doesn't work — we need inline hooks on the actual code)
    void** vtable = *reinterpret_cast<void***>(_device);
    auto fnLegacy = reinterpret_cast<void*>(vtable[10]);  // CreateGraphicsPipelineState
    auto fnStream = reinterpret_cast<void*>(vtable[47]);  // ID3D12Device2::CreatePipelineState

    DebugLog("[MDI] Device %p, vtable %p\n", _device, vtable);
    DebugLog("[MDI] CreateGraphicsPipelineState = %p\n", fnLegacy);
    DebugLog("[MDI] CreatePipelineState = %p\n", fnStream);

    // Also hook CreatePipelineLibrary (vtable[44]) to intercept PSO caching
    auto fnPipelineLib = reinterpret_cast<void*>(vtable[44]);  // ID3D12Device1::CreatePipelineLibrary
    DebugLog("[MDI] CreatePipelineLibrary = %p\n", fnPipelineLib);

    // Install inline hooks (patch actual function code)
    bool hookedLegacy = InstallInlineHook(
        fnLegacy,
        reinterpret_cast<void*>(&Hook_CreateGraphicsPipelineState),
        g_hookLegacy);
    g_origCreateGraphicsPSO = reinterpret_cast<PFN_CreateGraphicsPipelineState>(
        g_hookLegacy.trampoline);

    bool hookedStream = InstallInlineHook(
        fnStream,
        reinterpret_cast<void*>(&Hook_CreatePipelineState),
        g_hookStream);
    g_origCreatePipelineState = reinterpret_cast<PFN_CreatePipelineState>(
        g_hookStream.trampoline);

    bool hookedPipelineLib = InstallInlineHook(
        fnPipelineLib,
        reinterpret_cast<void*>(&Hook_CreatePipelineLibrary),
        g_hookPipelineLib);

    g_deviceHooked = true;
    DebugLog("[MDI] Inline hooks: legacy=%d, stream=%d, pipelineLib=%d\n",
             hookedLegacy, hookedStream, hookedPipelineLib);
}

void MDIBackend_D3D12::CreateInstanceIDBuffer()
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    uint64_t bufferSize = static_cast<uint64_t>(_maxInstanceCount) * sizeof(uint32_t);

    D3D12_RESOURCE_DESC resDesc = {};
    resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Width            = bufferSize;
    resDesc.Height           = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels        = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = _device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&_instanceIDBuffer));

    if (FAILED(hr))
    {
        DebugLog("[MDI] Failed to create instance ID buffer: 0x%08X\n", hr);
        return;
    }

    void* mapped = nullptr;
    hr = _instanceIDBuffer->Map(0, nullptr, &mapped);
    if (SUCCEEDED(hr))
    {
        auto* data = static_cast<uint32_t*>(mapped);
        for (uint32_t i = 0; i < _maxInstanceCount; ++i)
            data[i] = i;
        _instanceIDBuffer->Unmap(0, nullptr);
        DebugLog("[MDI] Instance ID buffer ready: %u entries, %llu bytes\n",
                 _maxInstanceCount, static_cast<unsigned long long>(bufferSize));
    }
}

bool MDIBackend_D3D12::ResizeInstanceIDBuffer(uint32_t newMaxCount)
{
    if (newMaxCount == 0) return false;
    if (newMaxCount == _maxInstanceCount && _instanceIDBuffer) return true;

    if (_instanceIDBuffer) { _instanceIDBuffer->Release(); _instanceIDBuffer = nullptr; }

    _maxInstanceCount = newMaxCount;
    CreateInstanceIDBuffer();
    return _instanceIDBuffer != nullptr;
}

// -----------------------------------------------------------------------
// ConfigureEvents
// -----------------------------------------------------------------------

void MDIBackend_D3D12::ConfigureEvents(IUnityInterfaces* unityInterfaces, int baseEventID, int count)
{
    auto* d3d12 = unityInterfaces->Get<IUnityGraphicsD3D12v7>();
    if (!d3d12) return;

    for (int i = 0; i < count; ++i)
    {
        UnityD3D12PluginEventConfig config = {};
        config.graphicsQueueAccess = kUnityD3D12GraphicsQueueAccess_DontCare;
        config.flags = kUnityD3D12EventConfigFlag_ModifiesCommandBuffersState;
        config.ensureActiveRenderTextureIsBound = false;
        d3d12->ConfigureEvent(baseEventID + i, &config);
    }

    DebugLog("[MDI] Configured D3D12 events [%d .. %d)\n", baseEventID, baseEventID + count);
}

// -----------------------------------------------------------------------
// Shutdown
// -----------------------------------------------------------------------

void MDIBackend_D3D12::Shutdown()
{
    if (g_deviceHooked)
    {
        RemoveInlineHook(g_hookLegacy);
        RemoveInlineHook(g_hookStream);
        RemoveInlineHook(g_hookPipelineLib);
        RemoveInlineHook(g_hookLoadGfxPipeline);
        g_origCreateGraphicsPSO = nullptr;
        g_origCreatePipelineState = nullptr;
        g_deviceHooked = false;
    }

    if (_instanceIDBuffer) { _instanceIDBuffer->Release(); _instanceIDBuffer = nullptr; }
    if (_cmdSignature) { _cmdSignature->Release(); _cmdSignature = nullptr; }

    _device      = nullptr;
    _d3d12       = nullptr;
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

    UnityGraphicsD3D12RecordingState recordingState = {};
    if (!_d3d12->CommandRecordingState(&recordingState))
        return;

    ID3D12GraphicsCommandList* cmdList = recordingState.commandList;
    if (!cmdList) return;

    auto* argsResource = static_cast<ID3D12Resource*>(params.argsBuffer);

    // Rebind caller's index buffer (prime DrawMesh sets its own 3-index IB)
    if (params.indexBuffer)
    {
        auto* indexResource = static_cast<ID3D12Resource*>(params.indexBuffer);
        D3D12_INDEX_BUFFER_VIEW ibView = {};
        ibView.BufferLocation = indexResource->GetGPUVirtualAddress();
        ibView.SizeInBytes    = static_cast<UINT>(indexResource->GetDesc().Width);
        ibView.Format         = (params.indexFormat == 0) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        cmdList->IASetIndexBuffer(&ibView);
    }

    // Bind per-instance identity VB to slot 15.
    if (_instanceIDBuffer)
    {
        D3D12_VERTEX_BUFFER_VIEW vbView = {};
        vbView.BufferLocation = _instanceIDBuffer->GetGPUVirtualAddress();
        vbView.SizeInBytes    = _maxInstanceCount * sizeof(uint32_t);
        vbView.StrideInBytes  = sizeof(uint32_t);
        cmdList->IASetVertexBuffers(kInstanceVBSlot, 1, &vbView);
    }

    // Set triangle list topology (prime DrawMesh may have set a different one)
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Single ExecuteIndirect — true multi-draw indirect
    cmdList->ExecuteIndirect(
        _cmdSignature,
        params.maxDrawCount,
        argsResource,
        params.argsOffsetBytes,
        nullptr, 0);

    static uint32_t s_callCount = 0;
    s_callCount++;

    // Log first call, then at frames 10, 100, and every 1000th
    if (s_callCount == 1 || s_callCount == 10 || s_callCount == 100 ||
        (s_callCount % 1000) == 0)
    {
        DebugLog("[MDI] ExecuteMDI #%u: drawCount=%u, offset=%u\n",
                 s_callCount, params.maxDrawCount, params.argsOffsetBytes);
        DebugLog("[MDI] Hook stats: legacy=%u, stream=%u, pipelineLib=%u, "
                 "loadGfx=%u, injected=%u, skipped=%u\n",
                 g_psoLegacyCallCount, g_psoStreamCallCount,
                 g_pipelineLibCallCount, g_loadGfxPipelineCallCount,
                 g_psoInjectedCount, g_psoSkippedCount);
    }
}

bool MDIBackend_D3D12::IsSupported() const
{
    return _initialized && _cmdSignature != nullptr;
}

#endif // _WIN32
