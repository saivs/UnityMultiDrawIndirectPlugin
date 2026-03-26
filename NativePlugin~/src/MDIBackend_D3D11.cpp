#include "MDIBackend_D3D11.h"

#ifdef _WIN32

#include <stdio.h>

// NvAPI function IDs from nvapi_interface.h — used with nvapi_QueryInterface
static constexpr unsigned int NVAPI_ID_Initialize                          = 0x0150e828;
static constexpr unsigned int NVAPI_ID_Unload                              = 0xd22bdd7e;
static constexpr unsigned int NVAPI_ID_D3D_RegisterDevice                  = 0x8c02c4d0;
static constexpr unsigned int NVAPI_ID_D3D11_MultiDrawIndexedInstancedIndirect = 0x59e890f9;

using NvAPI_QueryInterface_t = void* (*)(unsigned int id);

static void DebugLog(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    OutputDebugStringA(buf);
}

static bool IsRenderDocPresent()
{
    return GetModuleHandleA("renderdoc.dll") != nullptr;
}

// -----------------------------------------------------------------------
// NvAPI
// -----------------------------------------------------------------------

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

    DebugLog("[MDI] D3D11 backend initialized (NvAPI deferred)\n");
    _initialized = true;
    return true;
}

void MDIBackend_D3D11::Shutdown()
{
    if (_nvApiReady) ShutdownNvAPI();

    if (_annotation) { _annotation->Release(); _annotation = nullptr; }
    if (_context) { _context->Release(); _context = nullptr; }
    _device = nullptr;
    _initialized = false;
}

// -----------------------------------------------------------------------
// ExecuteMDI
// -----------------------------------------------------------------------

// Pipeline state (shaders, CBs, SRVs, samplers, render targets, topology)
// is already set by the preceding DrawProceduralIndirect in the same
// CommandBuffer. We only need to rebind the index buffer and issue draws.

void MDIBackend_D3D11::ExecuteMDI(const MDIParams& params)
{
    if (!_initialized || !_context || !params.argsBuffer || params.maxDrawCount == 0)
        return;

    EnsureNvAPIInitialized();

    auto* argsBuffer = static_cast<ID3D11Buffer*>(params.argsBuffer);
    constexpr uint32_t stride = 20;

    // Rebind index buffer — DrawProceduralIndirect may use an internal one
    if (params.indexBuffer)
    {
        auto* ib = static_cast<ID3D11Buffer*>(params.indexBuffer);
        DXGI_FORMAT fmt = (params.indexFormat == 1) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        _context->IASetIndexBuffer(ib, fmt, 0);
    }

    // Debug marker for RenderDoc / Nsight
    if (_annotation)
        _annotation->BeginEvent((_nvApiReady && _nvApiMDI)
            ? L"MDI::NvAPI [Native Hardware MDI]"
            : L"MDI::NativeLoop [Plugin DrawIndexedInstancedIndirect]");

    if (_nvApiReady && _nvApiMDI)
    {
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
}

bool MDIBackend_D3D11::IsSupported() const
{
    return _initialized;
}

#endif // _WIN32
