using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using UnityEngine.Rendering.RenderGraphModule;

/// <summary>
/// URP ScriptableRenderPass that executes MDI draw calls after opaque objects.
/// Injected directly via urpRenderer.EnqueuePass() from beginCameraRendering.
/// </summary>
public class MDIRenderPass : ScriptableRenderPass
{
    private System.Action<RasterCommandBuffer> _renderCallback;

    public MDIRenderPass()
    {
        renderPassEvent = RenderPassEvent.AfterRenderingOpaques;
    }

    public void SetCallback(System.Action<RasterCommandBuffer> callback)
    {
        _renderCallback = callback;
    }

    private class PassData
    {
        public System.Action<RasterCommandBuffer> callback;
    }

    public override void RecordRenderGraph(RenderGraph renderGraph, ContextContainer frameData)
    {
        if (_renderCallback == null) return;

        var resourceData = frameData.Get<UniversalResourceData>();

        using var builder = renderGraph.AddRasterRenderPass<PassData>("MDI Render Pass", out var passData);

        passData.callback = _renderCallback;

        builder.SetRenderAttachment(resourceData.activeColorTexture, 0);
        builder.SetRenderAttachmentDepth(resourceData.activeDepthTexture);
        builder.AllowGlobalStateModification(true);

        builder.SetRenderFunc((PassData data, RasterGraphContext ctx) =>
        {
            data.callback(ctx.cmd);
        });
    }
}
