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

// ── 診断用の表示切り替え ─────────────────────────────────────────────────────
//   0: 通常表示
//   1: メッシュごとの色分け（メッシュの重なり・割り当ての確認）
//   2: ray cone の LOD を白黒で表示（LOD 推定が効いているかの確認）
// 変更したら ClosestHit.cso の再生成（ビルド）が必要
#ifndef DEBUG_VIEW
#define DEBUG_VIEW 2
#endif

// メッシュ番号から区別しやすい色を作る（黄金比で色相をずらす）
float3 MeshColor(uint id)
{
    float h = frac(id * 0.61803398875f);
    return saturate(abs(frac(h + float3(1.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f);
}

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

    // ── テクスチャ LOD の推定（レイ円錐 / ray cone） ─────────────────────────
    // レイトレーシングでは、ラスタライズのように隣接ピクセルとの UV 差分（ddx/ddy）から
    // ミップ段階を自動で決められない。そのため Sample ではなく SampleLevel を使い、
    // ray cone によって LOD を推定する。
    // 参考: Akenine-Möller et al., "Texture Level of Detail Strategies for Real-Time Ray Tracing",
    //       Ray Tracing Gems, Chapter 20.
    //
    // ray cone は、カメラのピクセル1つ分の広がりを円錐として表したもの。
    // この円錐がヒット位置で表面に作る足跡（フットプリント）の大きさを、テクセル単位で求め、
    // その log2 を LOD とする。
    //   LOD = Δ + log2(円錐の幅 / |n・d|)
    //   Δ   = 0.5 * log2(三角形の UV 面積[テクセル²] / 三角形のワールド面積)
    // ※ 1次レイ（カメラから出るレイ）のみを対象とする簡易版。
    //   反射・屈折で円錐を伝播させる処理は、再帰深度が1のため行わない。

    // 三角形の3頂点をワールド空間へ変換
    float3 p0 = mul(ObjectToWorld3x4(), float4(v0.Position, 1.0f));
    float3 p1 = mul(ObjectToWorld3x4(), float4(v1.Position, 1.0f));
    float3 p2 = mul(ObjectToWorld3x4(), float4(v2.Position, 1.0f));

    // ワールド空間の三角形面積（の2倍）と幾何法線
    float3 faceCross     = cross(p1 - p0, p2 - p0);
    float  worldArea2    = length(faceCross);
    float3 geoNormal     = faceCross / max(worldArea2, 1e-20f);

    // テクスチャ空間の三角形面積（の2倍）[テクセル²]
    uint texWidth, texHeight;
    g_DiffuseTex.GetDimensions(texWidth, texHeight);
    float2 uvEdge1    = v1.UV - v0.UV;
    float2 uvEdge2    = v2.UV - v0.UV;
    float  uvArea2    = abs(uvEdge1.x * uvEdge2.y - uvEdge1.y * uvEdge2.x) * (float)texWidth * (float)texHeight;

    // Δ: ワールド空間の単位長さあたりのテクセル数（の log2）。面積比を平方根にして長さの比にする
    float delta = 0.5f * log2(max(uvArea2, 1e-20f) / max(worldArea2, 1e-20f));

    // ピクセル1つ分の円錐の広がり角。投影行列の逆行列から視野角を取り出す
    //   g_ProjInv[1][1] = tan(fovY / 2)、画面の高さ = DispatchRaysDimensions().y
    float tanHalfFovY = g_ProjInv[1][1];
    float spreadAngle = atan(2.0f * tanHalfFovY / (float)DispatchRaysDimensions().y);

    // ヒット位置での円錐の幅（カメラは1点なので、レイの出発点での幅は 0）
    float coneWidth = spreadAngle * RayTCurrent();

    // 斜めから見るほど足跡は表面上で引き伸ばされる。|n・d| で割って補正する。
    // 0 に近いと LOD が発散するため、下限を設ける（斜めの面が過度にぼけるのを防ぐ）
    float cosTheta = max(abs(dot(geoNormal, WorldRayDirection())), 0.05f);

    float lod = delta + log2(max(coneWidth / cosTheta, 1e-20f));
    lod = max(lod, 0.0f); // 拡大時（負の LOD）は、最も細かいミップ0を使う

    // オブジェクト空間の法線 → ワールド空間（逆転置行列 = WorldToObject を右から掛ける）
    float3 normal = normalize(mul(objectNormal, (float3x3)WorldToObject3x4()));

    // ── ランバート拡散 + アンビエント ───────────────────────────────────────
    float3 toLight = normalize(-g_LightDir);
    float  NdotL   = max(dot(normal, toLight), 0.0f);
    float  lighting = 0.4f + 0.6f * NdotL;

    // ── テクスチャサンプリング ───────────────────────────────────────────────
#if DEBUG_VIEW == 1
    // メッシュ番号ごとに色分けする。2つのメッシュが重なっていると、境界が細かく入り混じって見える
    payload.Color = float4(MeshColor(InstanceID()), 1.0f);
    return;
#elif DEBUG_VIEW == 2
    // ray cone で求めた LOD を白黒で表示する（LOD 8 で白）。遠い・斜めの面ほど明るくなる
    payload.Color = float4((lod / 8.0f).xxx, 1.0f);
    return;
#endif

    // ray cone で推定した LOD を指定してサンプリングする
    float4 texColor = g_DiffuseTex.SampleLevel(g_Sampler, uv, lod);

    payload.Color = float4(texColor.rgb * lighting, 1.0f);
}
