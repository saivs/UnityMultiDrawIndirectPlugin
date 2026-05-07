# Unity Multi Draw Indirect Plugin

A native plugin that brings true **Multi-Draw Indirect (MDI)** to Unity.

![MDI Test](MDI_Test.gif)

## What is Multi-Draw Indirect?

A **draw call** is one instruction to the GPU: "render this object with this material". Each one has real CPU cost — the engine has to validate state, build a command, and submit it to the graphics driver.

A "small" scene easily hits **2,000–3,000 draw calls per frame**, even with very few objects on screen. Each visible object isn't drawn once — it's drawn several times per frame: once for the main image, once or twice more for shadows, often again for a depth pre-pass, plus extra passes for transparency and post-processing. Add LODs (different mesh detail at different distances) and the count grows fast.

Even on a fast desktop CPU with Unity's built-in batching, **1,000 draw calls can cost over 1 ms of CPU time per frame**. At 60 fps you have a 16.6 ms budget for *everything* — physics, AI, scripts, rendering — so 1 ms just submitting draws to the GPU is a real problem.

**Multi-Draw Indirect (MDI)** solves this by replacing N draw calls with one. You write the parameters for all N draws into a single GPU buffer, then make **one** call that tells the GPU "look in this buffer and run whatever draws you find". The GPU does the dispatch itself — there's no per-draw CPU overhead. This is the technique modern engines like Unreal's Nanite and Frostbite use to render millions of objects without the CPU collapsing under draw-call cost.

## Why this plugin?

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
| Metal | ✅ Supported | `drawIndexedPrimitives:indirectBuffer:` via Objective-C method swizzling |

## Performance

CPU time comparison for **25,000 draw calls**. D3D11/D3D12/Vulkan/OpenGL ES tested on RTX 3080; Metal tested on Apple M2 Pro (Mac mini).
Measured as total `PlayerLoop` time (not just command submission) in the build, so the numbers include all engine overhead per frame:

### D3D11

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 0.41 ms |
| ProceduralIndirect Loop | 23.77 ms |
| RenderPrimitivesIndexedIndirect | 15.11 ms |

### D3D12

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 0.35 ms |
| ProceduralIndirect Loop | 28.61 ms |
| RenderPrimitivesIndexedIndirect | 36.24 ms |

### Vulkan

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 0.35 ms |
| ProceduralIndirect Loop | 25.06 ms |
| RenderPrimitivesIndexedIndirect | 23.08 ms |

### OpenGLES

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 1.18 ms |
| ProceduralIndirect Loop | 25.7 ms |
| RenderPrimitivesIndexedIndirect | 23.7 ms |

### Metal (Apple M2 Pro, Mac mini)

| Method | CPU Time |
|---|---|
| **MultiDrawIndirect** | 1.52 ms |
| ProceduralIndirect Loop | 23.06 ms |
| RenderPrimitivesIndexedIndirect | 18.43 ms |

## Limitations

- **D3D11 + RenderDoc**: The plugin uses NvAPI, which can cause Unity to crash when RenderDoc attempts to inject at runtime. To avoid this, attach RenderDoc **at Unity startup** (launch Unity from RenderDoc) rather than connecting mid-session.
- **D3D11 + AMD GPUs**: D3D11 does not have a native MDI API. On NVIDIA, this is solved via NvAPI, which can attach to an already-created D3D11 device. AMD has an equivalent extension in AGS (`agsDriverExtensionsDX11_MultiDrawIndexedInstancedIndirect`), but AGS requires the D3D11 device to be created through `agsDriverExtensionsDX11_CreateDevice` — since Unity creates the device itself, AGS extensions cannot be enabled retroactively. Because of this (and lack of AMD hardware for testing), MDI on D3D11 + AMD is not currently supported. AMD GPUs are fully supported under D3D12, Vulkan, and OpenGL.
- **Consoles & mobile devices**: The plugin has only been tested on desktop Windows and macOS. It has not been verified on consoles (PlayStation, Xbox, Switch) or mobile devices — support on these platforms is not guaranteed.
- **No Unity Mesh support**: The plugin currently uses `DrawProceduralIndirect` / `ExecuteIndirect`, which do not bind a Unity `Mesh` object. Vertex data must be stored in `StructuredBuffer` or `GraphicsBuffer` and fetched manually by `SV_VertexID` in the shader. This is standard practice for GPU-driven pipelines, but means you cannot pass a `Mesh` directly. Adding Mesh support is feasible — the prime draw already uses `DrawMesh` with a `TEXCOORD7` layout on D3D11/D3D12, so extending this to bind the user's actual Mesh (and its vertex/index buffers) instead of a dummy is a natural next step.
- **Identity buffer instance limit (D3D11/D3D12/OpenGL/GLES)**: The per-instance identity buffer defaults to 65,536 entries. For any draw command in an MDI batch, `startInstance + instanceCount` must not exceed this value. Use `MultiDrawIndirect.MaxInstanceCount` to increase or decrease the limit at runtime. This limitation does not apply to Vulkan.

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

// Increase the limit to 1,000,000 instances (D3D11/D3D12/OpenGL)
// Read the Deep Dive section "Per-Instance Identity Buffer"
MultiDrawIndirect.MaxInstanceCount = 1_000_000;

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

The plugin provides `MDI.hlsl` with two macros that handle cross-platform instance ID resolution automatically:

| Macro | Purpose |
|---|---|
| `MDI_INSTANCE_ID_PARAMETER` | Place in vertex shader signature — expands to the correct platform-specific parameter |
| `MDI_INSTANCE_ID` | Use in vertex shader body — resolves to the global instance index across all draw commands |

```hlsl
HLSLPROGRAM
#pragma vertex vert
#pragma fragment frag

#include "Packages/com.saivs.multi-draw-indirect/Runtime/ShaderLibrary/MDI.hlsl"

VertexOutput vert(uint vertexID : SV_VertexID, MDI_INSTANCE_ID_PARAMETER)
{
    uint globalInstanceID = MDI_INSTANCE_ID;

    // Use globalInstanceID to fetch per-instance data (positions, transforms, etc.)
    return BuildOutput(vertexID, globalInstanceID);
}
ENDHLSL
```

The macros expand differently depending on the platform and compile-time defines:

| Platform | `MDI_INSTANCE_ID_PARAMETER` | `MDI_INSTANCE_ID` |
|---|---|---|
| D3D11 / D3D12 / OpenGL / OpenGL ES | `uint : TEXCOORD7` | Identity buffer value (global instance index) |
| Vulkan / WebGPU | `uint : SV_InstanceID` | `SV_InstanceID` (already includes `startInstance`) |
| Metal | `uint : SV_InstanceID` | `_ArgsBuffer[_MDI_DrawIndex_Buffer[0]].startInstance + SV_InstanceID` |
| Fallback loop (`MDI_NATIVE_LOOP`) | `uint : SV_InstanceID` | `_ArgsBuffer[_MDI_DrawIndex].startInstance + SV_InstanceID` |

See the included [sample shader](Samples~/MDITest/Shaders/MDITestShader.shader) for a complete working example with multiple pass configurations.

## Technical Deep Dive: Cross-Platform Instance ID

The central challenge in implementing Multi-Draw Indirect across all graphics APIs is obtaining a correct **global instance index** — one that uniquely identifies each instance across all draw commands in a single MDI batch.

### The Problem

When issuing multiple draw commands via MDI, each draw has its own `startInstance` and `instanceCount` in the indirect arguments buffer. The shader needs a way to compute the global instance index: `startInstance + SV_InstanceID`.

On **Vulkan**, this works out of the box — `SV_InstanceID` (mapped to `gl_InstanceIndex` in SPIR-V) already includes the `firstInstance` offset, so it directly represents the global instance index.

On **D3D11, D3D12, OpenGL, and OpenGL ES**, however, `SV_InstanceID` always starts from zero regardless of the `startInstance`/`baseInstance` value in the draw arguments. On OpenGL, `SV_InstanceID` maps to `gl_InstanceID`, which — unlike Vulkan's `gl_InstanceIndex` — does **not** include `baseInstance`. The GPU does use `startInstance`/`baseInstance` internally to offset per-instance vertex buffer reads, but it does **not** expose that offset through the instance ID system value. This means the shader has no way to determine which draw command it belongs to, effectively making MDI useless on these APIs without a workaround.

### The D3D11 Challenge

D3D11 does not have a native MDI API at all. NVIDIA provides MDI functionality through their **NvAPI** extension (`NvAPI_D3D11_MultiDrawIndexedInstancedIndirect`), and AMD has an equivalent in AGS (`agsDriverExtensionsDX11_MultiDrawIndexedInstancedIndirect`). However, AGS requires the D3D11 device to be created through `agsDriverExtensionsDX11_CreateDevice` — since Unity creates the device itself, AGS extensions cannot be enabled retroactively. Because of this, the plugin currently supports D3D11 MDI on NVIDIA GPUs only.

Critically, the NvAPI MDI extension **also does not provide** `startInstance` to the shader. This limitation is well-documented — see [Interplay of Light: Experiments in GPU-based Occlusion Culling Part 2](https://interplayoflight.wordpress.com/2018/01/15/experiments-in-gpu-based-occlusion-culling-part-2-multidrawindirect-and-mesh-lodding/comment-page-1/) for a detailed discussion of this exact issue.

### The Solution: Per-Instance Identity Buffer

The approach used by this plugin (inspired by the article above) is to create a **per-instance vertex buffer** — an "identity buffer" — filled with sequential indices `[0, 1, 2, ..., N-1]`. When bound as a per-instance vertex input, the GPU's Input Assembler automatically offsets reads by `StartInstanceLocation`, so instance `i` in draw command `d` reads the value `startInstance_d + i` from the buffer. This value arrives in the shader as `TEXCOORD7` and serves as the global instance index — no args buffer lookup required.

#### Limitations of the Identity Buffer

The identity buffer defaults to **65,536 elements**. This means `startInstance + instanceCount` for any single draw command must not exceed this value. The buffer size can be changed at runtime via the `MaxInstanceCount` property:

```csharp
// Increase the limit to 1,000,000 instances (D3D11/D3D12/OpenGL)
MultiDrawIndirect.MaxInstanceCount = 1_000_000;

// Query the current limit
uint current = MultiDrawIndirect.MaxInstanceCount;
```

This limit applies only to APIs that use the identity buffer (D3D11, D3D12, OpenGL Core, OpenGL ES). On Vulkan and Metal `MaxInstanceCount` returns 0 and has no effect — Vulkan's `gl_InstanceIndex` already includes `startInstance`, and Metal's backend uses a different mechanism (see the Metal section below).

### The Unity Problem: No Access to Input Layouts

In a typical D3D11/D3D12 application, adding a per-instance vertex buffer is straightforward — you simply modify the input layout (PSO) to include the new element. But Unity does **not** expose any API for modifying Pipeline State Objects, input layouts, or vertex buffer bindings from C# or native plugins.

### The Workaround: Input Layout Hooking

This plugin solves the problem by **intercepting** `ID3D11Device::CreateInputLayout()` (and the equivalent D3D12 PSO creation) at the native level using an inline function hook. When Unity creates an input layout, the hook checks whether it contains a `TEXCOORD7` semantic. If it does, the hook patches that element to:

- Read from **vertex buffer slot 15** (an otherwise unused slot)
- Use `D3D11_INPUT_PER_INSTANCE_DATA` with `InstanceDataStepRate = 1`
- Format `R32_UINT`

At draw time, the plugin binds the identity buffer to slot 15. The Input Assembler then automatically loads the correct global index for each instance.

**Triggering PSO recreation:** Unity creates Pipeline State Objects before the native plugin is loaded, so the hook is not active during initial PSO creation. To force Unity to recreate the PSO (and trigger the hook), the plugin uses `VertexAttributeDescriptor` on the C# side to declare a mesh layout that includes `TEXCOORD7`. This causes Unity to create a new input layout that passes through the hook.

### OpenGL / OpenGL ES

On OpenGL, `gl_InstanceID` also does **not** include `baseInstance` — this is a common misconception, as Vulkan's `gl_InstanceIndex` does. The solution is the same identity buffer approach, but the implementation is simpler than D3D11/D3D12: OpenGL's vertex attribute state is dynamic, so no PSO hooking is needed.

Before each MDI call, the plugin creates a dedicated VAO (Vertex Array Object), queries the active shader program for the `TEXCOORD7` attribute location via `glGetAttribLocation`, and binds the identity buffer to that location using `glVertexAttribIPointer` with `glVertexAttribDivisor(location, 1)`. The attribute location and VAO are cached across frames for performance, with the VAO validated via `glIsVertexArray` to handle Unity's implicit GL context resets (e.g. window maximize/detach). After the MDI draw, the plugin restores Unity's original VAO.

### Metal: Why "It's Impossible" Was Wrong

Metal looked like a dead end at first. Three blockers stack on top of each other:

1. **`[[instance_id]]` does not include `[[base_instance]]`.** Apple's Metal Shading Language is explicit: `[[instance_id]]` is the per-draw counter that resets to 0 on every draw command, regardless of the `baseInstance` baked into the indirect arguments. So a Metal shader has no built-in path to "global instance index across draws".
2. **HLSL has no `SV_StartInstanceLocation` / `SV_BaseInstance`.** Unity's HLSL→MSL translator does not expose `[[base_instance]]` through any standard system value. Trying to declare `SV_StartInstanceLocation` on Metal produces `error X4567: maximum cbuffer exceeded` from Unity's compiler — it simply isn't a recognized semantic.
3. **`CommandBuffer.IssuePluginEventAndData` fires *outside* the active encoder in URP RenderGraph.** On Vulkan and D3D12, plugin events fire while Unity's command buffer/list is still recording, and the plugin gets a live `vkCommandBuffer` / `ID3D12GraphicsCommandList` via `IUnityGraphicsVulkan::CommandRecordingState` / `IUnityGraphicsD3D12v7::CommandRecordingState`. On Metal under URP NativePassCompiler, the situation is different: by the time the plugin event fires, Unity has already called `[encoder endEncoding]`. `IUnityGraphicsMetal::CurrentCommandEncoder()` returns a non-nil pointer that responds to Objective-C metadata selectors (`-respondsToSelector:`, `-conformsToProtocol:`) but **segfaults inside the Metal driver on the first draw command** — it's a freshly-ended encoder. Worse, even if we created our own encoder via `EndCurrentCommandEncoder` + `CurrentRenderPassDescriptor`, we have no access to the user's compiled `MTLRenderPipelineState`, and on Apple Silicon Metal will `__abort` the process if any draw call is encoded without a PSO bound.

The "obvious" approach — get the encoder, issue N indirect draws on it — is therefore not viable on Metal in modern Unity. Apple's official `NativeRenderingPlugin` sample sidesteps this by compiling its **own** PSO with **its own** shader, which is fine for self-contained drawing but useless when we want to use the user's `Material`.

#### The Workaround: Method Swizzling

The trick is to stop trying to inject draws *after* Unity's prime; instead, **intercept Unity's prime draw itself**, while the encoder is still alive and the user's PSO + resources are bound.

The plugin uses Objective-C runtime method swizzling to replace the implementation of two selectors on the concrete `MTLRenderCommandEncoder` class Unity uses (probed at init time by spinning up a throwaway encoder on Unity's `MetalDevice()`):

1. **`-drawIndexedPrimitives:indexType:indexBuffer:indexBufferOffset:indirectBuffer:indirectBufferOffset:`** — this is exactly the Metal selector that `cmd.DrawProceduralIndirect(indexBuffer, ..., bufferWithArgs)` lowers to. The C# wrapper issues a "prime" `DrawProceduralIndirect` against a small **dummy args buffer** (`instanceCount = 0`, so no pixels) just before calling `IssuePluginEventAndData`. When Unity later replays this command, the swizzled implementation:
   - recognises the prime by **pointer-equality** with the registered dummy buffer,
   - decodes a per-call slot index from `indirectBufferOffset` (C# encodes it as `slot * 20` to map onto the size of `MTLDrawIndexedPrimitivesIndirectArguments`),
   - reads the real `argsBuffer` / `indexBuffer` / `drawCount` from a **pinned C# `NativeArray`** registered with the plugin via `MDI_SetParamsRing` (this is critical — `g_pending` populated by the plugin event would not be ready yet, since the prime fires *before* the event),
   - and calls the original IMP **N times** on the same encoder, each time pointing at the user's real args at `argsOffset + i * 20`.

   A `thread_local` re-entry flag prevents the inner loop from recursing through the swizzle.

2. **`-setVertexBuffer:offset:atIndex:` and `-setVertexBuffers:offsets:withRange:`** — used to **auto-detect the MSL buffer slot** of `_MDI_DrawIndex_Buffer`. Unity's HLSL→MSL emitter (HLSLcc) reorders cbuffer / structured-buffer slots based on per-shader compilation order, so a hard-coded `register(bN)` does not survive translation. Instead, the plugin pre-creates a tiny `MTLBuffer` and registers its native pointer (`MDI_SetDrawIndexBuffer`); when Unity binds that buffer, both swizzled selectors fire, the hook compares the buffer pointer, and stores whichever slot Unity assigned. No guessing, no shader-specific tuning — works for any user material that includes `MDI.hlsl`.

   With the slot known, the inner loop calls `[encoder setVertexBytes:&i length:16 atIndex:slot]` before each indirect draw to push the current draw index inline. The shader reads `_MDI_DrawIndex_Buffer[0]` and gets `i` back. Inline data has no buffer-size limit, so this works for arbitrary draw counts (tested at 200×200 grids = 20,000 draws per call without issues).

The end result is a real GPU-side multi-draw on Metal: one prime CPU call, one live encoder, N indirect draws, with full access to the user's compiled PSO and material resources. The shader then computes `globalInstanceID = _ArgsBuffer[_MDI_DrawIndex_Buffer[0]].startInstance + SV_InstanceID` to recover the correct per-instance index across the batch.
