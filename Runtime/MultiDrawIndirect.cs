using System;
using System.Reflection;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;
using UnityEngine;
using UnityEngine.Rendering;

#if UNITY_6000_0_OR_NEWER
using UnityEngine.Rendering.RenderGraphModule;
#endif

namespace Saivs.Graphics.Core.MDI
{
    /// <summary>
    /// Native Multi-Draw Indirect plugin bridge (D3D11, D3D12, Vulkan, OpenGLES, OpenGL).
    ///
    /// Flow (identical for all APIs):
    /// 1. DrawProceduralIndirect with dummy args (instanceCount=0) — binds PSO, render targets, shaders.
    /// 2. IssuePluginEventAndData — plugin receives params via pinned ring buffer pointer.
    ///    - D3D11: NvAPI hardware MDI or native DrawIndexedInstancedIndirect loop.
    ///    - D3D12: ExecuteIndirect on Unity's command list via CommandRecordingState.
    ///    - Vulkan: vkCmdDrawIndexedIndirect (multi-draw if supported, loop fallback otherwise).
    /// </summary>
    public static class MultiDrawIndirect
    {
        private const string DLL_NAME = "GfxPluginMDI";
        private const int INDIRECT_DRAW_INDEXED_ARGS_SIZE = 20; // 5 * sizeof(uint)
        private const int MAX_PENDING = 256;

        // Must match native MDIParams layout (two pointers + three uint32)
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeMDIParams
        {
            public IntPtr argsBuffer;
            public IntPtr indexBuffer;
            public uint argsOffsetBytes;
            public uint maxDrawCount;
            public uint indexFormat;
            public uint topology;
        }

        // Native imports
        [DllImport(DLL_NAME)] private static extern int MDI_AllocSlot();
        [DllImport(DLL_NAME)] private static extern int MDI_GetBaseEventID();
        [DllImport(DLL_NAME)] private static extern IntPtr MDI_GetRenderEventAndDataFunc();
        [DllImport(DLL_NAME)] private static extern int MDI_IsSupported();
        [DllImport(DLL_NAME)] private static extern int MDI_UsesPerInstanceVB();
        [DllImport(DLL_NAME)] private static extern int MDI_SetMaxInstanceCount(uint maxCount);
        [DllImport(DLL_NAME)] private static extern uint MDI_GetMaxInstanceCount();
        [DllImport(DLL_NAME)] private static extern void MDI_SetDummyArgsBuffer(IntPtr nativePtr);
        [DllImport(DLL_NAME)] private static extern void MDI_SetParamsRing(IntPtr basePtr);
        [DllImport(DLL_NAME)] private static extern void MDI_SetDrawIndexBuffer(IntPtr nativePtr);

        private static IntPtr _renderEventAndDataFunc;
        private static bool _initialized;
        private static bool _supported;
        private static int _baseEventID;

        // Pinned ring buffer for IssuePluginEventAndData — stable pointers for render thread
        private static NativeArray<NativeMDIParams> _paramsRing;

        // dummy args buffer (instanceCount=0) for zero-pixel prime draw
        private static GraphicsBuffer _dummyArgsBuffer;

        // Per-draw index buffer used by the Metal backend. Holds [0, 1, …, MAX_PENDING-1]
        // as uint32; the native swizzle re-binds it with offset = i*4 between each
        // indirect draw, so the shader's `_MDI_DrawIndex_Buffer[0]` reads `i`.
        // No-op on other platforms.
        private static GraphicsBuffer _drawIndexBuffer;
        private static readonly int s_DrawIndexBufferID = Shader.PropertyToID("_MDI_DrawIndex_Buffer");

        public static bool IsSupported
        {
            get
            {
                EnsureInitialized();
                return _supported;
            }
        }

        /// <summary>
        /// Maximum number of instances that can be addressed by the per-instance identity buffer
        /// on D3D11/D3D12. For any draw command, <c>startInstance + instanceCount</c> must not exceed
        /// this value. Default is 65,536. Returns 0 on APIs that don't use the identity buffer
        /// (Vulkan, OpenGL, Metal).
        /// </summary>
        public static uint MaxInstanceCount
        {
            get
            {
                EnsureInitialized();
                if (!_supported) return 0;
                try { return MDI_GetMaxInstanceCount(); }
                catch { return 0; }
            }
            set
            {
                EnsureInitialized();
                if (!_supported) return;
                try
                {
                    if (MDI_SetMaxInstanceCount(value) == 0)
                        Debug.LogError($"[MDI] Failed to resize identity buffer to {value}.");
                    else
                        Debug.Log($"[MDI] Identity buffer resized to {value} entries.");
                }
                catch (Exception e)
                {
                    Debug.LogError($"[MDI] Failed to resize identity buffer: {e.Message}");
                }
            }
        }

        private static void EnsureInitialized()
        {
            if (_initialized) return;
            _initialized = true;

            try
            {
                _supported = MDI_IsSupported() != 0;

                _primeMeshes = new Dictionary<MeshTopology, Mesh>();

                if (_supported)
                {
                    _baseEventID = MDI_GetBaseEventID();
                    _renderEventAndDataFunc = MDI_GetRenderEventAndDataFunc();
                    _paramsRing = new NativeArray<NativeMDIParams>(MAX_PENDING, Allocator.Persistent);

                    // MAX_PENDING entries so we can encode the ring-buffer slot
                    // into argsOffset for each prime draw — the Metal backend
                    // recovers the slot in its method-swizzling hook.
                    _dummyArgsBuffer = new GraphicsBuffer(
                        GraphicsBuffer.Target.IndirectArguments, MAX_PENDING,
                        GraphicsBuffer.IndirectDrawIndexedArgs.size);
                    _dummyArgsBuffer.SetData(new GraphicsBuffer.IndirectDrawIndexedArgs[MAX_PENDING]);

                    // Per-draw-index buffer for the Metal backend. Pre-fill with
                    // [0, 1, …, MAX_PENDING-1]; the native swizzle re-binds with
                    // offset = i*4 between each draw inside the prime.
                    var drawIndices = new uint[MAX_PENDING];
                    for (int i = 0; i < MAX_PENDING; i++) drawIndices[i] = (uint)i;
                    _drawIndexBuffer = new GraphicsBuffer(
                        GraphicsBuffer.Target.Structured, MAX_PENDING, sizeof(uint));
                    _drawIndexBuffer.SetData(drawIndices);
                    Shader.SetGlobalBuffer(s_DrawIndexBufferID, _drawIndexBuffer);

                    try
                    {
                        MDI_SetDummyArgsBuffer(_dummyArgsBuffer.GetNativeBufferPtr());
                        MDI_SetDrawIndexBuffer(_drawIndexBuffer.GetNativeBufferPtr());
                        unsafe { MDI_SetParamsRing((IntPtr)_paramsRing.GetUnsafeReadOnlyPtr()); }
                    }
                    catch (EntryPointNotFoundException) { /* older native plugin — ignore */ }

                    var api = SystemInfo.graphicsDeviceType;
                    Debug.Log($"[MDI] Initialized: {api}, baseEventID={_baseEventID}");
                }
            }
            catch (DllNotFoundException)
            {
                _supported = false;
                Debug.LogWarning("[MDI] GfxPluginMDI native plugin not found. Falling back to DrawProceduralIndirect loop.");
            }
            catch (EntryPointNotFoundException)
            {
                _supported = false;
                Debug.LogWarning("[MDI] GfxPluginMDI native plugin is outdated. Falling back to DrawProceduralIndirect loop.");
            }
            catch (Exception e)
            {
                _supported = false;
                Debug.LogError($"[MDI] Native plugin initialization failed: {e.Message}. Falling back to DrawProceduralIndirect loop.");
            }
        }

        public static void Dispose()
        {
            if (_paramsRing.IsCreated)
                _paramsRing.Dispose();
            _dummyArgsBuffer?.Dispose();
            _dummyArgsBuffer = null;
            _drawIndexBuffer?.Dispose();
            _drawIndexBuffer = null;

            foreach (var pair in _primeMeshes)
                UnityEngine.Object.DestroyImmediate(pair.Value);

            _primeMeshes.Clear();

            _initialized = false;
        }

        // Write params to pinned ring buffer, return stable pointer for render thread
        private static unsafe IntPtr WriteParams(
            GraphicsBuffer bufferWithArgs,
            GraphicsBuffer indexBuffer,
            int argsStartIndex,
            int argsCount,
            MeshTopology topology,
            uint indexFormat,
            out int slot)
        {
            slot = MDI_AllocSlot();

            _paramsRing[slot] = new NativeMDIParams
            {
                argsBuffer = bufferWithArgs.GetNativeBufferPtr(),
                indexBuffer = indexBuffer.GetNativeBufferPtr(),
                argsOffsetBytes = (uint)(argsStartIndex * INDIRECT_DRAW_INDEXED_ARGS_SIZE),
                maxDrawCount = (uint)argsCount,
                indexFormat = indexFormat,
                topology = (uint)topology,
            };

            return (IntPtr)((NativeMDIParams*)_paramsRing.GetUnsafeReadOnlyPtr() + slot);
        }

        // Index format codes — match Metal MTLIndexType / native plugin's expectations.
        // 0 = UInt16, 1 = UInt32.
        private static uint EncodeIndexFormat(IndexFormat fmt)
            => fmt == IndexFormat.UInt16 ? 0u : 1u;

        // -----------------------------------------------------------------------
        // CommandBuffer extension
        // -----------------------------------------------------------------------
        private static bool _usesPerInstanceVB;
        private static bool _perInstanceVBChecked;
        private static Dictionary<MeshTopology, Mesh> _primeMeshes;

        private static bool UsesPerInstanceVB
        {
            get
            {
                if (!_perInstanceVBChecked)
                {
                    _perInstanceVBChecked = true;
                    try { _usesPerInstanceVB = _supported && MDI_UsesPerInstanceVB() != 0; }
                    catch { _usesPerInstanceVB = false; }
                }
                return _usesPerInstanceVB;
            }
        }

        // Create a minimal mesh whose vertex layout includes TEXCOORD7.
        // When Unity renders this mesh, it creates a PSO with TEXCOORD7
        // in the input layout — our native hook then modifies it to be
        // per-instance on VB slot 15.
        private static Mesh GetPrimeMesh(MeshTopology topology)
        {
            if (_primeMeshes.TryGetValue(topology, out var mesh))
                return mesh;

            mesh = new Mesh {
                name = $"MDI_PrimeMesh_{topology}",
                hideFlags = HideFlags.HideAndDontSave
            };

            mesh.SetVertexBufferParams(3,
                new VertexAttributeDescriptor(VertexAttribute.Position, VertexAttributeFormat.Float32, 3, stream: 0),
                new VertexAttributeDescriptor(VertexAttribute.TexCoord7, VertexAttributeFormat.UInt32, 1, stream: 1)
            );

            mesh.SetVertexBufferData(new Vector3[3], 0, 0, 3, stream: 0);
            mesh.SetVertexBufferData(new uint[3], 0, 0, 3, stream: 1);

            // Set the indices based on the requested topology
            switch (topology)
            {
                case MeshTopology.Lines:
                    mesh.SetIndices(new int[] { 0, 1 }, MeshTopology.Lines, 0);
                    break;
                case MeshTopology.Points:
                    mesh.SetIndices(new int[] { 0 }, MeshTopology.Points, 0);
                    break;
                case MeshTopology.Triangles:
                default:
                    mesh.SetIndices(new int[] { 0, 1, 2 }, MeshTopology.Triangles, 0);
                    break;
            }

            mesh.bounds = new Bounds(Vector3.zero, Vector3.one * 10000);

            _primeMeshes[topology] = mesh;

            return mesh;
        }

        // -----------------------------------------------------------------------
        // Mesh-based MDI helpers
        // -----------------------------------------------------------------------

        // Cached HashSet of mesh instance IDs whose `indexBufferTarget` we've
        // already ensured includes Raw, so `mesh.GetIndexBuffer()` returns a
        // GPU-readable buffer the native plugin can address.
        private static readonly HashSet<int> _meshesPrepared = new HashSet<int>();
        private static bool _meshFallbackWarned;

        // True when the current backend's MDI.hlsl branch routes the global
        // instance ID through SV_InstanceID — i.e. Metal (per-draw bytes),
        // Vulkan and WebGPU (firstInstance baked in by the indirect args).
        // These backends accept arbitrary user meshes because the shader has
        // no TEXCOORD7 input requirement. On D3D11/D3D12/OpenGL the shader
        // expects TEXCOORD7 in the vertex input layout — without it the
        // mesh path can't render correctly without mesh cloning.
        private static bool MeshApiSupportedNatively
        {
            get
            {
                var api = SystemInfo.graphicsDeviceType;
                return api == GraphicsDeviceType.Metal
                    || api == GraphicsDeviceType.Vulkan
                    || api == GraphicsDeviceType.WebGPU;
            }
        }

        private static GraphicsBuffer EnsureMeshIndexBuffer(Mesh mesh)
        {
            int id = mesh.GetInstanceID();
            if (!_meshesPrepared.Contains(id))
            {
                mesh.indexBufferTarget |= GraphicsBuffer.Target.Raw;
                _meshesPrepared.Add(id);
            }
            return mesh.GetIndexBuffer();
        }

        private static void WarnMeshFallbackOnce()
        {
            if (_meshFallbackWarned) return;
            _meshFallbackWarned = true;
            Debug.LogWarning(
                $"[MDI] MultiDrawMeshIndirect: true MDI is currently supported only on " +
                $"Metal, Vulkan and WebGPU. Current backend ({SystemInfo.graphicsDeviceType}) " +
                $"falls back to a per-draw DrawMeshInstancedIndirect loop, and the user shader " +
                $"must not require TEXCOORD7 in its vertex input layout (i.e. should not include " +
                $"the default MDI_INSTANCE_ID_PARAMETER macro from MDI.hlsl on these APIs).");
        }

        public static void MultiDrawIndexedIndirect(
            this CommandBuffer cmd,
            GraphicsBuffer indexBuffer,
            Material material,
            MaterialPropertyBlock properties,
            int shaderPass,
            MeshTopology topology,
            GraphicsBuffer bufferWithArgs,
            int argsStartIndex,
            int argsCount)
        {
            EnsureInitialized();

            if (_supported && argsCount > 1)
            {
                // Stage params into the ring buffer FIRST so we know which slot
                // this draw owns. The slot is encoded in the prime's argsOffset
                // — the Metal backend reads it back from inside its
                // drawIndexedPrimitives swizzle hook.
                IntPtr dataPtr = WriteParams(bufferWithArgs, indexBuffer, argsStartIndex, argsCount, topology, indexFormat: 1, out int slot);

                // Prime draw: sets PSO + render state on the command list.
                // On D3D11/D3D12, use a Mesh with TEXCOORD7 to force a PSO whose
                // input layout includes TEXCOORD7 (our hook patches it to
                // per-instance on VB slot 15). Zero-area triangle = no pixels.
                if (UsesPerInstanceVB)
                    cmd.DrawMesh(GetPrimeMesh(topology), Matrix4x4.identity, material, 0, shaderPass, properties);
                else
                    cmd.DrawProceduralIndirect(
                        indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                        shaderPass: shaderPass, topology: topology, bufferWithArgs: _dummyArgsBuffer,
                        argsOffset: slot * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties: properties);

                cmd.IssuePluginEventAndData(_renderEventAndDataFunc, _baseEventID + slot, dataPtr);
            }
            else
            {
                for (int i = 0; i < argsCount; i++)
                {
                    cmd.DrawProceduralIndirect(
                        indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                        shaderPass: shaderPass, topology: topology, bufferWithArgs: bufferWithArgs,
                        argsOffset: (argsStartIndex + i) * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties: properties);
                }
            }
        }

#if UNITY_6000_0_OR_NEWER
        // -----------------------------------------------------------------------
        // RasterCommandBuffer extension
        // -----------------------------------------------------------------------
        public static void MultiDrawIndexedIndirect(
            this RasterCommandBuffer cmd,
            GraphicsBuffer indexBuffer,
            Material material,
            MaterialPropertyBlock properties,
            int shaderPass,
            MeshTopology topology,
            GraphicsBuffer bufferWithArgs,
            int argsStartIndex,
            int argsCount)
        {
            EnsureInitialized();

            if (_supported && argsCount > 1)
            {
                IntPtr dataPtr = WriteParams(bufferWithArgs, indexBuffer, argsStartIndex, argsCount, topology, indexFormat: 1, out int slot);

                if (UsesPerInstanceVB)
                    cmd.DrawMesh(GetPrimeMesh(topology), Matrix4x4.identity, material, 0, shaderPass, properties);
                else
                    cmd.DrawProceduralIndirect(
                        indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                        shaderPass: shaderPass, topology: topology, bufferWithArgs: _dummyArgsBuffer,
                        argsOffset: slot * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties: properties);

                cmd.IssuePluginEventAndData(_renderEventAndDataFunc, _baseEventID + slot, dataPtr);
            }
            else
            {
                for (int i = 0; i < argsCount; i++)
                {
                    cmd.DrawProceduralIndirect(
                        indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                        shaderPass: shaderPass, topology: topology, bufferWithArgs: bufferWithArgs,
                        argsOffset: (argsStartIndex + i) * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties: properties);
                }
            }
        }

        // -----------------------------------------------------------------------
        // UnsafeCommandBuffer extension
        // -----------------------------------------------------------------------
        public static void MultiDrawIndexedIndirect(
            this UnsafeCommandBuffer cmd,
            GraphicsBuffer indexBuffer,
            Material material,
            MaterialPropertyBlock properties,
            int shaderPass,
            MeshTopology topology,
            GraphicsBuffer bufferWithArgs,
            int argsStartIndex,
            int argsCount)
        {
            EnsureInitialized();

            if (_supported && argsCount > 1)
            {
                IntPtr dataPtr = WriteParams(bufferWithArgs, indexBuffer, argsStartIndex, argsCount, topology, indexFormat: 1, out int slot);

                if (UsesPerInstanceVB)
                    cmd.DrawMesh(GetPrimeMesh(topology), Matrix4x4.identity, material, 0, shaderPass, properties);
                else
                    cmd.DrawProceduralIndirect(
                        indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                        shaderPass: shaderPass, topology: topology, bufferWithArgs: _dummyArgsBuffer,
                        argsOffset: slot * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties: properties);

                cmd.IssuePluginEventAndData(_renderEventAndDataFunc, _baseEventID + slot, dataPtr);
            }
            else
            {
                for (int i = 0; i < argsCount; i++)
                {
                    cmd.DrawProceduralIndirect(
                        indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                        shaderPass: shaderPass, topology: topology, bufferWithArgs: bufferWithArgs,
                        argsOffset: (argsStartIndex + i) * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties: properties);
                }
            }
        }
#endif

        // -----------------------------------------------------------------------
        // Mesh-based MDI: vertices come from the user's Mesh via the input
        // assembler — shaders can use the standard POSITION / NORMAL /
        // TEXCOORD0 semantics instead of pulling from a StructuredBuffer.
        //
        // True MDI is currently routed to the native plugin only on backends
        // whose MDI.hlsl branch resolves the global instance ID through
        // SV_InstanceID — that's Metal, Vulkan and WebGPU. On D3D11/D3D12
        // and OpenGL the shader expects a TEXCOORD7 input element which the
        // user mesh doesn't provide; for those backends we fall back to a
        // per-draw DrawMeshInstancedIndirect loop and emit a warning.
        // -----------------------------------------------------------------------

        public static void MultiDrawMeshIndirect(
            this CommandBuffer cmd,
            Mesh mesh,
            Material material,
            MaterialPropertyBlock properties,
            int shaderPass,
            GraphicsBuffer bufferWithArgs,
            int argsStartIndex,
            int argsCount)
        {
            EnsureInitialized();

            if (_supported && argsCount > 1 && MeshApiSupportedNatively)
            {
                var meshIndexBuffer = EnsureMeshIndexBuffer(mesh);
                IntPtr dataPtr = WriteParams(
                    bufferWithArgs, meshIndexBuffer, argsStartIndex, argsCount,
                    mesh.GetTopology(0),
                    EncodeIndexFormat(mesh.indexFormat),
                    out int slot);

                cmd.DrawMeshInstancedIndirect(mesh, 0, material, shaderPass,
                    _dummyArgsBuffer, slot * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties);

                cmd.IssuePluginEventAndData(_renderEventAndDataFunc, _baseEventID + slot, dataPtr);
            }
            else
            {
                if (_supported && !MeshApiSupportedNatively) WarnMeshFallbackOnce();
                for (int i = 0; i < argsCount; i++)
                {
                    cmd.DrawMeshInstancedIndirect(mesh, 0, material, shaderPass,
                        bufferWithArgs, (argsStartIndex + i) * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties);
                }
            }
        }

#if UNITY_6000_0_OR_NEWER
        public static void MultiDrawMeshIndirect(
            this RasterCommandBuffer cmd,
            Mesh mesh,
            Material material,
            MaterialPropertyBlock properties,
            int shaderPass,
            GraphicsBuffer bufferWithArgs,
            int argsStartIndex,
            int argsCount)
        {
            EnsureInitialized();

            if (_supported && argsCount > 1 && MeshApiSupportedNatively)
            {
                var meshIndexBuffer = EnsureMeshIndexBuffer(mesh);
                IntPtr dataPtr = WriteParams(
                    bufferWithArgs, meshIndexBuffer, argsStartIndex, argsCount,
                    mesh.GetTopology(0),
                    EncodeIndexFormat(mesh.indexFormat),
                    out int slot);

                cmd.DrawMeshInstancedIndirect(mesh, 0, material, shaderPass,
                    _dummyArgsBuffer, slot * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties);

                cmd.IssuePluginEventAndData(_renderEventAndDataFunc, _baseEventID + slot, dataPtr);
            }
            else
            {
                if (_supported && !MeshApiSupportedNatively) WarnMeshFallbackOnce();
                for (int i = 0; i < argsCount; i++)
                {
                    cmd.DrawMeshInstancedIndirect(mesh, 0, material, shaderPass,
                        bufferWithArgs, (argsStartIndex + i) * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties);
                }
            }
        }

        public static void MultiDrawMeshIndirect(
            this UnsafeCommandBuffer cmd,
            Mesh mesh,
            Material material,
            MaterialPropertyBlock properties,
            int shaderPass,
            GraphicsBuffer bufferWithArgs,
            int argsStartIndex,
            int argsCount)
        {
            EnsureInitialized();

            if (_supported && argsCount > 1 && MeshApiSupportedNatively)
            {
                var meshIndexBuffer = EnsureMeshIndexBuffer(mesh);
                IntPtr dataPtr = WriteParams(
                    bufferWithArgs, meshIndexBuffer, argsStartIndex, argsCount,
                    mesh.GetTopology(0),
                    EncodeIndexFormat(mesh.indexFormat),
                    out int slot);

                cmd.DrawMeshInstancedIndirect(mesh, 0, material, shaderPass,
                    _dummyArgsBuffer, slot * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties);

                cmd.IssuePluginEventAndData(_renderEventAndDataFunc, _baseEventID + slot, dataPtr);
            }
            else
            {
                if (_supported && !MeshApiSupportedNatively) WarnMeshFallbackOnce();
                for (int i = 0; i < argsCount; i++)
                {
                    cmd.DrawMeshInstancedIndirect(mesh, 0, material, shaderPass,
                        bufferWithArgs, (argsStartIndex + i) * INDIRECT_DRAW_INDEXED_ARGS_SIZE, properties);
                }
            }
        }
#endif
    }
}
