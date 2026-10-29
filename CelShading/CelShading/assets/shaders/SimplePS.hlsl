struct VSOutput
{
    float4 svpos : SV_Position; // 頂点シェーダーから来た座標
    float4 color : COLOR; // 頂点シェーダーから来た色
};

float4 pixel(VSOutput input) : SV_Target
{
    return input.color; // 色をそのまま表示
}