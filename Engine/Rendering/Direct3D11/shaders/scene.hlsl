cbuffer Draw : register(b0) {
    row_major float4x4 World;
    row_major float4x4 ViewProjection;
    float4 Tint; // RGB color, W = unlit.
};
struct Vertex {
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
};
Vertex VS(float3 position : POSITION) {
    Vertex result;
    float4 world = mul(World, float4(position, 1));
    result.position = mul(ViewProjection, world);
    result.worldPosition = world.xyz;
    return result;
}
float4 PS(Vertex input) : SV_TARGET {
    float brightness = 1;
    if (Tint.w < 0.5) {
        float3 normal = normalize(cross(ddx(input.worldPosition), ddy(input.worldPosition)));
        brightness = 0.25 + 0.75 * saturate(dot(normal, normalize(float3(-0.5, 1, -0.4))));
    }
    return float4(Tint.rgb * brightness, 1);
}
