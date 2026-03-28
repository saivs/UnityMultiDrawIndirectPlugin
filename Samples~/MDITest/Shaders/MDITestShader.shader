Shader "Hidden/MDITest"
{
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" }

        // ---------------------------------------------------------------
        // Shared code across all passes
        // ---------------------------------------------------------------
        HLSLINCLUDE
        #pragma target 4.5
        #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

        struct VertexOutput
        {
            float4 positionCS : SV_POSITION;
            float3 color : TEXCOORD0;
        };

        StructuredBuffer<float3> _VertexBuffer;
        StructuredBuffer<float3> _DrawPositions;

        VertexOutput BuildOutput(uint vertexID, uint globalInstanceID)
        {
            VertexOutput o;
            float t = (float)globalInstanceID / 64.0;

            float3 localPos = _VertexBuffer[vertexID];
            float3 worldPos = localPos + _DrawPositions[globalInstanceID] + float3(0, sin(_Time.y + t), 0);
            o.positionCS = TransformWorldToHClip(worldPos);
            o.color = float3(frac(t * 3.0), frac(t * 5.0), frac(t * 7.0));
            return o;
        }

        float4 frag(VertexOutput i) : SV_Target
        {
            return float4(i.color, 1.0);
        }
        ENDHLSL

        // ---------------------------------------------------------------
        // Pass 0: RenderPrimitivesIndexedIndirect (Unity built-in loop)
        // Uses Unity's IndirectDraw system for correct SV_InstanceID handling.
        // ---------------------------------------------------------------
        Pass
        {
            Name "RenderPrimitivesForward"
            Tags { "LightMode" = "UniversalForward" }

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            
            #define UNITY_INDIRECT_DRAW_ARGS IndirectDrawIndexedArgs
            #include "UnityIndirect.cginc"

            VertexOutput vert(uint svVertexID : SV_VertexID, uint svInstanceID : SV_InstanceID)
            {
                InitIndirectDrawArgs(0);
                return BuildOutput(svVertexID, (globalIndirectDrawArgs.startInstance + svInstanceID));
            }

            ENDHLSL
        }

        // ---------------------------------------------------------------
        // Pass 1: MultiDrawIndirect (native plugin)
        // D3D11: plugin sets cbuffer b7 with draw call index per draw.
        //        Shader reads startInstance from ArgsBuffer.
        // Other APIs: SV_InstanceID already includes startInstance.
        // ---------------------------------------------------------------
        Pass
        {
            Name "MDIForward"

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            
            //use the MDI.hlsl file from the multi-draw-indirect plugin
            #include "Packages/com.saivs.multi-draw-indirect/Runtime/ShaderLibrary/MDI.hlsl"

            //add MDI_INSTANCE_ID_PARAMETER to the vertex shader signature to add the global instance ID to the vertex shader
            VertexOutput vert(uint vertexID : SV_VertexID, MDI_INSTANCE_ID_PARAMETER)
            {
                //use the MDI_INSTANCE_ID macro to get the global instance ID
                uint globalInstanceID = MDI_INSTANCE_ID;
                return BuildOutput(vertexID, globalInstanceID);
            }
            

            ENDHLSL
        }

        // ---------------------------------------------------------------
        // Pass 2: DrawProceduralIndirect loop (C# managed loop)
        // C# sets _DrawCallIndex via commandBuffer before each draw.
        // D3D11: reads startInstance from ArgsBuffer by _DrawCallIndex.
        // Other APIs: SV_InstanceID already includes startInstance.
        // ---------------------------------------------------------------
        Pass
        {
            Name "ProceduralLoopForward"

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag

            struct IndirectDrawIndexedArgs
            {
                uint indexCountPerInstance;
                uint instanceCount;
                uint startIndex;
                uint baseVertexIndex;
                uint startInstance;
            };

            StructuredBuffer<IndirectDrawIndexedArgs> _ArgsBuffer;
            int _DrawCallIndex;

            VertexOutput vert(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
            {
                #if defined(SHADER_API_D3D11)
                    uint globalInstanceID = _ArgsBuffer[_DrawCallIndex].startInstance + instanceID;
                #else
                    uint globalInstanceID = instanceID;
                #endif
                return BuildOutput(vertexID, globalInstanceID);
            }
            ENDHLSL
        }
    }
}
