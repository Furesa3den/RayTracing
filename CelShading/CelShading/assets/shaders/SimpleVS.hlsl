cbuffer Transform : register(b0)
{
    float4x4 World; // ワールド行列
    float4x4 View; // ビュー行列
    float4x4 Proj; // 投影行列
}

struct VSInput
{
    float3 pos : POSITION; // 頂点座標
    float3 normal : NORMAL; // 法線
    float2 uv : TEXCOORD; // UV
    float3 tangent : TANGENT; // 接空間
    float4 color : COLOR; // 頂点色
};

struct VSOutput
{
    float4 svpos : SV_Position; // 変換された座標
    float4 color : COLOR; // 変換された色
};

VSOutput vert(VSInput input)
{
    VSOutput output = (VSOutput) 0; // アウトプット構造体を定義
    
    float4 localPos = float4(input.pos, 1.0f); // 頂点座標
    float4 worldPos = mul(World, localPos); // ワールド座標に変換
    float4 viewPos = mul(View, worldPos); // ビュー座標に変換
    float4 projPos = mul(Proj, viewPos); // 投影変換
    
    output.svpos = projPos; // 投影変換された座標をピクセルシェーダーに渡す
    output.color = input.color; // 頂点色をそのままピクセルシェーダーに渡す
    
    return output;
}