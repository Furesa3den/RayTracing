// ─────────────────────────────────────────────────────────────────────────────
//  Miss.hlsl
//  ミスシェーダー
//  レイがどのジオメトリにも当たらなかった場合に呼ばれる。
//  空の色（グラデーション）をペイロードに書き込む。
// ─────────────────────────────────────────────────────────────────────────────

struct RayPayload
{
    float4 Color;
};

[shader("miss")]
void Miss(inout RayPayload payload)
{
    // 下から上へ薄いグレー → 薄いブルーのグラデーション
    float t = WorldRayDirection().y * 0.5f + 0.5f;
    float3 sky = lerp(float3(0.5f, 0.5f, 0.5f), float3(0.3f, 0.5f, 0.8f), t);
    payload.Color = float4(sky, 1.0f);
}
