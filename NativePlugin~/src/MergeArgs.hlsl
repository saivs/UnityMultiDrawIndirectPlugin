// Compute shader: transforms standard DrawIndexed args (20 bytes/draw)
// into extended format for ExecuteIndirect with CBV (28 bytes/draw):
//   [D3D12_GPU_VIRTUAL_ADDRESS cbvAddr (8)] [DrawIndexedArgs (20)]

ByteAddressBuffer _Src : register(t0);
RWByteAddressBuffer _Dst : register(u0);

cbuffer Params : register(b0)
{
    uint _DrawCount;
    uint _SrcByteOffset;
    uint _CbvAddrLo;
    uint _CbvAddrHi;
    uint _CbvSlotSize;  // kCBVAlignment = 256
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint i = dtid.x;
    if (i >= _DrawCount) return;

    uint srcBase = _SrcByteOffset + i * 20;
    uint dstBase = i * 28;

    // 64-bit address = base + i * slotSize
    uint offset = i * _CbvSlotSize;
    uint addrLo = _CbvAddrLo + offset;
    uint carry = (addrLo < _CbvAddrLo) ? 1u : 0u;
    uint addrHi = _CbvAddrHi + carry;

    _Dst.Store(dstBase +  0, addrLo);
    _Dst.Store(dstBase +  4, addrHi);
    _Dst.Store(dstBase +  8, _Src.Load(srcBase +  0));
    _Dst.Store(dstBase + 12, _Src.Load(srcBase +  4));
    _Dst.Store(dstBase + 16, _Src.Load(srcBase +  8));
    _Dst.Store(dstBase + 20, _Src.Load(srcBase + 12));
    _Dst.Store(dstBase + 24, _Src.Load(srcBase + 16));
}
