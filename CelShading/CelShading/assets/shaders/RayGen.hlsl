// ─────────────────────────────────────────────────────────────────────────────
//  RayGen.hlsl
//  レイ生成シェーダー
//  各ピクセルに対してプライマリレイを生成し、TLASに向けて発射する。
// ─────────────────────────────────────────────────────────────────────────────

// グローバルルートシグネチャと対応するバインディング
RWTexture2D<float4>           g_Output : register(u0); // 出力画像
RaytracingAccelerationStructure g_TLAS : register(t0); // TLAS

cbuffer RayTracingCB : register(b0)
{
    float4x4 g_ViewInv;     // ビュー逆行列
    float4x4 g_ProjInv;     // プロジェクション逆行列
    float3   g_LightDir;    // 平行光源の方向（ワールド空間）
    float    g_Padding;
};

// ペイロード: ClosestHit / Miss が書き込む色
struct RayPayload
{
    float4 Color;
};

[shader("raygeneration")]
void RayGen()
{
    uint2  launchIndex = DispatchRaysIndex().xy;
    uint2  launchDim   = DispatchRaysDimensions().xy;

    // NDC 座標 [-1, 1] に変換（Y 軸は反転）
    float2 ndc = ((float2(launchIndex) + 0.5f) / float2(launchDim)) * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    // レイの始点・方向をビュー空間 → ワールド空間に変換
    float4 rayOriginView    = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float4 rayTargetView    = mul(g_ProjInv, float4(ndc, 1.0f, 1.0f));
    rayTargetView.xyz      /= rayTargetView.w;

    float3 rayOriginWorld   = mul(g_ViewInv, rayOriginView).xyz;
    float3 rayTargetWorld   = mul(g_ViewInv, float4(rayTargetView.xyz, 1.0f)).xyz;
    float3 rayDirection     = normalize(rayTargetWorld - rayOriginWorld);

    // レイ構造体の設定
    RayDesc ray;
    ray.Origin    = rayOriginWorld;
    ray.Direction = rayDirection;
    ray.TMin      = 0.001f;
    ray.TMax      = 1000.0f;

    RayPayload payload;
    payload.Color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    // 背面カリング: レイに対して裏向きの三角形を無視する。
    // このモデルには、外側の面とは別に、裏返した内張りの面（スカートのチェック柄など）がある。
    // 裏向きの面は、背面カリングが既定のUnityでは描かれない。
    // カリングしないと、内張りが手前に出たり、外側の面と競合してノイズになる。
    // 表面（外側）が欠けて見えるなど、副作用が出る場合は 0 にする。
#ifndef CULL_BACK_FACES
#define CULL_BACK_FACES 1
#endif
#if CULL_BACK_FACES
    const uint rayFlags = RAY_FLAG_CULL_BACK_FACING_TRIANGLES;
#else
    const uint rayFlags = RAY_FLAG_NONE;
#endif

    TraceRay(
        g_TLAS,
        rayFlags,
        0xFF,               // InstanceMask: 全インスタンス対象
        0,                  // HitGroupIndex
        1,                  // HitGroupStride
        0,                  // MissIndex
        ray,
        payload);

    g_Output[launchIndex] = payload.Color;
}
