Texture2D Frame : register(t0);
SamplerState PointClamp : register(s0);
struct Vertex {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};
Vertex VS(uint id : SV_VertexID) {
    Vertex result;
    result.uv = float2((id << 1) & 2, id & 2);
    result.position = float4(result.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}
float4 PS(Vertex input) : SV_TARGET { return Frame.Sample(PointClamp, input.uv); }
