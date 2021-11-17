Buffer<float4> VertexBuffer : register(t0);

struct DrawArguments
{
    uint VertexCountPerInstance;
    uint InstanceCount;
    uint StartVertexLocation;
    uint StartInstanceLocation;
};

StructuredBuffer<DrawArguments> DrawArguments : register(t1);

[numthreads(64, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    if (DTid.x < DrawArguments[0].VertexCountPerInstance)
    {
        float3 v0 = VertexBuffer.Load(DTid.x * 36);
        float3 v1 = VertexBuffer.Load(DTid.x * 36 + 12);
        float3 v2 = VertexBuffer.Load(DTid.x * 36 + 24);
    }
    // Fetch vertex data
    // Run vertex shader CallShader();
    // Rasterize triangles
    //
}