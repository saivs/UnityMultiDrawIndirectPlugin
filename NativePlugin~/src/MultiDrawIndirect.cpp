// GfxPlugin Multi-Draw Indirect
// DLL must be named GfxPlugin*.dll for Unity to call UnityRenderingExtEvent.
#include "Unity/IUnityInterface.h"
#include "Unity/IUnityGraphics.h"
#include "Unity/IUnityRenderingExtensions.h"
#include "MDIBackend.h"
#include "MDIBackend_Stub.h"

#ifdef _WIN32
#include "MDIBackend_D3D12.h"
#include "MDIBackend_D3D11.h"
#endif
#include "MDIBackend_Vulkan.h"
#include "MDIBackend_GLES.h"

static IUnityInterfaces* g_unityInterfaces = nullptr;
static IUnityGraphics*   g_graphics        = nullptr;
static IMDIBackend*      g_backend         = nullptr;
static MDIBackend_Stub   g_stubBackend;
static int               g_backendSupported = 0;

// Pending params ring buffer — shared by D3D11 and D3D12 paths
static MDIParams g_pending[MDI_MAX_PENDING] = {};
static volatile int g_pendingCounter = 0;

// Reserved event ID range — prevents clashes with Unity internals and other plugins
static int g_baseEventID = 0;

static IMDIBackend* CreateBackend(UnityGfxRenderer renderer)
{
    switch (renderer)
    {
#ifdef _WIN32
    case kUnityGfxRendererD3D12:
    {
        auto* backend = new MDIBackend_D3D12();
        if (backend->Initialize(g_unityInterfaces))
            return backend;
        delete backend;
        return &g_stubBackend;
    }
    case kUnityGfxRendererD3D11:
    {
        auto* backend = new MDIBackend_D3D11();
        if (backend->Initialize(g_unityInterfaces))
            return backend;
        delete backend;
        return &g_stubBackend;
    }
#endif
    case kUnityGfxRendererVulkan:
    {
        auto* backend = new MDIBackend_Vulkan();
        if (backend->Initialize(g_unityInterfaces))
            return backend;
        delete backend;
        return &g_stubBackend;
    }
    case kUnityGfxRendererOpenGLES30:
    case kUnityGfxRendererOpenGLCore:
    {
        auto* backend = new MDIBackend_GLES();
        if (backend->Initialize(g_unityInterfaces))
            return backend;
        delete backend;
        return &g_stubBackend;
    }
    default:
        return &g_stubBackend;
    }
}

static void DestroyBackend()
{
    if (g_backend && g_backend != &g_stubBackend)
    {
        g_backend->Shutdown();
        delete g_backend;
    }
    g_backend = &g_stubBackend;
}

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
    if (eventType == kUnityGfxDeviceEventInitialize)
    {
        DestroyBackend();
        g_backend = CreateBackend(g_graphics->GetRenderer());
        g_backendSupported = (g_backend && g_backend != &g_stubBackend && g_backend->IsSupported()) ? 1 : 0;
    }
    else if (eventType == kUnityGfxDeviceEventShutdown)
    {
        DestroyBackend();
        g_backendSupported = 0;
    }
}

// -----------------------------------------------------------------------
// Unity Plugin Load / Unload
// -----------------------------------------------------------------------

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    g_unityInterfaces = unityInterfaces;
    g_graphics = unityInterfaces->Get<IUnityGraphics>();

    // Reserve unique event IDs to avoid clashes with other plugins
    g_baseEventID = g_graphics->ReserveEventIDRange(MDI_MAX_PENDING);

#ifdef _WIN32
    // D3D12: ConfigureEvent must be called during UnityPluginLoad, before device init
    if (g_graphics->GetRenderer() == kUnityGfxRendererD3D12)
    {
        MDIBackend_D3D12 configurator;
        configurator.ConfigureEvents(unityInterfaces, g_baseEventID, MDI_MAX_PENDING);
    }
#endif

    // Vulkan: ConfigureEvent must be called during UnityPluginLoad, before device init
    if (g_graphics->GetRenderer() == kUnityGfxRendererVulkan)
    {
        MDIBackend_Vulkan configurator;
        configurator.ConfigureEvents(unityInterfaces, g_baseEventID, MDI_MAX_PENDING);
    }

    g_graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

    if (g_graphics->GetRenderer() != kUnityGfxRendererNull)
        OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
}

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityPluginUnload()
{
    if (g_graphics)
        g_graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);

    DestroyBackend();
    g_unityInterfaces = nullptr;
    g_graphics = nullptr;
}

// -----------------------------------------------------------------------
// Unity Rendering Extensions — stub (required export for GfxPlugin prefix)
// -----------------------------------------------------------------------

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
UnityRenderingExtEvent(UnityRenderingExtEventType event, void* data)
{
    (void)event;
    (void)data;
}

// -----------------------------------------------------------------------
// Render event callback — single UnityRenderingEventAndData for both APIs
// -----------------------------------------------------------------------

// Both D3D11 and D3D12 use IssuePluginEventAndData with this callback.
// - D3D11: params arrive via data pointer (pinned NativeArray from C#)
// - D3D12: params arrive via data pointer (same mechanism)
// The eventID encodes the slot: slot = (eventID - g_baseEventID) % MDI_MAX_PENDING
static void UNITY_INTERFACE_API OnRenderEventAndData(int eventID, void* data)
{
    if (!g_backendSupported || !g_backend)
        return;

    int slot = (eventID - g_baseEventID) % MDI_MAX_PENDING;
    if (slot < 0) slot += MDI_MAX_PENDING;

    // Copy params from data pointer into pending slot
    if (data)
        g_pending[slot] = *static_cast<const MDIParams*>(data);

    if (g_pending[slot].argsBuffer && g_pending[slot].maxDrawCount > 0)
        g_backend->ExecuteMDI(g_pending[slot]);

    // Clear slot
    g_pending[slot].argsBuffer = nullptr;
    g_pending[slot].maxDrawCount = 0;
}

// -----------------------------------------------------------------------
// Exported C API
// -----------------------------------------------------------------------

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
MDI_AllocSlot()
{
    return (g_pendingCounter++) % MDI_MAX_PENDING;
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
MDI_GetBaseEventID()
{
    return g_baseEventID;
}

// Returns UnityRenderingEventAndData callback pointer
extern "C" UNITY_INTERFACE_EXPORT UnityRenderingEventAndData UNITY_INTERFACE_API
MDI_GetRenderEventAndDataFunc()
{
    return OnRenderEventAndData;
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
MDI_IsSupported()
{
    return g_backendSupported;
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
MDI_IsD3D12()
{
    return (g_graphics && g_graphics->GetRenderer() == kUnityGfxRendererD3D12) ? 1 : 0;
}
