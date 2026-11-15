// ─────────────────────────────────────────────────────────────────────────────
//  ClosestHit.hlsl
//  最近傍ヒットシェーダー
//  レイがジオメトリに当たったときに呼ばれる。
//  ランバート拡散反射 + アンビエントで色を計算してペイロードに書き込む。
// ─────────────────────────────────────────────────────────────────────────────

cbuffer RayTracingCB : register(b0)
{
    float4x4 g_ViewInv;
    float4x4 g_ProjInv;
    float3   g_LightDir; // ワールド空間での平行光源方向（光源 → シーンへの向き）
    float    g_Padding;
};

struct RayPayload
{
    float4 Color;
};

// ─────────────────────────────────────────────────────────────────────────────
//  NOTE: BuiltInTriangleIntersectionAttributes はDXRシステムが定義済みのため
//        ユーザーが再定義してはいけない。フィールド名は barycentrics（小文字）。
// ─────────────────────────────────────────────────────────────────────────────

[shader("closesthit")]
void ClosestHit(inout RayPayload payload,
                in BuiltInTriangleIntersectionAttributes attr)
{
    // バリセントリクスから重心座標を計算（システム定義のフィールドは小文字）
    float3 barycentrics = float3(
        1.0f - attr.barycentrics.x - attr.barycentrics.y,
        attr.barycentrics.x,
        attr.barycentrics.y);

    // ライトが来る方向（光源→シーンの向きを反転）
    float3 lightDir = normalize(-g_LightDir);

    // ジオメトリ法線をワールド空間で計算する
    // ObjectToWorld4x3 の行からX/Z基底ベクトルを取り出して外積を求める
    float3 worldRight   = float3(ObjectToWorld4x3()[0][0],
                                 ObjectToWorld4x3()[1][0],
                                 ObjectToWorld4x3()[2][0]);
    float3 worldForward = float3(ObjectToWorld4x3()[0][2],
                                 ObjectToWorld4x3()[1][2],
                                 ObjectToWorld4x3()[2][2]);
    float3 geometricNormal = normalize(cross(worldRight, worldForward));

    // ランバート拡散 + アンビエント
    float  NdotL     = max(dot(geometricNormal, lightDir), 0.0f);
    float  ambient   = 0.2f;
    float  diffuse   = NdotL;

    // インスタンス ID に応じて色相を変える（メッシュの識別用）
    float  hue       = frac((float)InstanceID() / 12.0f);
    float3 baseColor = lerp(float3(0.8f, 0.6f, 0.4f),
                            float3(0.4f, 0.6f, 0.8f), hue);

    float3 finalColor = baseColor * (ambient + diffuse);
    payload.Color = float4(finalColor, 1.0f);
}
