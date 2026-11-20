#pragma once
#include "Scene/Scene.h"
#include "Core/Engine.h"
#include "Objects/Objects.h"
#include "Graphics/SharedStruct.h"
#include "Graphics/ConstantBuffer.h"
#include "Graphics/DescriptorHeap.h"
#include "Core/ComPtr.h"
#include <d3d12.h>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  シェーダーテーブルの各レコードに書き込む定数（ローカルルートシグネチャ用）
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(256) RayTracingCB {
    DirectX::XMMATRIX ViewInv;   // ビュー逆行列
    DirectX::XMMATRIX ProjInv;   // プロジェクション逆行列
    DirectX::XMFLOAT3 LightDir;  // 平行光源の方向（ワールド空間）
    float             Padding;
};

// ─────────────────────────────────────────────────────────────────────────────
//  DXR（DirectX Raytracing）を用いたシーン
//
//  Init() でBLAS/TLAS/シェーダーテーブル等を構築し、
//  Draw() で DispatchRays を発行してUAVへ書き込んだ後
//  スワップチェインのRTVにコピーします。
// ─────────────────────────────────────────────────────────────────────────────
class RayTracingScene : public Scene {
public:
    bool Init()   override;
    void Update() override;
    void Draw()   override;

private:
    // ── モデルデータ ──────────────────────────────────────────────────────────
    // 表示するモデル数
    static constexpr UINT kModelCount = 2;
    Objects::Model* m_models[kModelCount] = {};
    // 各モデルの先頭メッシュの通し番号（BLAS・ヒットグループ・ディスクリプタ共通のインデックス）
    UINT m_meshBase[kModelCount] = {};
    // 全モデルの合計メッシュ数
    UINT m_totalMeshes = 0;

    // ── カメラ / 定数バッファ ─────────────────────────────────────────────────
    ConstantBuffer* m_rayTracingCB[Engine::FRAME_BUFFER_COUNT] = {};
    float           m_rotateY = 0.0f;

    // ── アクセラレーション構造 ────────────────────────────────────────────────
    // BLAS: メッシュ1本ごとに1つ
    std::vector<ComPtr<ID3D12Resource>> m_blas;
    // TLAS: シーン全体を1つにまとめる
    ComPtr<ID3D12Resource> m_tlas;
    // インスタンスデスク（TLASビルドの入力）
    ComPtr<ID3D12Resource> m_instanceDescBuffer;

    bool BuildBLAS();   // 全メッシュのBLASを構築
    bool BuildTLAS();   // TLASを構築

    // ── レイトレーシングパイプライン ───────────────────────────────────────────
    ComPtr<ID3D12RootSignature> m_globalRootSignature;
    ComPtr<ID3D12StateObject>   m_rtStateObject;
    ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;

    bool CreateRootSignature();
    bool CreateRaytracingPipeline();

    // ── シェーダーテーブル ────────────────────────────────────────────────────
    ComPtr<ID3D12Resource> m_rayGenTable;
    ComPtr<ID3D12Resource> m_missTable;
    ComPtr<ID3D12Resource> m_hitGroupTable;
    UINT64                 m_hitGroupRecordSize = 0; // ローカルRS込みのレコードサイズ

    bool BuildShaderTable();

    // ── 出力バッファ（UAV） ───────────────────────────────────────────────────
    ComPtr<ID3D12Resource>      m_outputBuffer;  // レイトレの書き込み先（UAV）
    D3D12_CPU_DESCRIPTOR_HANDLE m_outputUAVCpuHandle = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_outputUAVGpuHandle = {};

    // DXR用ディスクリプタヒープ（UAV・TLAS SRV・CB を1本に集約）
    ComPtr<ID3D12DescriptorHeap> m_dxrDescHeap;
    UINT                         m_dxrDescIncrementSize = 0;

    // ヒープ内の各スロット（番号）
    // グローバルヒープ: UAV(0), TLAS(1) の固定2枠 + メッシュごとのテクスチャ(2～)
    enum DxrHeapSlot : UINT {
        SLOT_OUTPUT_UAV  = 0,  // 出力画像 UAV
        SLOT_TLAS_SRV    = 1,  // TLAS SRV
        SLOT_TEX_BEGIN   = 2,  // メッシュ用SRVの先頭
        SLOT_PER_MESH    = 3,  // 1メッシュあたり [テクスチャ, 頂点バッファ, インデックスバッファ] の3スロット
    };

    bool CreateOutputBuffer();
    bool CreateDxrDescriptorHeap();
    void UpdateDescriptors();

    // ローカルルートシグネチャ（メッシュごとのテクスチャ参照用）
    ComPtr<ID3D12RootSignature> m_localRootSignature;

    // ── ヘルパー ──────────────────────────────────────────────────────────────
    // GPU上にバッファを確保する汎用ヘルパー
    static ComPtr<ID3D12Resource> CreateBuffer(
        UINT64 size,
        D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES initialState,
        D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT);

    // コマンドを即時実行して完了を待つ（AS構築用）
    void ExecuteAndWait();
};
