#ifndef MDI_INCLUDED
#define MDI_INCLUDED

// Multi-Draw Indirect shader utilities.
//
// Usage:
//   #include "Packages/com.saivs.multi-draw-indirect/Runtime/ShaderLibrary/MDI.hlsl"
//
//   VertexOutput vert(uint vertexID : SV_VertexID, MDI_INSTANCE_ID_PARAMETER)
//   {
//       uint globalInstanceID = MDI_INSTANCE_ID;
//       ...
//   }

// ---------------------------------------------------------------
// Per-draw bookkeeping declarations
// ---------------------------------------------------------------

#if defined(SHADER_API_METAL)
    // Metal path: the native plugin pushes the current draw index via
    // inline setVertexBytes between each indirect draw, at whichever MSL
    // buffer slot Unity assigned to `_MDI_DrawIndex_Buffer` (auto-detected
    // by swizzling setVertexBuffer / setVertexBuffers). The shader reads
    // `_MDI_DrawIndex_Buffer[0]` and gets the current draw index. The
    // args buffer is bound by the user's MaterialPropertyBlock and
    // carries per-draw startInstance.
    struct MDI_IndirectDrawIndexedArgs
    {
        uint indexCountPerInstance;
        uint instanceCount;
        uint startIndex;
        uint baseVertexIndex;
        uint startInstance;
    };
    StructuredBuffer<MDI_IndirectDrawIndexedArgs> _ArgsBuffer;
    StructuredBuffer<uint> _MDI_DrawIndex_Buffer;
#elif defined(MDI_NATIVE_LOOP)
    // Opt-in fallback for the C# DrawProceduralIndirect loop path on
    // platforms with no real MDI (D3D11 + AMD, where AGS extensions
    // cannot attach to Unity's already-created device). Define
    // MDI_NATIVE_LOOP from the shader before #include'ing this file:
    // the C# wrapper updates `_MDI_DrawIndex` via cbuffer (register b7)
    // before each per-call DrawProceduralIndirect, and the args buffer
    // gives per-draw startInstance.
    struct MDI_IndirectDrawIndexedArgs
    {
        uint indexCountPerInstance;
        uint instanceCount;
        uint startIndex;
        uint baseVertexIndex;
        uint startInstance;
    };
    StructuredBuffer<MDI_IndirectDrawIndexedArgs> _ArgsBuffer;
    cbuffer MDI_DrawIndex : register(b7)
    {
        uint _MDI_DrawIndex;
        uint3 _MDI_Pad;
    };
#endif

// ---------------------------------------------------------------
// MDI_INSTANCE_ID_PARAMETER — place in vertex shader signature
// ---------------------------------------------------------------
#if defined(SHADER_API_VULKAN) || defined(SHADER_API_WEBGPU) || defined(SHADER_API_METAL) || defined(MDI_NATIVE_LOOP)
    // Vulkan / WebGPU: SV_InstanceID already includes startInstance.
    // Metal:           SV_InstanceID is per-draw; plugin supplies offset.
    // MDI_NATIVE_LOOP: SV_InstanceID is per-draw; cbuffer + args supply offset.
    #define MDI_INSTANCE_ID_PARAMETER uint _mdi_instanceID : SV_InstanceID
#else
    // D3D11 / D3D12 / OpenGL: identity buffer in TEXCOORD7 carries the
    // global instance index directly — startInstance offsets per-instance
    // VB reads, so [0,1,2,...] becomes [startInstance, startInstance+1, ...].
    #define MDI_INSTANCE_ID_PARAMETER uint _mdi_globalInstanceID : TEXCOORD7
#endif

// ---------------------------------------------------------------
// MDI_INSTANCE_ID — resolves to the global instance index
// ---------------------------------------------------------------
#if defined(SHADER_API_METAL)
    #define MDI_INSTANCE_ID (_ArgsBuffer[_MDI_DrawIndex_Buffer[0]].startInstance + _mdi_instanceID)
#elif defined(MDI_NATIVE_LOOP)
    #define MDI_INSTANCE_ID (_ArgsBuffer[_MDI_DrawIndex].startInstance + _mdi_instanceID)
#elif defined(SHADER_API_VULKAN) || defined(SHADER_API_WEBGPU)
    #define MDI_INSTANCE_ID (_mdi_instanceID)
#else
    #define MDI_INSTANCE_ID (_mdi_globalInstanceID)
#endif

#endif // MDI_INCLUDED
