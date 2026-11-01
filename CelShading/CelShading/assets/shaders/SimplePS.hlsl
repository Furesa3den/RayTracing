struct VSOutput
{
    float4 svpos : SV_Position; // 頂点シェーダーから来た座標
    float4 color : COLOR; // 頂点シェーダーから来た色
    float2 uv : TEXCOORD; // 頂点シェーダーからUV
};

SamplerState smp : register(s0); // サンプラー
Texture2D _MainTex : register(t0); // テクスチャ

float4 pixel(VSOutput input) : SV_Target
{
    return _MainTex.Sample(smp, input.uv);
}