# Unity Multi Draw Indirect Plugin

A native plugin that brings true **Multi-Draw Indirect (MDI)** to Unity.

## Why?

Modern GPU-driven rendering pipelines rely on Multi-Draw Indirect to batch thousands of draw calls into a single GPU command. Unity does **not** expose MDI in any form:

- `Graphics.RenderPrimitivesIndexedIndirect` is the closest built-in alternative, but it is **not** MDI — it issues individual draw calls on the CPU side and, critically, **cannot be used inside CommandBuffers**, making it unusable in scriptable render pipelines and render graph workflows.
- `CommandBuffer.DrawProceduralIndirect` supports only a single indirect draw per call. Issuing it in a loop ("ProceduralIndirect loop") works but scales poorly — each call has full CPU overhead of state validation, command recording, and managed-to-native transitions.

This plugin solves the problem by injecting a single native MDI command directly into Unity's graphics command stream via `IssuePluginEventAndData`, providing **true hardware-level batching** with minimal CPU cost.

## Supported Platforms

| Graphics API | Status | Backend |
|---|---|---|
| D3D11 | ✅ Supported | (Nvidia)NvAPI `DrawIndexedInstancedIndirect` / loop fallback |
| D3D12 | ✅ Supported | `ExecuteIndirect` via `CommandRecordingState` |
| Vulkan | ✅ Supported | `vkCmdDrawIndexedIndirect` (multi-draw or loop fallback) |
| OpenGL Core | ✅ Supported | `glMultiDrawElementsIndirect` |
| OpenGL ES 3.1+ | ✅ Supported | `glMultiDrawElementsIndirect` |
| Metal | ❌ Not yet supported | `MTLIndirectCommandBuffer` / `executeCommandsInBuffer` |

## Performance

CPU time comparison for **40,000 draw calls** on RTX 3080.
Measured as total `PlayerLoop` time (not just command submission), so the numbers include all engine overhead per frame:

### D3D11

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 0.51 ms |
| ProceduralIndirect Loop | 46.71 ms |
| RenderPrimitivesIndexedIndirect | 3.68 ms |

### D3D12

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 0.56 ms |
| ProceduralIndirect Loop | 44.62 ms |
| RenderPrimitivesIndexedIndirect | 29.83 ms |

### Vulkan

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 0.6 ms |
| ProceduralIndirect Loop | 76.03 ms |
| RenderPrimitivesIndexedIndirect | 18.57 ms |

### OpenGLES

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 1.23 ms |
| ProceduralIndirect Loop | 38.31 ms |
| RenderPrimitivesIndexedIndirect | 13.90 ms |

## Limitations

- **D3D11 + RenderDoc**: The plugin uses NvAPI, which can cause Unity to crash when RenderDoc attempts to inject at runtime. To avoid this, attach RenderDoc **at Unity startup** (launch Unity from RenderDoc) rather than connecting mid-session.
- **D3D11 + AMD GPUs**: D3D11 does not have a native MDI API. On NVIDIA, this is solved via NvAPI, which can attach to an already-created D3D11 device. AMD has an equivalent extension in AGS (`agsDriverExtensionsDX11_MultiDrawIndexedInstancedIndirect`), but AGS requires the D3D11 device to be created through `agsDriverExtensionsDX11_CreateDevice` — since Unity creates the device itself, AGS extensions cannot be enabled retroactively. Because of this (and lack of AMD hardware for testing), MDI on D3D11 + AMD is not currently supported. AMD GPUs are fully supported under D3D12, Vulkan, and OpenGL.
- **Metal**: Metal supports indirect command buffers (`MTLIndirectCommandBuffer` / `executeCommandsInBuffer`), so a native MDI backend is feasible. It is not yet implemented because the author does not currently have access to a macOS/iOS device for testing.
- **Consoles & mobile devices**: The plugin has only been tested on desktop Windows. It has not been verified on consoles (PlayStation, Xbox, Switch) or mobile devices — support on these platforms is not guaranteed.

## Installation

Add the package via Unity Package Manager using a git URL:

1. Open **Window > Package Manager**
2. Click **+** > **Add package from git URL...**
3. Enter:
   ```
   https://github.com/saivs/UnityMultiDrawIndirectPlugin.git
   ```

## Usage

The plugin exposes a single extension method on `CommandBuffer` (and `RasterCommandBuffer` / `UnsafeCommandBuffer` in Unity 6+):

```csharp
using Saivs.Graphics.Core.MDI;

cmd.MultiDrawIndexedIndirect(
    indexBuffer:    indexBuffer,
    material:       material,
    properties:     propertyBlock,
    shaderPass:     0,
    topology:       MeshTopology.Triangles,
    bufferWithArgs: argsBuffer,
    argsStartIndex: 0,
    argsCount:      drawCount
);
```

That's it — one call replaces the entire draw loop. When the native plugin is available, all draws are batched into a single MDI command. Otherwise, it falls back to a `DrawProceduralIndirect` loop automatically.

### Shader

<!-- TODO: shader setup examples -->

*Shader examples coming soon.*
