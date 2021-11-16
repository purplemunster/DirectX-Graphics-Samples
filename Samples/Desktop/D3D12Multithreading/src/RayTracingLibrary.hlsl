//=====================================================================================================================
struct GlobalConstants
{
    float4x4 cameraViewProjInverse;
};

//=====================================================================================================================
// Ray payload
struct RayPayload
{
    float4 color;
};

RaytracingAccelerationStructure Scene    : register(t0);
RWTexture2D<float4>             Output   : register(u0);
ConstantBuffer<GlobalConstants> GlobalCB : register(b0, space0);

//=====================================================================================================================
RayDesc GenerateRayDirection(float2 clipSpace)
{
    // R
    float4 d0 = mul(GlobalCB.cameraViewProjInverse, float4(clipSpace, 0, 1));
    d0.xyz /= d0.w;
    float4 d1 = mul(GlobalCB.cameraViewProjInverse, float4(clipSpace, 1, 1));
    d1.xyz /= d1.w;

    RayDesc ray;
    ray.TMin      = 1.0e-4f;
    ray.TMax      = 1.0e+38f;
    ray.Origin    = d0.xyz;
    ray.Direction = normalize((d1 - d0).xyz);

    return ray;
}

//=====================================================================================================================
[shader("raygeneration")]
void PrimaryRayGen()
{
    RayPayload payload = (RayPayload)(0);

    uint2 offset = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    // Screen position for the ray
    float2 fragCoord = (offset.xy + 0.5f) / size.xy;
    float2 clipSpace = float2(2 * fragCoord.x - 1, 1 - 2 * fragCoord.y);

    RayDesc ray = GenerateRayDirection(clipSpace);

    const uint instanceInclusionMask = 0xff;
    const uint rayContributionToHitGroupIndex = 0;
    const uint geometryMultiplier = 0; // single material shader

    TraceRay(Scene,
             RAY_FLAG_NONE,
             instanceInclusionMask,
             rayContributionToHitGroupIndex,
             geometryMultiplier,
             0,
             ray,
             payload);
}

//=====================================================================================================================
float3 GetDebugHitColor()
{
    uint seed = 7;// InstanceIndex() + PrimitiveIndex();

    float cr = (((seed + 23) % 11) + 1) / 11.f;
    float cg = (((seed + 16) % 12) + 1) / 12.f;
    float cb = (((seed + 7) % 10) + 1) / 10.f;

    return float3(cr, cg, cb);
}

//=====================================================================================================================
[shader("miss")]
void Miss(inout RayPayload payload)
{
    const float3 backgroundColor = float3(0.501960814f, 0.000000000f, 0.501960814f);
    Output[DispatchRaysIndex().xy] = float4(backgroundColor, 1.0f);
}

//=====================================================================================================================
[shader("closesthit")]
void ClosestHitNoMaterials(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    Output[DispatchRaysIndex().xy] = float4(GetDebugHitColor(), 1.0);
}