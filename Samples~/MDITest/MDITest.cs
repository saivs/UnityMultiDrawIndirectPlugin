using UnityEngine;
using UnityEngine.Rendering;
using Saivs.Graphics.Core.MDI;
using UnityEngine.Rendering.Universal;

/// <summary>
/// Test MonoBehaviour for the Multi-Draw Indirect native plugin.
/// Creates a grid of procedural quads, each dispatched as a separate draw command in a single MDI call.
/// Attach to any GameObject in the scene.
/// </summary>
[ExecuteInEditMode]
public class MDITest : MonoBehaviour
{
    public enum DrawMode
    {
        MultiDrawIndirect,
        ProceduralIndirectLoop,
        RenderPrimitivesIndexedIndirect
    }

    [Header("Grid Settings")]
    [SerializeField] private int _gridSize = 8;
    [SerializeField] private float _spacing = 1.5f;

    [Header("Rendering")]
    [SerializeField] private Material _material;
    [SerializeField] private DrawMode _drawMode = DrawMode.MultiDrawIndirect;

    private GraphicsBuffer _indexBuffer;
    private GraphicsBuffer _vertexBuffer;
    private GraphicsBuffer _argsBuffer;
    private MaterialPropertyBlock _mpb;
    private int _drawCount;

    private void OnEnable()
    {
        RenderPipelineManager.beginCameraRendering += OnBeginCameraRendering;
        CreateBuffers();
    }

    private void OnDisable()
    {
        RenderPipelineManager.beginCameraRendering -= OnBeginCameraRendering;
        DisposeBuffers();
    }

    private void CreateBuffers()
    {
        _drawCount = _gridSize * _gridSize;
        if (_drawCount == 0) return;

        // Base quad offsets (4 verts per quad)
        var quadOffsets = new Vector3[]
        {
            new Vector3(-0.5f, -0.5f, 0f),
            new Vector3( 0.5f, -0.5f, 0f),
            new Vector3( 0.5f,  0.5f, 0f),
            new Vector3(-0.5f,  0.5f, 0f),
        };

        // Bake world position into vertices: 4 vertices per draw command.
        var vertices = new Vector3[4 * _drawCount];
        for (int z = 0; z < _gridSize; z++)
        {
            for (int x = 0; x < _gridSize; x++)
            {
                int i = z * _gridSize + x;
                var offset = new Vector3(
                    (x - _gridSize / 2f + 0.5f) * _spacing,
                    0f,
                    (z - _gridSize / 2f + 0.5f) * _spacing);

                for (int v = 0; v < 4; v++)
                    vertices[i * 4 + v] = quadOffsets[v] + offset;
            }
        }
        _vertexBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, 4 * _drawCount, sizeof(float) * 3);
        _vertexBuffer.SetData(vertices);

        // Index buffer: 6 indices per draw, pre-offset to point at correct vertices.
        var indices = new int[6 * _drawCount];
        for (int d = 0; d < _drawCount; d++)
        {
            int vBase = d * 4;
            int iBase = d * 6;
            indices[iBase + 0] = vBase + 0;
            indices[iBase + 1] = vBase + 1;
            indices[iBase + 2] = vBase + 2;
            indices[iBase + 3] = vBase + 0;
            indices[iBase + 4] = vBase + 2;
            indices[iBase + 5] = vBase + 3;
        }
        _indexBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Index, 6 * _drawCount, sizeof(int));
        _indexBuffer.SetData(indices);

        // Indirect args: each draw uses startIndex to pick its 6 indices from the fat index buffer.
        var args = new GraphicsBuffer.IndirectDrawIndexedArgs[_drawCount];
        for (int i = 0; i < _drawCount; i++)
        {
            args[i] = new GraphicsBuffer.IndirectDrawIndexedArgs
            {
                indexCountPerInstance = 6,
                instanceCount = (uint)(Random.Range(0f,1f) > 0.5f ? 1 : 0),
                startIndex = (uint)(i * 6),
                baseVertexIndex = 0,
                startInstance = 0
            };
        }
        _argsBuffer = new GraphicsBuffer(GraphicsBuffer.Target.IndirectArguments, _drawCount,
            GraphicsBuffer.IndirectDrawIndexedArgs.size);
        _argsBuffer.SetData(args);

        _mpb = new MaterialPropertyBlock();
        _mpb.SetBuffer("_VertexBuffer", _vertexBuffer);
    }

    private void DisposeBuffers()
    {
        _indexBuffer?.Dispose();
        _vertexBuffer?.Dispose();
        _argsBuffer?.Dispose();

        _indexBuffer = null;
        _vertexBuffer = null;
        _argsBuffer = null;
    }

    private void OnBeginCameraRendering(ScriptableRenderContext context, Camera camera)
    {
        if (_argsBuffer == null || _material == null) return;

        if (_drawMode == DrawMode.RenderPrimitivesIndexedIndirect)
        {
            var rp = new RenderParams(_material)
            {
                worldBounds = new Bounds(Vector3.zero, 10000f * Vector3.one),
                matProps = _mpb,
                shadowCastingMode = ShadowCastingMode.Off,
                receiveShadows = false,
                reflectionProbeUsage = ReflectionProbeUsage.Off,
                lightProbeUsage = LightProbeUsage.Off,
            };
            Graphics.RenderPrimitivesIndexedIndirect(
                rp,
                MeshTopology.Triangles,
                _indexBuffer,
                _argsBuffer,
                _drawCount,
                0);
        }
        else if (camera.GetUniversalAdditionalCameraData().scriptableRenderer is UniversalRenderer urpRenderer)
        {
            urpRenderer.RenderOpaqueForwardPass.EnqueueRenderObjects(RenderMDI);
        }
    }

    private void RenderMDI(RasterCommandBuffer commandBuffer)
    {
        if (_argsBuffer == null || _material == null) return;

        if (_drawMode == DrawMode.MultiDrawIndirect)
        {
            commandBuffer.DrawProceduralIndirectMDI(
                indexBuffer: _indexBuffer,
                material: _material,
                properties: _mpb,
                shaderPass: 0,
                topology: MeshTopology.Triangles,
                bufferWithArgs: _argsBuffer,
                argsStartIndex: 0,
                argsCount: _drawCount);
        }
        else
        {
            for (int i = 0; i < _drawCount; i++)
            {
                commandBuffer.DrawProceduralIndirect(
                    indexBuffer: _indexBuffer,
                    matrix: Matrix4x4.identity,
                    material: _material,
                    shaderPass: 0,
                    topology: MeshTopology.Triangles,
                    bufferWithArgs: _argsBuffer,
                    argsOffset: i * GraphicsBuffer.IndirectDrawIndexedArgs.size,
                    properties: _mpb);
            }
        }
    }

    private void OnGUI()
    {
        GUILayout.BeginArea(new Rect(10, 10, 350, 140));
        GUILayout.Label($"MDI Plugin Supported: {MultiDrawIndirect.IsSupported}");
        GUILayout.Label($"Draw Mode: {_drawMode}");
        GUILayout.Label($"Draw Commands: {_drawCount}");
        GUILayout.Label($"Graphics API: {SystemInfo.graphicsDeviceType}");
        GUILayout.EndArea();
    }
}
