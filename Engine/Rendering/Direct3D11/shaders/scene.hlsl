cbuffer View : register(b0) {
    row_major float4x4 ViewProjection;
};
StructuredBuffer<float4> ClipPlanes : register(t1);
struct Instance {
    float3 position : POSITION;
    float4 world0 : WORLD0;
    float4 world1 : WORLD1;
    float4 world2 : WORLD2;
    float4 world3 : WORLD3;
    float4 tint : COLOR0; // RGB, W = unlit.
    float4 rect : RECT0; // XY origin, width/height; zero for ordinary meshes.
    uint4 ink : INK0; // Font bits, then clipping plane offset/count.
    float quadZ : QUADZ0;
};
struct Vertex {
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 localPosition : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation float4 tint : COLOR0;
    nointerpolation uint4 ink : INK0;
};
Vertex VS(Instance input) {
    Vertex result;
    float3 local = input.position;
    if (input.rect.z > 0 && input.rect.w > 0)
        local = float3(input.rect.xy + input.position.xy * input.rect.zw, input.quadZ);
    float4x4 worldMatrix = float4x4(input.world0, input.world1, input.world2, input.world3);
    float4 world = mul(worldMatrix, float4(local, 1));
    result.position = mul(ViewProjection, world);
    result.worldPosition = world.xyz;
    result.localPosition = local;
    result.uv = input.position.xy;
    result.tint = input.tint;
    result.ink = input.ink;
    return result;
}
float4 PS(Vertex input) : SV_TARGET {
    if ((input.ink.y & 0x80000000u) != 0) {
        // Choose the top/left-inclusive cell consistently at exact pixel boundaries.
        uint column = min(4u, (uint)max(0.0, floor(input.uv.x * 5 + 0.0001)));
        uint row = min(6u, (uint)max(0.0, floor((1 - input.uv.y) * 7 + 0.0001)));
        uint bit = row * 5 + 4 - column;
        uint word = bit < 32 ? input.ink.x : (input.ink.y & 0x7fffffffu);
        clip(float((word >> (bit & 31)) & 1u) - 0.5);
    }
    for (uint i = 0; i < input.ink.w; ++i)
        clip(dot(ClipPlanes[input.ink.z + i], float4(input.localPosition, 1)));
    float brightness = 1;
    if (input.tint.w < 0.5) {
        float3 normal = normalize(cross(ddx(input.worldPosition), ddy(input.worldPosition)));
        brightness = 0.25 + 0.75 * saturate(dot(normal, normalize(float3(-0.5, 1, -0.4))));
    }
    return float4(input.tint.rgb * brightness, 1);
}
