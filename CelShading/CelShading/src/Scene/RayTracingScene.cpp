#include "Scene/RayTracingScene.h"
#include "Core/App.h"
#include "Objects/AssimpLoader.h"
#include <d3dx12.h>
#include <DirectXMath.h>


using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
//  定数
// ─────────────────────────────────────────────────────────────────────────────
// シェーダーテーブル: 各レコードは ShaderIdentifier(32 byte) を
// D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT(64 byte) にアライン
static constexpr UINT kShaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
static constexpr UINT kShaderRecordAlign    = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

// シェーダーエントリポイント名
static const wchar_t* kRayGenName   = L"RayGen";
static const wchar_t* kMissName     = L"Miss";
static const wchar_t* kHitGroupName = L"HitGroup";
static const wchar_t* kCHitName     = L"ClosestHit";

// コンパイル済みシェーダーのパス
static const wchar_t* kRayGenCso     = L"../x64/Debug/RayGen.cso";
static const wchar_t* kMissCso       = L"../x64/Debug/Miss.cso";
static const wchar_t* kClosestHitCso = L"../x64/Debug/ClosestHit.cso";

// ─────────────────────────────────────────────────────────────────────────────
//  汎用バッファ生成ヘルパー
// ─────────────────────────────────────────────────────────────────────────────
ComPtr<ID3D12Resource> RayTracingScene::CreateBuffer(
    UINT64 size,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState,
    D3D12_HEAP_TYPE heapType)
{
    auto heapProp = CD3DX12_HEAP_PROPERTIES(heapType);
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(size, flags);

    ComPtr<ID3D12Resource> buf;
    g_Engine->Device()->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &desc,
        initialState, nullptr,
        IID_PPV_ARGS(buf.GetAddressOf()));
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Init
// ─────────────────────────────────────────────────────────────────────────────
bool RayTracingScene::Init()
{
    auto device = g_Engine->Device();

    // ── DXR サポート確認 ────────────────────────────────────────────────────
    if (!g_Engine->CheckDXRSupport()) return false;

    // ── モデル読み込み ──────────────────────────────────────────────────────
    m_totalMeshes = 0;
    for (UINT m = 0; m < kModelCount; m++) {
        m_models[m]   = new Objects::Models::Alicia();
        m_meshBase[m] = m_totalMeshes;
        m_totalMeshes += static_cast<UINT>(m_models[m]->GetMeshes().size());
    }

    // テクスチャはDXRヒープ生成後にロードするため、ここではモデルデータのみ

    // ── 定数バッファ ────────────────────────────────────────────────────────
    for (size_t i = 0; i < Engine::FRAME_BUFFER_COUNT; i++) {
        m_rayTracingCB[i] = new ConstantBuffer(sizeof(RayTracingCB));
        if (!m_rayTracingCB[i]->IsValid()) {
            DebugLog("レイトレ用定数バッファの生成に失敗\n");
            return false;
        }
    }

    // ── BLAS 構築 ────────────────────────────────────────────────────────
    // コマンドの記録・実行・完了待ちは Engine::ExecuteImmediate に任せる。
    // ここでは各メッシュの入力情報とバッファを用意し、ビルドコマンドは後でまとめて記録する。
    // geomDescs / blasDescs は ExecuteImmediate の間も生きている必要があるので vector で保持する。
    // BLAS は全モデルのメッシュを通し番号 g で管理する（モデルごとに独立した BLAS を持つ）
    m_blas.resize(m_totalMeshes);
    std::vector<ComPtr<ID3D12Resource>> blasScratch(m_totalMeshes);
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDescs(m_totalMeshes);
    std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> blasDescs(m_totalMeshes);

    for (size_t g = 0; g < m_totalMeshes; g++) {
        // 通し番号 g から、所属モデルとモデル内メッシュ番号を求める
        UINT m = kModelCount - 1;
        while (m > 0 && m_meshBase[m] > g) m--;
        const size_t i = g - m_meshBase[m];
        Objects::Model* model = m_models[m];

        auto& geomDesc = geomDescs[g]; // vector の要素なので、ポインタが ExecuteImmediate まで有効
        geomDesc.Type  = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geomDesc.Triangles.VertexBuffer.StartAddress  = model->GetVertexBuffers()[i]->GetResource()->GetGPUVirtualAddress();
        geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
        geomDesc.Triangles.VertexCount                = static_cast<UINT>(model->GetMeshes()[i].Vertices.size());
        geomDesc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT; // Position は先頭3要素
        geomDesc.Triangles.IndexBuffer                = model->GetIndexBuffers()[i]->GetResource()->GetGPUVirtualAddress();
        geomDesc.Triangles.IndexCount                 = static_cast<UINT>(model->GetMeshes()[i].Indices.size());
        geomDesc.Triangles.IndexFormat                = DXGI_FORMAT_R32_UINT;
        geomDesc.Triangles.Transform3x4               = 0;

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs = {};
        blasInputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        blasInputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        blasInputs.NumDescs       = 1;
        blasInputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
        blasInputs.pGeometryDescs = &geomDesc;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasInfo = {};
        device->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasInfo);

        blasScratch[g] = CreateBuffer(
            blasInfo.ScratchDataSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        m_blas[g] = CreateBuffer(
            blasInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

        auto& blasDesc = blasDescs[g];
        blasDesc.Inputs                           = blasInputs;
        blasDesc.DestAccelerationStructureData    = m_blas[g]->GetGPUVirtualAddress();
        blasDesc.ScratchAccelerationStructureData = blasScratch[g]->GetGPUVirtualAddress();
    }

    // 全メッシュの BLAS ビルドを記録して GPU で実行し、完了まで待つ
    bool blasOk = g_Engine->ExecuteImmediate([&](ID3D12GraphicsCommandList4* cmd) {
        for (size_t i = 0; i < blasDescs.size(); i++) {
            cmd->BuildRaytracingAccelerationStructure(&blasDescs[i], 0, nullptr);

            // BLASビルド完了バリア（TLAS が参照する前に保証）
            D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(m_blas[i].Get());
            cmd->ResourceBarrier(1, &uavBarrier);
        }
    });
    if (!blasOk) {
        DebugLog("BLAS の構築に失敗\n");
        return false;
    }

    // ── TLAS 構築 ────────────────────────────────────────────────────────
    // インスタンスデスクをアップロードバッファに書き込む
    // モデルごとの X 方向オフセット（1体目は左、2体目は右）
    static constexpr float kModelOffsetX[kModelCount] = { -35.0f, 35.0f };

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(m_totalMeshes);
    for (UINT m = 0; m < kModelCount; m++) {
        const UINT meshCount = static_cast<UINT>(m_models[m]->GetMeshes().size());
        for (UINT i = 0; i < meshCount; i++) {
            const UINT g = m_meshBase[m] + i; // 通し番号
            auto& inst = instanceDescs[g];
            memset(&inst, 0, sizeof(inst));
            // 単位行列 + X 方向の平行移動（行優先 3x4）
            inst.Transform[0][0] = 1.0f;
            inst.Transform[1][1] = 1.0f;
            inst.Transform[2][2] = 1.0f;
            inst.Transform[0][3] = kModelOffsetX[m];
            inst.InstanceID                          = i; // モデル内メッシュ番号（デバッグ色を両モデルで揃える）
            inst.InstanceMask                        = 0xFF;
            inst.InstanceContributionToHitGroupIndex = g; // ヒットグループレコードは通し番号で参照
            inst.Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            inst.AccelerationStructure               = m_blas[g]->GetGPUVirtualAddress();
        }
    }

    UINT64 instanceDescSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * m_totalMeshes;
    m_instanceDescBuffer = CreateBuffer(
        instanceDescSize,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_HEAP_TYPE_UPLOAD);

    {
        void* pMapped = nullptr;
        m_instanceDescBuffer->Map(0, nullptr, &pMapped);
        memcpy(pMapped, instanceDescs.data(), static_cast<size_t>(instanceDescSize));
        m_instanceDescBuffer->Unmap(0, nullptr);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs = {};
    tlasInputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInputs.Flags         = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInputs.NumDescs      = m_totalMeshes;
    tlasInputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInputs.InstanceDescs = m_instanceDescBuffer->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasInfo = {};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasInfo);

    ComPtr<ID3D12Resource> tlasScratch = CreateBuffer(
        tlasInfo.ScratchDataSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    m_tlas = CreateBuffer(
        tlasInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc = {};
    tlasDesc.Inputs                           = tlasInputs;
    tlasDesc.DestAccelerationStructureData    = m_tlas->GetGPUVirtualAddress();
    tlasDesc.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress();

    // TLAS ビルドを記録して GPU で実行し、完了まで待つ
    bool tlasOk = g_Engine->ExecuteImmediate([&](ID3D12GraphicsCommandList4* cmd) {
        cmd->BuildRaytracingAccelerationStructure(&tlasDesc, 0, nullptr);
    });
    if (!tlasOk) {
        DebugLog("TLAS の構築に失敗\n");
        return false;
    }

    // ── ルートシグネチャ ────────────────────────────────────────────────────
    if (!CreateRootSignature())      { return false; }

    // ── レイトレーシングパイプライン（StateObject） ─────────────────────────
    if (!CreateRaytracingPipeline()) { return false; }

    // ── 出力バッファ（UAV）/ DXR ディスクリプタヒープ ─────────────────────
    if (!CreateOutputBuffer())       { return false; }
    if (!CreateDxrDescriptorHeap())  { return false; }

    // ── テクスチャロード（ヒープ生成後に行う） ──────────────────────────────
    for (UINT m = 0; m < kModelCount; m++) {
        // モデルごとに、通し番号に対応するスロット位置からテクスチャを書き込む
        m_models[m]->LoadMaterial(m_dxrDescHeap, m_dxrDescIncrementSize,
                                  SLOT_TEX_BEGIN + m_meshBase[m] * SLOT_PER_MESH, SLOT_PER_MESH);
    }

    // ── シェーダーテーブル ──────────────────────────────────────────────────
    if (!BuildShaderTable())         { return false; }

    DebugLog("レイトレーシングシーンの初期化に成功\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Update
// ─────────────────────────────────────────────────────────────────────────────
void RayTracingScene::Update()
{
    m_rotateY += 0.02f;

    // カメラパラメータ（RasterScene と同じ視点）
    auto eyePos = XMVectorSet(0.0f, 150.0f, 75.0f, 0.0f);
    auto targetPos = XMVectorSet(0.0f, 120.0f, 0.0f, 0.0f);
    auto upward = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    float fov = XMConvertToRadians(60.0f);
    float aspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);

    XMMATRIX view = XMMatrixLookAtRH(eyePos, targetPos, upward);
    XMMATRIX proj = XMMatrixPerspectiveFovRH(fov, aspect, 0.3f, 1000.0f);

    auto currentIndex = g_Engine->CurrentBackBufferIndex();
    auto cb = m_rayTracingCB[currentIndex]->GetPtr<RayTracingCB>();
    cb->ViewInv = XMMatrixInverse(nullptr, view);
    cb->ProjInv = XMMatrixInverse(nullptr, proj);
    cb->LightDir = { 0.577f, -0.577f, -0.577f }; // 光の進行方向。左上手前から照らす平行光源
    cb->Padding = 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw
// ─────────────────────────────────────────────────────────────────────────────
void RayTracingScene::Draw()
{
    if (!m_rtStateObject || !m_outputBuffer) return;

    auto currentIndex = g_Engine->CurrentBackBufferIndex();
    auto cmdList = g_Engine->CommandList();

    // DXR コマンド発行用に ID3D12GraphicsCommandList4 を取得
    ComPtr<ID3D12GraphicsCommandList4> dxrCmdList;
    cmdList->QueryInterface(IID_PPV_ARGS(dxrCmdList.GetAddressOf()));
    if (!dxrCmdList) return;

    // ── パイプライン / ルートシグネチャのセット ──────────────────────────────
    dxrCmdList->SetComputeRootSignature(m_globalRootSignature.Get());
    dxrCmdList->SetPipelineState1(m_rtStateObject.Get());

    // ── ディスクリプタヒープのバインド ──────────────────────────────────────
    ID3D12DescriptorHeap* heaps[] = { m_dxrDescHeap.Get() };
    dxrCmdList->SetDescriptorHeaps(1, heaps);

    // スロット0: 出力UAV
    dxrCmdList->SetComputeRootDescriptorTable(
        0, m_dxrDescHeap->GetGPUDescriptorHandleForHeapStart());

    // スロット1: TLAS SRV
    D3D12_GPU_DESCRIPTOR_HANDLE tlasSrvHandle = m_dxrDescHeap->GetGPUDescriptorHandleForHeapStart();
    tlasSrvHandle.ptr += SLOT_TLAS_SRV * m_dxrDescIncrementSize;
    dxrCmdList->SetComputeRootDescriptorTable(1, tlasSrvHandle);

    // スロット2: カメラ定数 CBV（直接アドレス指定）
    dxrCmdList->SetComputeRootConstantBufferView(2, m_rayTracingCB[currentIndex]->GetAddress());

    // ── DispatchRays ────────────────────────────────────────────────────────
    auto alignUp = [](UINT64 val, UINT64 align) { return (val + align - 1) & ~(align - 1); };
    UINT64 recordSize = alignUp(kShaderIdentifierSize, kShaderRecordAlign);

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress = m_rayGenTable->GetGPUVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = recordSize;
    dispatchDesc.MissShaderTable.StartAddress = m_missTable->GetGPUVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = recordSize;
    dispatchDesc.MissShaderTable.StrideInBytes = recordSize;
    dispatchDesc.HitGroupTable.StartAddress  = m_hitGroupTable->GetGPUVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes   = m_hitGroupRecordSize * m_totalMeshes;
    dispatchDesc.HitGroupTable.StrideInBytes = m_hitGroupRecordSize;
    dispatchDesc.Width = WINDOW_WIDTH;
    dispatchDesc.Height = WINDOW_HEIGHT;
    dispatchDesc.Depth = 1;

    dxrCmdList->DispatchRays(&dispatchDesc);

    // ── 出力バッファ（UAV）→ スワップチェイン RT へコピー ──────────────────
    // バリアとコピーは Engine 側が行う（出力バッファは UAV 状態のまま渡す）
    g_Engine->CopyToRenderTarget(m_outputBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}


// ─────────────────────────────────────────────────────────────────────────────
//  グローバル / ローカルルートシグネチャ
//
//  グローバルスロット:
//    0: DescriptorTable → UAV (u0)  出力画像
//    1: DescriptorTable → SRV (t0)  TLAS
//    2: CBV (b0)                    カメラ / ライト定数
//
//  ローカルスロット（ヒットグループ専用、シェーダーテーブルに埋め込む）:
//    0: DescriptorTable → SRV (t1)  テクスチャ (space1)
// ─────────────────────────────────────────────────────────────────────────────
bool RayTracingScene::CreateRootSignature()
{
    // ── グローバルルートシグネチャ ─────────────────────────────────────────
    {
        CD3DX12_ROOT_PARAMETER params[3] = {};

        CD3DX12_DESCRIPTOR_RANGE uavRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        params[0].InitAsDescriptorTable(1, &uavRange);

        CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        params[1].InitAsDescriptorTable(1, &srvRange);

        params[2].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

        D3D12_ROOT_SIGNATURE_DESC desc = {};
        desc.NumParameters = _countof(params);
        desc.pParameters   = params;
        desc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sigBlob, errBlob;
        HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
            sigBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) { DebugLog("グローバルルートシグネチャのシリアライズに失敗\n"); return false; }

        hr = g_Engine->Device()->CreateRootSignature(
            0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
            IID_PPV_ARGS(m_globalRootSignature.GetAddressOf()));
        if (FAILED(hr)) { DebugLog("グローバルルートシグネチャの生成に失敗\n"); return false; }
    }

    // ── ローカルルートシグネチャ（ClosestHit 専用、テクスチャを渡す） ────────
    {
        // t1(テクスチャ), t2(頂点バッファ), t3(インデックスバッファ) を space1 に
        // 連続した3ディスクリプタとして1つのテーブルで渡す
        CD3DX12_DESCRIPTOR_RANGE texRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 1, 1);
        CD3DX12_ROOT_PARAMETER localParam = {};
        localParam.InitAsDescriptorTable(1, &texRange);

        // スタティックサンプラー (s0, space1)
        D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MinLOD           = 0.0f;
        samplerDesc.MaxLOD           = D3D12_FLOAT32_MAX; // 0のままだとミップ0に固定される。ray cone で求めた LOD を使うために必要
        samplerDesc.ShaderRegister   = 0;
        samplerDesc.RegisterSpace    = 1; // space1
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC localDesc = {};
        localDesc.NumParameters     = 1;
        localDesc.pParameters       = &localParam;
        localDesc.NumStaticSamplers = 1;
        localDesc.pStaticSamplers   = &samplerDesc;
        localDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

        ComPtr<ID3DBlob> sigBlob, errBlob;
        HRESULT hr = D3D12SerializeRootSignature(&localDesc, D3D_ROOT_SIGNATURE_VERSION_1_0,
            sigBlob.GetAddressOf(), errBlob.GetAddressOf());
        if (FAILED(hr)) { DebugLog("ローカルルートシグネチャのシリアライズに失敗\n"); return false; }

        hr = g_Engine->Device()->CreateRootSignature(
            0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
            IID_PPV_ARGS(m_localRootSignature.GetAddressOf()));
        if (FAILED(hr)) { DebugLog("ローカルルートシグネチャの生成に失敗\n"); return false; }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DXR パイプライン（StateObject）
// ─────────────────────────────────────────────────────────────────────────────
bool RayTracingScene::CreateRaytracingPipeline()
{
    // .cso ファイルをバイナリで読み込むヘルパー
    auto loadCso = [](const wchar_t* path, std::vector<uint8_t>& outBytes) -> bool {
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, path, L"rb") != 0 || !fp) return false;
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        outBytes.resize(size);
        fread(outBytes.data(), 1, size, fp);
        fclose(fp);
        return true;
    };

    std::vector<uint8_t> rayGenBytes, missBytes, chitBytes;
    if (!loadCso(kRayGenCso,     rayGenBytes)) { DebugLog("RayGen.cso の読み込みに失敗\n");     return false; }
    if (!loadCso(kMissCso,       missBytes))   { DebugLog("Miss.cso の読み込みに失敗\n");       return false; }
    if (!loadCso(kClosestHitCso, chitBytes))   { DebugLog("ClosestHit.cso の読み込みに失敗\n"); return false; }

    std::vector<D3D12_STATE_SUBOBJECT> subObjects;
    subObjects.reserve(16);

    // ── DXIL ライブラリ（シェーダーごとに1つ） ────────────────────────────
    D3D12_EXPORT_DESC rgExport = { kRayGenName, nullptr, D3D12_EXPORT_FLAG_NONE };
    D3D12_EXPORT_DESC msExport = { kMissName,   nullptr, D3D12_EXPORT_FLAG_NONE };
    D3D12_EXPORT_DESC chExport = { kCHitName,   nullptr, D3D12_EXPORT_FLAG_NONE };

    D3D12_DXIL_LIBRARY_DESC rgLib = {};
    rgLib.DXILLibrary = { rayGenBytes.data(), rayGenBytes.size() };
    rgLib.NumExports  = 1;
    rgLib.pExports    = &rgExport;

    D3D12_DXIL_LIBRARY_DESC msLib = {};
    msLib.DXILLibrary = { missBytes.data(), missBytes.size() };
    msLib.NumExports  = 1;
    msLib.pExports    = &msExport;

    D3D12_DXIL_LIBRARY_DESC chLib = {};
    chLib.DXILLibrary = { chitBytes.data(), chitBytes.size() };
    chLib.NumExports  = 1;
    chLib.pExports    = &chExport;

    subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &rgLib });
    subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &msLib });
    subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &chLib });

    // ── ヒットグループ（ClosestHit をまとめる） ────────────────────────────
    D3D12_HIT_GROUP_DESC hitGroup = {};
    hitGroup.HitGroupExport         = kHitGroupName;
    hitGroup.Type                   = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroup.ClosestHitShaderImport = kCHitName;
    subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroup });

    // ── シェーダーコンフィグ（ペイロード・バリセントリクスのサイズ） ────────
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes   = 4 * sizeof(float); // float4 color
    shaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float); // barycentrics
    subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig });

    // ── パイプラインコンフィグ（最大再帰深度） ─────────────────────────────
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = 1;
    subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig });

    // ── グローバルルートシグネチャ ─────────────────────────────────────────
    D3D12_GLOBAL_ROOT_SIGNATURE globalRS = { m_globalRootSignature.Get() };
    subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRS });

    // ── ローカルルートシグネチャ（ClosestHit に関連付け） ───────────────────
    D3D12_LOCAL_ROOT_SIGNATURE localRS = { m_localRootSignature.Get() };
    D3D12_STATE_SUBOBJECT localRSSub = { D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &localRS };
    subObjects.push_back(localRSSub);

    // ローカルルートシグネチャを HitGroup に関連付ける
    const wchar_t* hitGroupExports[] = { kHitGroupName };
    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION localAssoc = {};
    localAssoc.pSubobjectToAssociate = &subObjects.back(); // 直前のローカルRS
    localAssoc.NumExports            = 1;
    localAssoc.pExports              = hitGroupExports;
    subObjects.push_back({ D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &localAssoc });

    // ── StateObject 生成（ID3D12Device5 経由） ─────────────────────────────
    D3D12_STATE_OBJECT_DESC soDesc = {};
    soDesc.Type          = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    soDesc.NumSubobjects = static_cast<UINT>(subObjects.size());
    soDesc.pSubobjects   = subObjects.data();

    ComPtr<ID3D12Device5> device5;
    g_Engine->Device()->QueryInterface(IID_PPV_ARGS(device5.GetAddressOf()));
    if (!device5) {
        DebugLog("ID3D12Device5 の取得に失敗\n");
        return false;
    }

    HRESULT hr = device5->CreateStateObject(&soDesc,
        IID_PPV_ARGS(m_rtStateObject.GetAddressOf()));
    if (FAILED(hr)) {
        DebugLog("DXR StateObject の生成に失敗 (hr=0x%08X)\n", static_cast<unsigned>(hr));
        return false;
    }

    m_rtStateObject->QueryInterface(IID_PPV_ARGS(m_rtStateObjectProps.GetAddressOf()));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  出力バッファ（Texture2D UAV）生成
// ─────────────────────────────────────────────────────────────────────────────
bool RayTracingScene::CreateOutputBuffer()
{
    auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = g_Engine->Device()->CreateCommittedResource(
        &heapProp, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(m_outputBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        DebugLog("レイトレ出力バッファの生成に失敗\n");
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DXR 用ディスクリプタヒープの生成とビュー登録
//
//  スロット:
//    SLOT_OUTPUT_UAV (0)  出力画像  UAV
//    SLOT_TLAS_SRV   (1)  TLAS      SRV
//    SLOT_TEX_BEGIN  (2～) テクスチャ SRV × メッシュ数
// ─────────────────────────────────────────────────────────────────────────────
bool RayTracingScene::CreateDxrDescriptorHeap()
{
    auto device   = g_Engine->Device();
    UINT totalSlots = SLOT_TEX_BEGIN + m_totalMeshes * SLOT_PER_MESH; // UAV + TLAS + (テクスチャ+VB+IB)×n

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = totalSlots;
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NodeMask       = 0;

    HRESULT hr = device->CreateDescriptorHeap(&heapDesc,
        IID_PPV_ARGS(m_dxrDescHeap.GetAddressOf()));
    if (FAILED(hr)) { DebugLog("DXRディスクリプタヒープの生成に失敗\n"); return false; }

    m_dxrDescIncrementSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // SLOT_OUTPUT_UAV: 出力バッファの UAV
    {
        auto cpuHandle = m_dxrDescHeap->GetCPUDescriptorHandleForHeapStart();
        cpuHandle.ptr += SLOT_OUTPUT_UAV * m_dxrDescIncrementSize;
        m_outputUAVCpuHandle = cpuHandle;

        auto gpuHandle = m_dxrDescHeap->GetGPUDescriptorHandleForHeapStart();
        gpuHandle.ptr += SLOT_OUTPUT_UAV * m_dxrDescIncrementSize;
        m_outputUAVGpuHandle = gpuHandle;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
        device->CreateUnorderedAccessView(m_outputBuffer.Get(), nullptr, &uavDesc, cpuHandle);
    }

    // SLOT_TLAS_SRV: TLAS の SRV
    {
        auto cpuHandle = m_dxrDescHeap->GetCPUDescriptorHandleForHeapStart();
        cpuHandle.ptr += SLOT_TLAS_SRV * m_dxrDescIncrementSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.ViewDimension                            = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();
        device->CreateShaderResourceView(nullptr, &srvDesc, cpuHandle);
    }

    // メッシュごとの頂点バッファ SRV / インデックスバッファ SRV
    // スロット配置: SLOT_TEX_BEGIN + i*SLOT_PER_MESH + {0:テクスチャ, 1:頂点, 2:インデックス}
    // テクスチャ SRV は LoadMaterial() が書き込む
    for (UINT g = 0; g < m_totalMeshes; g++) {
        // 通し番号 g から所属モデルとモデル内メッシュ番号を求める
        UINT m = kModelCount - 1;
        while (m > 0 && m_meshBase[m] > g) m--;
        const UINT i = g - m_meshBase[m];
        Objects::Model* model = m_models[m];
        auto& mesh = model->GetMeshes()[i];

        // 頂点バッファ: StructuredBuffer<Vertex>
        {
            auto cpuHandle = m_dxrDescHeap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += (SLOT_TEX_BEGIN + g * SLOT_PER_MESH + 1) * m_dxrDescIncrementSize;

            D3D12_SHADER_RESOURCE_VIEW_DESC vbDesc = {};
            vbDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
            vbDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            vbDesc.Format                          = DXGI_FORMAT_UNKNOWN;
            vbDesc.Buffer.FirstElement             = 0;
            vbDesc.Buffer.NumElements              = static_cast<UINT>(mesh.Vertices.size());
            vbDesc.Buffer.StructureByteStride      = sizeof(Vertex);
            vbDesc.Buffer.Flags                    = D3D12_BUFFER_SRV_FLAG_NONE;
            device->CreateShaderResourceView(
                model->GetVertexBuffers()[i]->GetResource(), &vbDesc, cpuHandle);
        }

        // インデックスバッファ: ByteAddressBuffer（32bit要素の RAW ビュー）
        {
            auto cpuHandle = m_dxrDescHeap->GetCPUDescriptorHandleForHeapStart();
            cpuHandle.ptr += (SLOT_TEX_BEGIN + g * SLOT_PER_MESH + 2) * m_dxrDescIncrementSize;

            D3D12_SHADER_RESOURCE_VIEW_DESC ibDesc = {};
            ibDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
            ibDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            ibDesc.Format                          = DXGI_FORMAT_R32_TYPELESS;
            ibDesc.Buffer.FirstElement             = 0;
            ibDesc.Buffer.NumElements              = static_cast<UINT>(mesh.Indices.size());
            ibDesc.Buffer.Flags                    = D3D12_BUFFER_SRV_FLAG_RAW;
            device->CreateShaderResourceView(
                model->GetIndexBuffers()[i]->GetResource(), &ibDesc, cpuHandle);
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  シェーダーテーブル生成
// ─────────────────────────────────────────────────────────────────────────────
bool RayTracingScene::BuildShaderTable()
{
    if (!m_rtStateObjectProps) { DebugLog("StateObjectProps が未初期化\n"); return false; }

    auto alignUp = [](UINT64 val, UINT64 align) { return (val + align - 1) & ~(align - 1); };
    UINT64 recordSize = alignUp(kShaderIdentifierSize, kShaderRecordAlign);

    // 1レコード分のテーブルを生成するヘルパー
    auto makeTable = [&](const wchar_t* exportName) -> ComPtr<ID3D12Resource> {
        void* shaderId = m_rtStateObjectProps->GetShaderIdentifier(exportName);
        auto  buf = CreateBuffer(recordSize, D3D12_RESOURCE_FLAG_NONE,
                                 D3D12_RESOURCE_STATE_GENERIC_READ,
                                 D3D12_HEAP_TYPE_UPLOAD);
        void* pMapped = nullptr;
        buf->Map(0, nullptr, &pMapped);
        memcpy(pMapped, shaderId, kShaderIdentifierSize);
        buf->Unmap(0, nullptr);
        return buf;
    };

    m_rayGenTable = makeTable(kRayGenName);
    m_missTable   = makeTable(kMissName);

    // ヒットグループテーブル: メッシュ数分のレコード
    // 各レコード = ShaderIdentifier(32B) + GPUDescriptorHandle(8B) をアライン
    {
        // ローカルルートシグネチャのパラメータ: DescriptorTable (GPU handle 8 byte)
        UINT64 localParamSize = sizeof(D3D12_GPU_DESCRIPTOR_HANDLE); // 8 byte
        UINT64 hitRecordSize  = alignUp(kShaderIdentifierSize + localParamSize, kShaderRecordAlign);

        UINT64 hitTableSize = hitRecordSize * m_totalMeshes;
        m_hitGroupTable = CreateBuffer(hitTableSize, D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_GENERIC_READ,
                                       D3D12_HEAP_TYPE_UPLOAD);
        uint8_t* pMapped = nullptr;
        m_hitGroupTable->Map(0, nullptr, reinterpret_cast<void**>(&pMapped));
        void* shaderId = m_rtStateObjectProps->GetShaderIdentifier(kHitGroupName);

        for (size_t i = 0; i < m_totalMeshes; i++) { // i は全モデル通しのメッシュ番号
            uint8_t* pRecord = pMapped + i * hitRecordSize;

            // ShaderIdentifier を先頭に書き込む
            memcpy(pRecord, shaderId, kShaderIdentifierSize);

            // ローカルルートパラメータ: このメッシュのテクスチャ GPU ハンドル
            D3D12_GPU_DESCRIPTOR_HANDLE texHandle = m_dxrDescHeap->GetGPUDescriptorHandleForHeapStart();
            texHandle.ptr += (SLOT_TEX_BEGIN + i * SLOT_PER_MESH) * m_dxrDescIncrementSize; // 先頭=テクスチャ、続けて VB, IB
            memcpy(pRecord + kShaderIdentifierSize, &texHandle, sizeof(texHandle));
        }
        m_hitGroupTable->Unmap(0, nullptr);

        // DispatchRays で使うストライドを hitRecordSize に更新するため保持
        m_hitGroupRecordSize = hitRecordSize;
    }

    return true;
}

