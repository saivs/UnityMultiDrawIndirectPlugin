using UnityEngine;
using UnityEngine.Rendering;
using Saivs.Graphics.Core.MDI;
using UnityEngine.Rendering.Universal;

/// <summary>
/// Test MonoBehaviour for the Multi-Draw Indirect native plugin.
/// Creates a grid of procedural quads, each dispatched as a separate draw command in a single MDI call.
/// All draws reference a single shared quad (4 vertices) — draw index is derived from baseVertexIndex.
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

    private const int VERTICES_PER_QUAD = 4;

    private GraphicsBuffer _indexBuffer;
    private GraphicsBuffer _vertexBuffer;
    private GraphicsBuffer _drawPositionsBuffer;
    private GraphicsBuffer _argsBuffer;
    private MaterialPropertyBlock _mpb;
    private int _drawCount;
    private MDIRenderPass _mdiRenderPass;

    private void OnEnable()
    {
        Application.targetFrameRate = -1;
        QualitySettings.vSyncCount = 0;
        _mdiRenderPass = new MDIRenderPass();
        _mdiRenderPass.SetCallback(RenderMDI);
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

        // Shared quad: 4 vertices in local space (used by ALL draws)
        var quadVertices = new Vector3[]
        {
            new Vector3(-0.5f, -0.5f, 0f),
            new Vector3( 0.5f, -0.5f, 0f),
            new Vector3( 0.5f,  0.5f, 0f),
            new Vector3(-0.5f,  0.5f, 0f),
        };
        _vertexBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, VERTICES_PER_QUAD, sizeof(float) * 3);
        _vertexBuffer.SetData(quadVertices);

        // Shared index buffer: 6 indices for one quad
        var indices = new int[] { 0, 1, 2, 0, 2, 3 };
        _indexBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Index, 6, sizeof(int));
        _indexBuffer.SetData(indices);

        // Per-draw world positions
        var positions = new Vector3[_drawCount];
        for (int z = 0; z < _gridSize; z++)
        {
            for (int x = 0; x < _gridSize; x++)
            {
                int i = z * _gridSize + x;
                positions[i] = new Vector3(
                    (x - _gridSize / 2f + 0.5f) * _spacing,
                    0f,
                    (z - _gridSize / 2f + 0.5f) * _spacing);
            }
        }

        _drawPositionsBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, _drawCount, sizeof(float) * 3);
        _drawPositionsBuffer.SetData(positions);

        // Indirect args: all draws share the same 6 indices and 4 vertices
        var args = new GraphicsBuffer.IndirectDrawIndexedArgs[_drawCount / 2];
        
        for (int i = 0; i < _drawCount / 2; i++)
        {
            args[i] = new GraphicsBuffer.IndirectDrawIndexedArgs
            {
                indexCountPerInstance = 6,
                instanceCount = 2,
                startIndex = 0,
                baseVertexIndex = 0,
                startInstance = (uint)(i * 2)
            };
        }

        _argsBuffer = new GraphicsBuffer(
            GraphicsBuffer.Target.IndirectArguments | GraphicsBuffer.Target.Structured,
            _drawCount / 2,
            GraphicsBuffer.IndirectDrawIndexedArgs.size);
        _argsBuffer.SetData(args);

        _drawCount = _drawCount / 2;

        _mpb = new MaterialPropertyBlock();
        _mpb.SetBuffer("_VertexBuffer", _vertexBuffer);
        _mpb.SetBuffer("_DrawPositions", _drawPositionsBuffer);
        _mpb.SetBuffer("_ArgsBuffer", _argsBuffer);
    }

    private void DisposeBuffers()
    {
        _indexBuffer?.Dispose();
        _vertexBuffer?.Dispose();
        _drawPositionsBuffer?.Dispose();
        _argsBuffer?.Dispose();

        _indexBuffer = null;
        _vertexBuffer = null;
        _drawPositionsBuffer = null;
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
            urpRenderer.EnqueuePass(_mdiRenderPass);
        }
    }
    private float _fps;
    private float _fpsTimer;
    private int _fpsFrames;

    private void Update()
    {
        _fpsFrames++;
        _fpsTimer += Time.unscaledDeltaTime;
        if (_fpsTimer >= 0.5f)
        {
            _fps = _fpsFrames / _fpsTimer;
            _fpsFrames = 0;
            _fpsTimer = 0f;
        }

        if (Input.GetKeyDown(KeyCode.Space))
        {
            if(_drawMode == DrawMode.MultiDrawIndirect)
            {
                _drawMode = DrawMode.ProceduralIndirectLoop;
            }
            else if(_drawMode == DrawMode.ProceduralIndirectLoop)
            {
                _drawMode = DrawMode.RenderPrimitivesIndexedIndirect;
            }
            else if(_drawMode == DrawMode.RenderPrimitivesIndexedIndirect)
            {
                _drawMode = DrawMode.MultiDrawIndirect;
            }
        }
    }

    private static readonly int DrawCallIndexID = Shader.PropertyToID("_DrawCallIndex");

    private void RenderMDI(RasterCommandBuffer commandBuffer)
    {
        if (_argsBuffer == null || _material == null) return;

        if (_drawMode == DrawMode.MultiDrawIndirect)
        {
            // Pass 1: MDI — native plugin sets cbuffer b7 per draw
            commandBuffer.MultiDrawIndexedIndirect(
                indexBuffer: _indexBuffer,
                material: _material,
                properties: _mpb,
                shaderPass: 1,
                topology: MeshTopology.Triangles,
                bufferWithArgs: _argsBuffer,
                argsStartIndex: 0,
                argsCount: _drawCount);
        }
        else
        {
            // Pass 2: ProceduralLoop — set _DrawCallIndex before each draw
            for (int i = 0; i < _drawCount; i++)
            {
                commandBuffer.SetGlobalInt(DrawCallIndexID, i);
                commandBuffer.DrawProceduralIndirect(
                    indexBuffer: _indexBuffer,
                    matrix: Matrix4x4.identity,
                    material: _material,
                    shaderPass: 2,
                    topology: MeshTopology.Triangles,
                    bufferWithArgs: _argsBuffer,
                    argsOffset: i * GraphicsBuffer.IndirectDrawIndexedArgs.size,
                    properties: _mpb);
            }
        }
    }

    private static GUIStyle _guiStyle;

    private void OnGUI()
    {
        if (_guiStyle == null)
        {
            _guiStyle = new GUIStyle(GUI.skin.label);
            _guiStyle.fontStyle = FontStyle.Bold;
            _guiStyle.fontSize = 16;
        }

        _guiStyle.normal.textColor = (_drawMode == DrawMode.MultiDrawIndirect) ? new Color(0f, 0.9f, 0.1f) : new Color(0.9f, 0f, 0.1f);

        GUILayout.BeginArea(new Rect(10, 10, 400, 200));
        GUILayout.Label($"FPS: {_fps:F1}  ({1000f / Mathf.Max(_fps, 0.001f):F2} ms)", _guiStyle);
        GUILayout.Label($"MDI Plugin Supported: {MultiDrawIndirect.IsSupported}", _guiStyle);
        GUILayout.Label($"Draw Mode: {_drawMode}", _guiStyle);
        GUILayout.Label($"(Space to switch)", _guiStyle);
        GUILayout.Label($"Draw Commands: {_drawCount}", _guiStyle);
        GUILayout.Label($"Graphics API: {SystemInfo.graphicsDeviceType}", _guiStyle);
        GUILayout.EndArea();
    }
}
