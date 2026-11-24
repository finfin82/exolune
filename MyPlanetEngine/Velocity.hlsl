// Motion vectors at render resolution: previous-minus-current pixel motion, from scene distance
// (composite alpha, km) and the unjittered reprojection matrix, matching ToneMap.hlsl.
cbuffer VelocityFrame : register(b0)
{
    float4x4 reproject;
    float4   camForward;
    float4   camRight;
    float4   camUp;
    float4   sizeNear; // xy: render size in pixels, z: near plane (m), w: unused
};

Texture2D<float> depth : register(t0);
Texture2D<float4> composite : register(t1);
SamplerState linearClamp : register(s0);

struct Output
{
    float2 velocity     : SV_Target0;
    float  composition  : SV_Target1;
};

Output PSMain(float4 position : SV_POSITION)
{
    const float2 renderSize = sizeNear.xy;
    const int2 pixel = int2(position.xy);
    const float2 uvCurr = (float2(pixel) + 0.5) / renderSize;
    const float2 ndc = float2(2.0 * uvCurr.x - 1.0, 1.0 - 2.0 * uvCurr.y);
    const float3 view = normalize(camForward.xyz + ndc.x * camRight.w * camRight.xyz + ndc.y * camUp.w * camUp.xyz);
    const float4 current = composite.Load(int3(pixel, 0));
    const float deviceZ = depth.Load(int3(pixel, 0));
    const float viewZDepth = sizeNear.z / max(deviceZ, 1.0e-7);
    const float along = current.a * 1000.0;
    const float viewZColor = along * max(dot(view, camForward.xyz), 1.0e-6);

    float4 seen;
    if (current.a > 0.0)
        seen = float4(view * along, 1.0);
    else if (current.a < 0.0)
        seen = float4(view * (-along), 1.0);
    else
        seen = float4(view, 0.0);

    const float4 previous = mul(seen, reproject);
    float2 velocity = 0.0;
    if (previous.w > 0.0)
    {
        const float2 uvPrev = previous.xy / previous.w * float2(0.5, -0.5) + 0.5;
        velocity = (uvPrev - uvCurr) * renderSize;
    }

    float composition = 0.0;
    if (current.a < 0.0)
        composition = 1.0; // sea surface, self-moving
    else if (current.a == 0.0)
        composition = 0.15; // sky / volume without a hard surface
    else if (deviceZ > 1.0e-6 && viewZColor + 2.0 < viewZDepth)
        composition = 0.85; // closer than the depth buffer: clouds over terrain

    Output output;
    output.velocity = velocity;
    output.composition = composition;
    return output;
}
