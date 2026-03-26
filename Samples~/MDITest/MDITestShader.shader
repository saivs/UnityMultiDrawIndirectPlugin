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

            struct VertexOutput
            {
                float4 positionCS : SV_POSITION;
                float3 color : TEXCOORD0;
            };

            StructuredBuffer<float3> _VertexBuffer;

            VertexOutput vert(uint vertexID : SV_VertexID)
            {
                VertexOutput o;

                // SV_VertexID includes baseVertexIndex offset from indirect args,
                // so each draw command reads its own 4 vertices with baked world positions.
                float3 worldPos = _VertexBuffer[vertexID];
                o.positionCS = TransformWorldToHClip(worldPos);

                // Color based on draw index (vertexID / 4 = which quad)
                uint drawIndex = vertexID / 4;
                float hue = frac((float)drawIndex * 0.618033988749895); // golden ratio
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
