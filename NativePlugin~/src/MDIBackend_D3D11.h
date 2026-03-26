#pragma once

#include "MDIBackend.h"

#ifdef _WIN32
#include <d3d11_1.h>
#include <windows.h>
#include "Unity/IUnityGraphicsD3D11.h"

class MDIBackend_D3D11 final : public IMDIBackend
{
public:
    bool Initialize(IUnityInterfaces* unityInterfaces) override;
    void Shutdown() override;
    void ExecuteMDI(const MDIParams& params) override;
    bool IsSupported() const override;

private:
    bool TryInitNvAPI();
    void ShutdownNvAPI();
    void EnsureNvAPIInitialized();

    ID3D11Device*              _device      = nullptr;
    ID3D11DeviceContext*       _context     = nullptr;
    ID3DUserDefinedAnnotation* _annotation  = nullptr;
    bool                       _initialized = false;

    // NvAPI dynamic loading — lazy init
    HMODULE _nvApiModule = nullptr;
    bool    _nvApiReady  = false;
    bool    _nvApiAttempted = false;

    using NvAPI_Initialize_t                          = int (*)();
    using NvAPI_Unload_t                              = int (*)();
    using NvAPI_D3D_RegisterDevice_t                  = int (*)(IUnknown*);
    using NvAPI_D3D11_MultiDrawIndexedInstancedIndirect_t = int (*)(ID3D11DeviceContext*, unsigned int, ID3D11Buffer*, unsigned int, unsigned int);

    NvAPI_Unload_t                                    _nvApiUnload    = nullptr;
    NvAPI_D3D11_MultiDrawIndexedInstancedIndirect_t   _nvApiMDI       = nullptr;
};

#endif // _WIN32
