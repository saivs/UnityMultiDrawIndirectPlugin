using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using UnityEngine.Rendering.RenderGraphModule;
using Saivs.Graphics.Core.MDI;

namespace Saivs.Graphics.Test
{
    public class MDIRenderPass : ScriptableRenderPass
    {
        private static readonly int DrawCallIndexID = Shader.PropertyToID("_DrawCallIndex");

        private GraphicsBuffer _indexBuffer;
        private GraphicsBuffer _argsBuffer;
        private Material _material;
        private MaterialPropertyBlock _mpb;
        private int _drawCount;
        private MDITestController.DrawMode _drawMode;

        public MDIRenderPass()
        {
            renderPassEvent = RenderPassEvent.AfterRenderingOpaques;
        }

        public void SetRenderData(
            GraphicsBuffer indexBuffer,
            GraphicsBuffer argsBuffer,
            Material material,
            MaterialPropertyBlock mpb,
            int drawCount,
            MDITestController.DrawMode drawMode)
        {
            _indexBuffer = indexBuffer;
            _argsBuffer = argsBuffer;
            _material = material;
            _mpb = mpb;
            _drawCount = drawCount;
            _drawMode = drawMode;
        }

        private class PassData
        {
            public GraphicsBuffer indexBuffer;
            public GraphicsBuffer argsBuffer;
            public Material material;
            public MaterialPropertyBlock mpb;
            public int drawCount;
            public MDITestController.DrawMode drawMode;
        }

        public override void RecordRenderGraph(RenderGraph renderGraph, ContextContainer frameData)
        {
            if (_argsBuffer == null || _material == null) return;

            var resourceData = frameData.Get<UniversalResourceData>();

            using var builder = renderGraph.AddRasterRenderPass<PassData>("MDI Render Pass", out var passData);

            passData.indexBuffer = _indexBuffer;
            passData.argsBuffer = _argsBuffer;
            passData.material = _material;
            passData.mpb = _mpb;
            passData.drawCount = _drawCount;
            passData.drawMode = _drawMode;

            builder.SetRenderAttachment(resourceData.activeColorTexture, 0);
            builder.SetRenderAttachmentDepth(resourceData.activeDepthTexture);
            builder.AllowGlobalStateModification(true);

            builder.SetRenderFunc((PassData data, RasterGraphContext ctx) =>
            {
                Render(ctx.cmd, data);
            });
        }

        private static void Render(RasterCommandBuffer cmd, PassData data)
        {
            if (data.drawMode == MDITestController.DrawMode.MultiDrawIndirect)
            {
                cmd.MultiDrawIndexedIndirect(
                    indexBuffer: data.indexBuffer,
                    material: data.material,
                    properties: data.mpb,
                    shaderPass: 1,
                    topology: MeshTopology.Triangles,
                    bufferWithArgs: data.argsBuffer,
                    argsStartIndex: 0,
                    argsCount: data.drawCount);
            }
            else
            {
                for (int i = 0; i < data.drawCount; i++)
                {
                    cmd.SetGlobalInt(DrawCallIndexID, i);
                    cmd.DrawProceduralIndirect(
                        indexBuffer: data.indexBuffer,
                        matrix: Matrix4x4.identity,
                        material: data.material,
                        shaderPass: 2,
                        topology: MeshTopology.Triangles,
                        bufferWithArgs: data.argsBuffer,
                        argsOffset: i * GraphicsBuffer.IndirectDrawIndexedArgs.size,
                        properties: data.mpb);
                }
            }
        }
    }
}
