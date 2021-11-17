#define NUM_LIGHTS 3

//=====================================================================================================================
struct LightState
{
    float3 position;
    float3 direction;
    float4 color;
    float4 falloff;
    float4x4 view;
    float4x4 projection;
};

//=====================================================================================================================
struct SceneConstants
{
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float4 ambientColor;
    bool sampleShadowMap;
    LightState lights[NUM_LIGHTS];
};

//=====================================================================================================================
struct GeometryInfo
{
    uint PrimitiveOffset;
    uint IndexStart;
    uint VertexBase;
    uint GeometryIndex;
};

//=====================================================================================================================
struct GlobalConstants
{
    float4 cameraPosition;
    matrix projectionToWorld;
    matrix cameraViewProjInverse;
};

//=====================================================================================================================
struct MeshVertex
{
    float3 position;
    float3 normal;
    float2 uv;
    float3 tangent;
};

//=====================================================================================================================
// Ray payload
struct RayPayload
{
    bool isShadowRay;
    float hitDistance;
};

SamplerState                    Sampler  : register(s0);
RaytracingAccelerationStructure Scene    : register(t0);
RWTexture2D<float4>             Output   : register(u0);
ConstantBuffer<GlobalConstants> GlobalCB : register(b0, space0);
ConstantBuffer<SceneConstants>  SceneCB  : register(b1, space0);
ConstantBuffer<GeometryInfo>    Geometry : register(b0, space1);

StructuredBuffer<MeshVertex> VertexBuffer : register(t1);
StructuredBuffer<uint>       IndexBuffer  : register(t2);

// Local root arguments
Texture2D DiffuseMap : register(t0, space1);
Texture2D NormalMap  : register(t1, space1);

RWByteAddressBuffer Debug : register(u99);

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
    const uint geometryMultiplier = 1;

    TraceRay(Scene,
             RAY_FLAG_CULL_FRONT_FACING_TRIANGLES,
             instanceInclusionMask,
             rayContributionToHitGroupIndex,
             geometryMultiplier,
             0,
             ray,
             payload);
}

//=====================================================================================================================
float3 GetDebugHitColor(in uint seed)
{
    float cr = (((seed + 23) % 11) + 1) / 11.f;
    float cg = (((seed + 16) % 12) + 1) / 12.f;
    float cb = (((seed + 7) % 10) + 1) / 10.f;

    return float3(cr, cg, cb);
}

//=====================================================================================================================
// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

//=====================================================================================================================
float3 InterpolateAttribute(in float3 v0, in float3 v1, in float3 v2, in float3 barycentrics)
{
    return v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
}

//=====================================================================================================================
float2 InterpolateAttribute(in float2 v0, in float2 v1, in float2 v2, in float3 barycentrics)
{
    return v0 * barycentrics.x + v1 * barycentrics.y + v2 * barycentrics.z;
}

//=====================================================================================================================
[shader("miss")]
void Miss(inout RayPayload payload)
{
    if (payload.isShadowRay)
    {
        payload.hitDistance = 0.0f;
    }
    else
    {
        Output[DispatchRaysIndex().xy] = float4(0,0,0,0);
    }
}

//=====================================================================================================================
float3 CalcPerPixelNormal(float2 vTexcoord, float3 vVertNormal, float3 vVertTangent)
{
    // Compute tangent frame.
    vVertNormal = normalize(vVertNormal);
    vVertTangent = normalize(vVertTangent);

    float3 vVertBinormal = normalize(cross(vVertTangent, vVertNormal));
    float3x3 mTangentSpaceToWorldSpace = float3x3(vVertTangent, vVertBinormal, vVertNormal);

    // Compute per-pixel normal.
    float3 vBumpNormal = (float3)NormalMap.SampleLevel(Sampler, vTexcoord, 0.0f);
    vBumpNormal = 2.0f * vBumpNormal - 1.0f;

    return mul(vBumpNormal, mTangentSpaceToWorldSpace);
}

//--------------------------------------------------------------------------------------
// Diffuse lighting calculation, with angle and distance falloff.
//--------------------------------------------------------------------------------------
float4 CalcLightingColor(float3 vLightPos, float3 vLightDir, float4 vLightColor, float4 vFalloffs, float3 vPosWorld, float3 vPerPixelNormal)
{
    float3 vLightToPixelUnNormalized = vPosWorld - vLightPos;

    // Dist falloff = 0 at vFalloffs.x, 1 at vFalloffs.x - vFalloffs.y
    float fDist = length(vLightToPixelUnNormalized);

    float fDistFalloff = saturate((vFalloffs.x - fDist) / vFalloffs.y);

    // Normalize from here on.
    float3 vLightToPixelNormalized = vLightToPixelUnNormalized / fDist;

    // Angle falloff = 0 at vFalloffs.z, 1 at vFalloffs.z - vFalloffs.w
    float fCosAngle = dot(vLightToPixelNormalized, vLightDir / length(vLightDir));
    float fAngleFalloff = saturate((fCosAngle - vFalloffs.z) / vFalloffs.w);

    // Diffuse contribution.
    float fNDotL = saturate(-dot(vLightToPixelNormalized, vPerPixelNormal));

    return vLightColor * fNDotL * fDistFalloff * fAngleFalloff;
}

//=====================================================================================================================
float4 CalcUnshadowedAmountRayTrace(float3 vToLight, float3 vPosWorld)
{
    RayDesc ray;
    ray.Origin    = vPosWorld;
    ray.Direction = normalize(vToLight);
    ray.TMin      = 0.1;
    ray.TMax      = length(vToLight);

    RayPayload shadowPayload;
    shadowPayload.isShadowRay = true;
    shadowPayload.hitDistance = ray.TMax;

    TraceRay(Scene,
             RAY_FLAG_CULL_BACK_FACING_TRIANGLES
             | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
             | RAY_FLAG_FORCE_OPAQUE             // ~skip any hit shaders
             | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, // ~skip closest hit shaders,,
             0xFF,
             0,
             0,
             0,
             ray,
             shadowPayload);

    return (shadowPayload.hitDistance == 0) ? float4(1, 1, 1, 1) : float4(0,0,0,0);
}

//=====================================================================================================================
[shader("closesthit")]
void ClosestHitNoMaterials(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint index = (PrimitiveIndex() + Geometry.PrimitiveOffset) * 3;

    uint3 idx;
    idx.x = IndexBuffer[index + 0];
    idx.y = IndexBuffer[index + 1];
    idx.z = IndexBuffer[index + 2];

    MeshVertex v[3];
    v[0] = VertexBuffer[idx.x + Geometry.VertexBase];
    v[1] = VertexBuffer[idx.y + Geometry.VertexBase];
    v[2] = VertexBuffer[idx.z + Geometry.VertexBase];

    v[0].position = mul(float4(v[0].position, 1.0), SceneCB.model).xyz;
    v[1].position = mul(float4(v[1].position, 1.0), SceneCB.model).xyz;
    v[2].position = mul(float4(v[2].position, 1.0), SceneCB.model).xyz;
    v[0].normal.z *= -1.0f;
    v[1].normal.z *= -1.0f;
    v[2].normal.z *= -1.0f;

    float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);

    MeshVertex hitSurface;
    hitSurface.position = InterpolateAttribute(v[0].position, v[1].position, v[2].position, barycentrics);
    hitSurface.normal   = InterpolateAttribute(v[0].normal, v[1].normal, v[2].normal, barycentrics);
    hitSurface.uv       = InterpolateAttribute(v[0].uv, v[1].uv, v[2].uv, barycentrics);
    hitSurface.tangent  = InterpolateAttribute(v[0].tangent, v[1].tangent, v[2].tangent, barycentrics);

    // Compute per-pixel normal.
    float3 pixelNormal = CalcPerPixelNormal(hitSurface.uv, hitSurface.normal, hitSurface.tangent);

    float4 totalLight = SceneCB.ambientColor;
    for (int i = 0; i < NUM_LIGHTS; i++)
    {
        float4 lightPass = CalcLightingColor(SceneCB.lights[i].position,
                                             SceneCB.lights[i].direction,
                                             SceneCB.lights[i].color,
                                             SceneCB.lights[i].falloff,
                                             HitWorldPosition(),
                                             pixelNormal);
        if (SceneCB.sampleShadowMap && i == 0)
        {
            const float3 vToLight = SceneCB.lights[i].position.xyz - HitWorldPosition();
            lightPass *= CalcUnshadowedAmountRayTrace(vToLight, HitWorldPosition());
        }
        totalLight += lightPass;
    }

    Output[DispatchRaysIndex().xy] = DiffuseMap.SampleLevel(Sampler, hitSurface.uv, 0.0f) * saturate(totalLight);
}