#include "MDIBackend_D3D11.h"

#ifdef _WIN32

#include <vector>
#include <cstring>
#include "MDILog.h"
#include "InlineHook.h"

// -----------------------------------------------------------------------
// Input layout hook: patch TEXCOORD7 to per-instance on slot 15
// -----------------------------------------------------------------------

static constexpr uint32_t kInstanceVBSlot_D3D11 = 15;

using PFN_CreateInputLayout = HRESULT(STDMETHODCALLTYPE*)(
    ID3D11Device*, const D3D11_INPUT_ELEMENT_DESC*, UINT,
    const void*, SIZE_T, ID3D11InputLayout**);

static bool g_d3d11DeviceHooked = false;
static InlineHookData g_hookInputLayout;
static uint32_t g_ilCallCount    = 0;
static uint32_t g_ilInjectedCount = 0;
static uint32_t g_ilSkippedCount  = 0;

static bool HasTexcoord7_D3D11(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count)
{
    for (UINT i = 0; i < count; ++i)
    {
        if (elements[i].SemanticIndex == 7 &&
            elements[i].SemanticName && strcmp(elements[i].SemanticName, "TEXCOORD") == 0)
            return true;
    }
    return false;
}

static bool IsTexcoord7Correct_D3D11(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count)
{
    for (UINT i = 0; i < count; ++i)
    {
        if (elements[i].SemanticIndex == 7 &&
            elements[i].SemanticName && strcmp(elements[i].SemanticName, "TEXCOORD") == 0)
        {
            return elements[i].InputSlot == kInstanceVBSlot_D3D11 &&
                   elements[i].InputSlotClass == D3D11_INPUT_PER_INSTANCE_DATA &&
                   elements[i].InstanceDataStepRate == 1;
        }
    }
    return false;
}

static HRESULT STDMETHODCALLTYPE Hook_CreateInputLayout(
    ID3D11Device* self,
    const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs,
    UINT NumElements,
    const void* pShaderBytecodeWithInputSignature,
    SIZE_T BytecodeLength,
    ID3D11InputLayout** ppInputLayout)
{
    g_ilCallCount++;

    auto callOrig = reinterpret_cast<PFN_CreateInputLayout>(g_hookInputLayout.trampoline);

    if (!pInputElementDescs || NumElements == 0)
        return callOrig(self, pInputElementDescs, NumElements,
                        pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);

    // Only modify layouts that already have TEXCOORD7 (from our MDI Mesh)
    if (!HasTexcoord7_D3D11(pInputElementDescs, NumElements))
    {
        g_ilSkippedCount++;
        return callOrig(self, pInputElementDescs, NumElements,
                        pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
    }

    if (IsTexcoord7Correct_D3D11(pInputElementDescs, NumElements))
    {
        g_ilSkippedCount++;
        return callOrig(self, pInputElementDescs, NumElements,
                        pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
    }

    // Patch TEXCOORD7 to per-instance on slot 15
    std::vector<D3D11_INPUT_ELEMENT_DESC> patched(pInputElementDescs, pInputElementDescs + NumElements);
    for (auto& e : patched)
    {
        if (e.SemanticIndex == 7 && e.SemanticName && strcmp(e.SemanticName, "TEXCOORD") == 0)
        {
            e.InputSlot            = kInstanceVBSlot_D3D11;
            e.AlignedByteOffset    = 0;
            e.Format               = DXGI_FORMAT_R32_UINT;
            e.InputSlotClass       = D3D11_INPUT_PER_INSTANCE_DATA;
            e.InstanceDataStepRate = 1;
        }
    }

    HRESULT hr = callOrig(self, patched.data(), static_cast<UINT>(patched.size()),
                          pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);

    g_ilInjectedCount++;
    if (g_ilInjectedCount <= 5)
        DebugLog("[MDI] D3D11 InputLayout hook: patched TEXCOORD7 to per-instance (slot %u), "
                 "elements=%u, hr=0x%08X\n",
                 kInstanceVBSlot_D3D11, NumElements, hr);

    return hr;
}

// -----------------------------------------------------------------------
// NvAPI
// -----------------------------------------------------------------------

static constexpr unsigned int NVAPI_ID_Initialize                          = 0x0150e828;
static constexpr unsigned int NVAPI_ID_Unload                              = 0xd22bdd7e;
static constexpr unsigned int NVAPI_ID_D3D_RegisterDevice                  = 0x8c02c4d0;
static constexpr unsigned int NVAPI_ID_D3D11_MultiDrawIndexedInstancedIndirect = 0x59e890f9;

using NvAPI_QueryInterface_t = void* (*)(unsigned int id);

static bool IsRenderDocPresent()
{
    return GetModuleHandleA("renderdoc.dll") != nullptr;
}

bool MDIBackend_D3D11::TryInitNvAPI()
{
    _nvApiModule = LoadLibraryA("nvapi64.dll");
    if (!_nvApiModule) return false;

    auto queryInterface = reinterpret_cast<NvAPI_QueryInterface_t>(
        GetProcAddress(_nvApiModule, "nvapi_QueryInterface"));
    if (!queryInterface)
    {
        FreeLibrary(_nvApiModule); _nvApiModule = nullptr;
        return false;
    }

    auto nvInit     = reinterpret_cast<NvAPI_Initialize_t>(queryInterface(NVAPI_ID_Initialize));
    auto nvRegister = reinterpret_cast<NvAPI_D3D_RegisterDevice_t>(queryInterface(NVAPI_ID_D3D_RegisterDevice));
    _nvApiUnload    = reinterpret_cast<NvAPI_Unload_t>(queryInterface(NVAPI_ID_Unload));
    _nvApiMDI       = reinterpret_cast<NvAPI_D3D11_MultiDrawIndexedInstancedIndirect_t>(
                          queryInterface(NVAPI_ID_D3D11_MultiDrawIndexedInstancedIndirect));

    if (!nvInit || !nvRegister || !_nvApiUnload || !_nvApiMDI)
    {
        FreeLibrary(_nvApiModule); _nvApiModule = nullptr; _nvApiMDI = nullptr;
        return false;
    }
    if (nvInit() != 0)
    {
        FreeLibrary(_nvApiModule); _nvApiModule = nullptr; _nvApiMDI = nullptr;
        return false;
    }
    if (nvRegister(_device) != 0)
    {
        _nvApiUnload();
        FreeLibrary(_nvApiModule); _nvApiModule = nullptr; _nvApiMDI = nullptr;
        return false;
    }
    return true;
}

void MDIBackend_D3D11::ShutdownNvAPI()
{
    if (_nvApiUnload) _nvApiUnload();
    if (_nvApiModule) { FreeLibrary(_nvApiModule); _nvApiModule = nullptr; }
    _nvApiMDI = nullptr; _nvApiUnload = nullptr;
    _nvApiReady = false; _nvApiAttempted = false;
}

void MDIBackend_D3D11::EnsureNvAPIInitialized()
{
    if (_nvApiAttempted) return;
    _nvApiAttempted = true;

    if (IsRenderDocPresent())
    {
        DebugLog("[MDI] RenderDoc detected — skipping NvAPI\n");
        _nvApiReady = false;
        return;
    }

    _nvApiReady = TryInitNvAPI();
    DebugLog("[MDI] Lazy NvAPI init: %s\n", _nvApiReady ? "OK" : "failed/skipped");
}

// -----------------------------------------------------------------------
// Initialize / Shutdown
// -----------------------------------------------------------------------

bool MDIBackend_D3D11::Initialize(IUnityInterfaces* unityInterfaces)
{
    auto* d3d11 = unityInterfaces->Get<IUnityGraphicsD3D11>();
    if (!d3d11) return false;

    _device = d3d11->GetDevice();
    if (!_device) return false;

    _device->GetImmediateContext(&_context);
    if (!_context) return false;

    _context->QueryInterface(__uuidof(ID3DUserDefinedAnnotation),
        reinterpret_cast<void**>(&_annotation));

    _nvApiReady = false;
    _nvApiAttempted = false;

    InstallDeviceHook();
    CreateInstanceIDBuffer();

    DebugLog("[MDI] D3D11 backend initialized (InputLayout hook + per-instance VB + NvAPI deferred)\n");
    _initialized = true;
    return true;
}

void MDIBackend_D3D11::InstallDeviceHook()
{
    if (g_d3d11DeviceHooked || !_device) return;

    // ID3D11Device vtable[11] = CreateInputLayout
    void** vtable = *reinterpret_cast<void***>(_device);
    auto fnCreateIL = reinterpret_cast<void*>(vtable[11]);

    DebugLog("[MDI] D3D11 Device %p, vtable %p\n", _device, vtable);
    DebugLog("[MDI] CreateInputLayout = %p\n", fnCreateIL);

    bool hooked = InstallInlineHook(
        fnCreateIL,
        reinterpret_cast<void*>(&Hook_CreateInputLayout),
        g_hookInputLayout);

    g_d3d11DeviceHooked = true;
    DebugLog("[MDI] D3D11 InputLayout inline hook: %d\n", hooked);
}

void MDIBackend_D3D11::CreateInstanceIDBuffer()
{
    std::vector<uint32_t> data(_maxInstanceCount);
    for (uint32_t i = 0; i < _maxInstanceCount; ++i)
        data[i] = i;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth  = _maxInstanceCount * sizeof(uint32_t);
    desc.Usage      = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags  = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data.data();

    HRESULT hr = _device->CreateBuffer(&desc, &initData, &_instanceIDBuffer);
    if (FAILED(hr))
        DebugLog("[MDI] Failed to create D3D11 instance ID buffer: 0x%08X\n", hr);
    else
        DebugLog("[MDI] D3D11 Instance ID buffer ready: %u entries, %u bytes\n",
                 _maxInstanceCount, _maxInstanceCount * (uint32_t)sizeof(uint32_t));
}

bool MDIBackend_D3D11::ResizeInstanceIDBuffer(uint32_t newMaxCount)
{
    if (newMaxCount == 0) return false;
    if (newMaxCount == _maxInstanceCount && _instanceIDBuffer) return true;

    if (_instanceIDBuffer) { _instanceIDBuffer->Release(); _instanceIDBuffer = nullptr; }

    _maxInstanceCount = newMaxCount;
    CreateInstanceIDBuffer();
    return _instanceIDBuffer != nullptr;
}

void MDIBackend_D3D11::Shutdown()
{
    if (_nvApiReady) ShutdownNvAPI();

    if (g_d3d11DeviceHooked)
    {
        RemoveInlineHook(g_hookInputLayout);
        g_d3d11DeviceHooked = false;
    }

    if (_instanceIDBuffer) { _instanceIDBuffer->Release(); _instanceIDBuffer = nullptr; }
    if (_annotation) { _annotation->Release(); _annotation = nullptr; }
    if (_context) { _context->Release(); _context = nullptr; }
    _device = nullptr;
    _initialized = false;
}

// -----------------------------------------------------------------------
// ExecuteMDI
// -----------------------------------------------------------------------

void MDIBackend_D3D11::ExecuteMDI(const MDIParams& params)
{
    if (!_initialized || !_context || !params.argsBuffer || params.maxDrawCount == 0)
        return;

    EnsureNvAPIInitialized();

    auto* argsBuffer = static_cast<ID3D11Buffer*>(params.argsBuffer);
    constexpr uint32_t stride = 20;

    // Rebind caller's index buffer (prime DrawMesh sets its own 3-index IB)
    if (params.indexBuffer)
    {
        auto* ib = static_cast<ID3D11Buffer*>(params.indexBuffer);
        DXGI_FORMAT fmt = (params.indexFormat == 1) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        _context->IASetIndexBuffer(ib, fmt, 0);
    }

    // Bind per-instance identity VB to slot 15
    if (_instanceIDBuffer)
    {
        UINT vbStride = sizeof(uint32_t);
        UINT vbOffset = 0;
        _context->IASetVertexBuffers(kInstanceVBSlot, 1, &_instanceIDBuffer, &vbStride, &vbOffset);
    }

    // Set triangle list topology
    _context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Debug marker
    if (_annotation)
        _annotation->BeginEvent(
            (_nvApiReady && _nvApiMDI)
                ? L"MDI::NvAPI [Native Hardware MDI + PerInstance VB]"
                : L"MDI::NativeLoop [Plugin Loop + PerInstance VB]");

    if (_nvApiReady && _nvApiMDI)
    {
        // NvAPI hardware MDI — single call
        _nvApiMDI(
            _context,
            params.maxDrawCount,
            argsBuffer,
            params.argsOffsetBytes,
            stride
        );
    }
    else
    {
        // Fallback: CPU loop of DrawIndexedInstancedIndirect
        for (uint32_t i = 0; i < params.maxDrawCount; ++i)
        {
            _context->DrawIndexedInstancedIndirect(
                argsBuffer,
                params.argsOffsetBytes + i * stride
            );
        }
    }

    if (_annotation)
        _annotation->EndEvent();

    static uint32_t s_callCount = 0;
    s_callCount++;
    if (s_callCount == 1 || s_callCount == 10 || s_callCount == 100 ||
        (s_callCount % 1000) == 0)
    {
        DebugLog("[MDI] D3D11 ExecuteMDI #%u: drawCount=%u, offset=%u, nvapi=%d\n",
                 s_callCount, params.maxDrawCount, params.argsOffsetBytes, _nvApiReady);
        DebugLog("[MDI] D3D11 IL hook stats: calls=%u, injected=%u, skipped=%u\n",
                 g_ilCallCount, g_ilInjectedCount, g_ilSkippedCount);
    }
}

bool MDIBackend_D3D11::IsSupported() const
{
    return _initialized;
}

#endif // _WIN32
