using System;
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
    /// Native Multi-Draw Indirect plugin bridge (D3D11, D3D12, Vulkan).
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
        }

        // Native imports
        [DllImport(DLL_NAME)] private static extern int MDI_AllocSlot();
        [DllImport(DLL_NAME)] private static extern int MDI_GetBaseEventID();
        [DllImport(DLL_NAME)] private static extern IntPtr MDI_GetRenderEventAndDataFunc();
        [DllImport(DLL_NAME)] private static extern int MDI_IsSupported();
        [DllImport(DLL_NAME)] private static extern int MDI_IsD3D12();

        private static IntPtr _renderEventAndDataFunc;
        private static bool _initialized;
        private static bool _supported;
        private static int _baseEventID;

        // Pinned ring buffer for IssuePluginEventAndData — stable pointers for render thread
        private static NativeArray<NativeMDIParams> _paramsRing;

        // dummy args buffer (instanceCount=0) for zero-pixel prime draw
        private static GraphicsBuffer _dummyArgsBuffer;

        public static bool IsSupported
        {
            get
            {
                EnsureInitialized();
                return _supported;
            }
        }

        private static void EnsureInitialized()
        {
            if (_initialized) return;
            _initialized = true;

            try
            {
                _supported = MDI_IsSupported() != 0;
                if (_supported)
                {
                    _baseEventID = MDI_GetBaseEventID();
                    _renderEventAndDataFunc = MDI_GetRenderEventAndDataFunc();
                    _paramsRing = new NativeArray<NativeMDIParams>(MAX_PENDING, Allocator.Persistent);

                    _dummyArgsBuffer = new GraphicsBuffer(
                        GraphicsBuffer.Target.IndirectArguments, 1,
                        GraphicsBuffer.IndirectDrawIndexedArgs.size);
                    _dummyArgsBuffer.SetData(new GraphicsBuffer.IndirectDrawIndexedArgs[1]);

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
            _initialized = false;
        }

        // Write params to pinned ring buffer, return stable pointer for render thread
        private static unsafe IntPtr WriteParams(
            GraphicsBuffer bufferWithArgs,
            GraphicsBuffer indexBuffer,
            int argsStartIndex,
            int argsCount,
            out int slot)
        {
            slot = MDI_AllocSlot();

            _paramsRing[slot] = new NativeMDIParams
            {
                argsBuffer = bufferWithArgs.GetNativeBufferPtr(),
                indexBuffer = indexBuffer.GetNativeBufferPtr(),
                argsOffsetBytes = (uint)(argsStartIndex * INDIRECT_DRAW_INDEXED_ARGS_SIZE),
                maxDrawCount = (uint)argsCount,
                indexFormat = 1 // R32_UINT
            };

            return (IntPtr)((NativeMDIParams*)_paramsRing.GetUnsafeReadOnlyPtr() + slot);
        }

        // -----------------------------------------------------------------------
        // CommandBuffer extension
        // -----------------------------------------------------------------------
        public static void DrawProceduralIndirectMDI(
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
                // Dummy prime draw (instanceCount=0) — binds PSO, render targets, shaders.
                // Renders zero pixels. Plugin handles ALL draws.
                cmd.DrawProceduralIndirect(
                    indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                    shaderPass: shaderPass, topology: topology, bufferWithArgs: _dummyArgsBuffer,
                    argsOffset: 0, properties: properties);

                IntPtr dataPtr = WriteParams(bufferWithArgs, indexBuffer, argsStartIndex, argsCount, out int slot);
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
        public static void DrawProceduralIndirectMDI(
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
                cmd.DrawProceduralIndirect(
                    indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                    shaderPass: shaderPass, topology: topology, bufferWithArgs: _dummyArgsBuffer,
                    argsOffset: 0, properties: properties);

                IntPtr dataPtr = WriteParams(bufferWithArgs, indexBuffer, argsStartIndex, argsCount, out int slot);
                cmd.IssuePluginEventAndData(_renderEventAndDataFunc, _baseEventID + slot, dataPtr);
            }
            else
            {
                // ProceduralIndirectLoop in case of no MDI support
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
        public static void DrawProceduralIndirectMDI(
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
                cmd.DrawProceduralIndirect(
                    indexBuffer: indexBuffer, matrix: Matrix4x4.identity, material: material,
                    shaderPass: shaderPass, topology: topology, bufferWithArgs: _dummyArgsBuffer,
                    argsOffset: 0, properties: properties);

                IntPtr dataPtr = WriteParams(bufferWithArgs, indexBuffer, argsStartIndex, argsCount, out int slot);
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
    }
}
