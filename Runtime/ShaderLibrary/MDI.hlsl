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
// Internal resources for MDI_NATIVE_LOOP path (cbuffer + args)
// ---------------------------------------------------------------
#if defined(MDI_NATIVE_LOOP)

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

#endif // MDI_NATIVE_LOOP

// ---------------------------------------------------------------
// Platform detection: does SV_InstanceID include startInstance?
// Only Vulkan (gl_InstanceIndex) and WebGPU include it.
// D3D11, D3D12, OpenGL Core, OpenGL ES — SV_InstanceID / gl_InstanceID
// does NOT include startInstance/baseInstance.
// ---------------------------------------------------------------
#if defined(SHADER_API_VULKAN) || defined(SHADER_API_WEBGPU)
    #define MDI_SV_INSTANCE_ID_INCLUDES_START 1
#endif

// ---------------------------------------------------------------
// MDI_INSTANCE_ID_PARAMETER — place in vertex shader signature
// ---------------------------------------------------------------
#if !defined(MDI_SV_INSTANCE_ID_INCLUDES_START) && !defined(MDI_NATIVE_LOOP)
    // D3D11 / D3D12 / OpenGL: identity buffer in TEXCOORD7 carries global instance ID.
    // The Input Assembler offsets per-instance VB reads by startInstance/baseInstance,
    // so the identity buffer value [0,1,2,...N] becomes startInstance + localInstance.
    #define MDI_INSTANCE_ID_PARAMETER uint _mdi_globalInstanceID : TEXCOORD7
#else
    #define MDI_INSTANCE_ID_PARAMETER uint _mdi_instanceID : SV_InstanceID
#endif

// ---------------------------------------------------------------
// MDI_INSTANCE_ID — resolves to the global instance index
// ---------------------------------------------------------------
#if !defined(MDI_SV_INSTANCE_ID_INCLUDES_START) && !defined(MDI_NATIVE_LOOP)
    #define MDI_INSTANCE_ID (_mdi_globalInstanceID)
#elif defined(MDI_NATIVE_LOOP)
    #define MDI_INSTANCE_ID (_ArgsBuffer[_MDI_DrawIndex].startInstance + _mdi_instanceID)
#else
    // Vulkan / WebGPU — SV_InstanceID already includes startInstance
    #define MDI_INSTANCE_ID (_mdi_instanceID)
#endif

#endif // MDI_INCLUDED
