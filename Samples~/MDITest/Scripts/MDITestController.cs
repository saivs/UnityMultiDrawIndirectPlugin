using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace Saivs.Graphics.Test
{
    [ExecuteInEditMode]
    public class MDITestController : MonoBehaviour
    {
        public enum DrawMode
        {
            MultiDrawIndirect,
            ProceduralIndirectLoop,
            RenderPrimitivesIndexedIndirect
        }

        [Header("Rendering")]
        [SerializeField] private Material _material;
        [SerializeField] private DrawMode _drawMode = DrawMode.MultiDrawIndirect;
        [SerializeField] private MDITestBufferManager _bufferManager;

        private MDIRenderPass _mdiRenderPass;

        public DrawMode CurrentDrawMode => _drawMode;
        public MDITestBufferManager BufferManager => _bufferManager;

        private void OnEnable()
        {
            _mdiRenderPass = new MDIRenderPass();
            RenderPipelineManager.beginCameraRendering += OnBeginCameraRendering;
        }

        private void OnDisable()
        {
            RenderPipelineManager.beginCameraRendering -= OnBeginCameraRendering;
        }

        private void Update()
        {
            if (Input.GetKeyDown(KeyCode.Space))
            {
                _drawMode = _drawMode switch
                {
                    DrawMode.MultiDrawIndirect => DrawMode.ProceduralIndirectLoop,
                    DrawMode.ProceduralIndirectLoop => DrawMode.RenderPrimitivesIndexedIndirect,
                    _ => DrawMode.MultiDrawIndirect
                };
            }
        }

        private void OnBeginCameraRendering(ScriptableRenderContext context, Camera camera)
        {
            if (_bufferManager == null || _bufferManager.ArgsBuffer == null || _material == null) return;

            if (_drawMode == DrawMode.RenderPrimitivesIndexedIndirect)
            {
                var rp = new RenderParams(_material)
                {
                    worldBounds = new Bounds(Vector3.zero, 10000f * Vector3.one),
                    matProps = _bufferManager.MPB,
                    shadowCastingMode = ShadowCastingMode.Off,
                    receiveShadows = false,
                    reflectionProbeUsage = ReflectionProbeUsage.Off,
                    lightProbeUsage = LightProbeUsage.Off,
                };
                
                UnityEngine.Graphics.RenderPrimitivesIndexedIndirect(
                    rp,
                    MeshTopology.Triangles,
                    _bufferManager.IndexBuffer,
                    _bufferManager.ArgsBuffer,
                    _bufferManager.DrawCount,
                    0);
            }
            else if (camera.GetUniversalAdditionalCameraData().scriptableRenderer is UniversalRenderer urpRenderer)
            {
                _mdiRenderPass.SetRenderData(
                    _bufferManager.IndexBuffer,
                    _bufferManager.ArgsBuffer,
                    _material,
                    _bufferManager.MPB,
                    _bufferManager.DrawCount,
                    _drawMode);

                urpRenderer.EnqueuePass(_mdiRenderPass);
            }
        }
    }
}
