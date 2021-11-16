//=====================================================================================================================
struct GlobalConstants
{
    float4 cameraPosition;
    matrix projectionToWorld;
    matrix cameraViewProjInverse;
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
// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline RayDesc GenerateCameraRay(uint2 index, in float3 cameraPosition, in float4x4 projectionToWorld)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a world positon.
    float4 world = mul(float4(screenPos, 0, 1), projectionToWorld);
    world.xyz /= world.w;

    RayDesc ray;
    ray.Origin    = cameraPosition;
    ray.Direction = normalize(world.xyz - ray.Origin);
    ray.TMin      = 1.0e-4f;
    ray.TMax      = 1.0e+38f;

    return ray;
}

//=====================================================================================================================
[shader("raygeneration")]
void PrimaryRayGen()
{
    RayPayload payload = (RayPayload)(0);

    // Screen position for the ray
    float2 fragCoord = (DispatchRaysIndex().xy + 0.5f) / DispatchRaysDimensions().xy;
    float2 clipSpace = float2(2 * fragCoord.x - 1, 1 - 2 * fragCoord.y);

    RayDesc ray = GenerateCameraRay(DispatchRaysIndex().xy, GlobalCB.cameraPosition.xyz, GlobalCB.projectionToWorld);

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
    uint seed = InstanceIndex() + PrimitiveIndex();

    float cr = (((seed + 23) % 11) + 1) / 11.f;
    float cg = (((seed + 16) % 12) + 1) / 12.f;
    float cb = (((seed + 7) % 10) + 1) / 10.f;

    return float3(cr, cg, cb);
}

//=====================================================================================================================
[shader("miss")]
void Miss(inout RayPayload payload)
{
    const float3 backgroundColor = float3(0, 0, 0);// float3(0.501960814f, 0.000000000f, 0.501960814f);
    Output[DispatchRaysIndex().xy] = float4(backgroundColor, 1.0f);
}

//=====================================================================================================================
[shader("closesthit")]
void ClosestHitNoMaterials(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    Output[DispatchRaysIndex().xy] = float4(GetDebugHitColor() * attr.barycentrics.xy, 1.0, 1.0);
}