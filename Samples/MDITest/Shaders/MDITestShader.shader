Shader "Hidden/MDITest"
{
    SubShader
    {
        Tags { "RenderType" = "Opaque" "RenderPipeline" = "UniversalPipeline" }

        Pass
        {
            Name "MDITestForward"
            Tags { "LightMode" = "UniversalForward" }

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 4.5

            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            #define MDI_VERTEX_COUNT 4
            #include "Packages/com.saivs.multi-draw-indirect/Runtime/ShaderLibrary/MDI.hlsl"

            struct VertexOutput
            {
                float4 positionCS : SV_POSITION;
                float3 color : TEXCOORD0;
            };

            StructuredBuffer<float3> _VertexBuffer;
            StructuredBuffer<float3> _DrawPositions;

            VertexOutput vert(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
            {
                VertexOutput o;
                
                float3 localPos = _VertexBuffer[vertexID];
                float3 worldPos = localPos + _DrawPositions[instanceID];
                o.positionCS = TransformWorldToHClip(worldPos);

                float hue = frac((float)instanceID * 0.218033988749895);
                float3 rgb = saturate(abs(frac(hue + float3(0.0, 0.333, 0.667)) * 6.0 - 3.0) - 1.0);
                o.color = rgb;

                return o;
            }

            float4 frag(VertexOutput i) : SV_Target
            {
                return float4(i.color, 1.0);
            }
            ENDHLSL
        }
    }
}
