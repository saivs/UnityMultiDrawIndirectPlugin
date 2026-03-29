using UnityEngine;

namespace Saivs.Graphics.Test
{
    [ExecuteInEditMode]
    public class MDITestBufferManager : MonoBehaviour
    {
        [Header("Grid Settings")]
        [SerializeField] private int _gridSize = 8;
        [SerializeField] private float _spacing = 1.5f;

        private const int VERTICES_PER_QUAD = 4;

        private GraphicsBuffer _indexBuffer;
        private GraphicsBuffer _vertexBuffer;
        private GraphicsBuffer _drawPositionsBuffer;
        private GraphicsBuffer _argsBuffer;
        private MaterialPropertyBlock _mpb;
        private int _drawCount;

        public GraphicsBuffer IndexBuffer => _indexBuffer;
        public GraphicsBuffer ArgsBuffer => _argsBuffer;
        public MaterialPropertyBlock MPB => _mpb;
        public int DrawCount => _drawCount;

        private void OnEnable()
        {
            CreateBuffers();
        }

        private void OnDisable()
        {
            DisposeBuffers();
        }

        private void CreateBuffers()
        {
            _drawCount = _gridSize * _gridSize;
            if (_drawCount == 0) return;

            var quadVertices = new Vector3[]
            {
                new Vector3(-0.5f, -0.5f, 0f),
                new Vector3( 0.5f, -0.5f, 0f),
                new Vector3( 0.5f,  0.5f, 0f),
                new Vector3(-0.5f,  0.5f, 0f),
            };
            _vertexBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, VERTICES_PER_QUAD, sizeof(float) * 3);
            _vertexBuffer.SetData(quadVertices);

            var indices = new int[] { 0, 1, 2, 0, 2, 3 };
            _indexBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Index, 6, sizeof(int));
            _indexBuffer.SetData(indices);

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
    }
}