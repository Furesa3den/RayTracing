// ─────────────────────────────────────────────────────────────────────────────
//  ClosestHit.hlsl
//  レイが三角形に当たったときに呼ばれる。
//  ヒットした三角形の3頂点を頂点/インデックスバッファから読み、
//  バリセントリクスで UV と法線を補間してテクスチャ付きの拡散照明を計算する。
// ─────────────────────────────────────────────────────────────────────────────

// ── グローバルルートシグネチャ ─────────────────────────────────────────────
cbuffer RayTracingCB : register(b0)
{
    float4x4 g_ViewInv;
    float4x4 g_ProjInv;
    float3   g_LightDir; // 光の進行方向（ワールド空間）
    float    g_Padding;
};

// CPU 側 Vertex 構造体（60 byte）と同じレイアウト
struct Vertex
{
    float3 Position;
    float3 Normal;
    float2 UV;
    float3 Tangent;
    float4 Color;
};

// ── ローカルルートシグネチャ（メッシュごとに異なる） ────────────────────────
// 連続した3ディスクリプタ: t1=テクスチャ, t2=頂点バッファ, t3=インデックスバッファ
Texture2D                g_DiffuseTex : register(t1, space1);
StructuredBuffer<Vertex> g_Vertices   : register(t2, space1);
ByteAddressBuffer        g_Indices    : register(t3, space1);
SamplerState             g_Sampler    : register(s0, space1);

struct RayPayload
{
    float4 Color;
};

[shader("closesthit")]
void ClosestHit(inout RayPayload payload,
                in BuiltInTriangleIntersectionAttributes attr)
{
    // ── ヒットした三角形の3頂点を取得 ───────────────────────────────────────
    // インデックスは uint32。1三角形 = 3 * 4 byte
    uint3 idx = g_Indices.Load3(PrimitiveIndex() * 3 * 4);

    Vertex v0 = g_Vertices[idx.x];
    Vertex v1 = g_Vertices[idx.y];
    Vertex v2 = g_Vertices[idx.z];

    // ── バリセントリクスで補間 ──────────────────────────────────────────────
    float3 bary = float3(
        1.0f - attr.barycentrics.x - attr.barycentrics.y,
        attr.barycentrics.x,
        attr.barycentrics.y);

    float2 uv           = v0.UV * bary.x + v1.UV * bary.y + v2.UV * bary.z;
    float3 objectNormal = v0.Normal * bary.x + v1.Normal * bary.y + v2.Normal * bary.z;

    // オブジェクト空間の法線 → ワールド空間（逆転置行列 = WorldToObject を右から掛ける）
    float3 normal = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));

    // ── ランバート拡散 + アンビエント ───────────────────────────────────────
    float3 toLight = normalize(-g_LightDir);
    float  NdotL   = max(dot(normal, toLight), 0.0f);
    float  lighting = 0.4f + 0.6f * NdotL;

    // ── テクスチャサンプリング ───────────────────────────────────────────────
    float4 texColor = g_DiffuseTex.SampleLevel(g_Sampler, uv, 0);

    payload.Color = float4(texColor.rgb * lighting, 1.0f);
}
