#include "Graphics/RootSignature.h"
#include "Core/Engine.h"
#include "Core/App.h"
#include <d3dx12.h>

RootSignature::RootSignature() {
	auto flag = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT; // アプリケーションの入力アセンブラを使用する
	flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS; // ドメインシェーダーのルートシグネチャへんアクセスを拒否する
	flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS; // ハルシェーダーのルートシグネチャへんアクセスを拒否する
	flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS; // ジオメトリシェーダーのルートシグネチャへんアクセスを拒否する

	CD3DX12_ROOT_PARAMETER rootParam[2] = {}; // 定数バッファとテクスチャ
	rootParam[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);

	CD3DX12_DESCRIPTOR_RANGE tableRange[1] = {}; // ディスクリプタテーブル
	tableRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // シェーダーリソースビュー
	rootParam[1].InitAsDescriptorTable(std::size(tableRange), tableRange, D3D12_SHADER_VISIBILITY_ALL);

	// スタティックサンプラーの設定
	auto sampler = CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

	// ルートシグネチャの設定（設定したいルートパラメータとスタティックサンプラーを入れる）
	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.NumParameters = std::size(rootParam); // ルートパラメータの個数をいれる
	desc.NumStaticSamplers = 1; // サンプラーの個数をいれる
	desc.pParameters = rootParam; // ルートパラメータのポインタをいれる
	desc.pStaticSamplers = &sampler; // サンプラーのポインタをいれる
	desc.Flags = flag; // フラグを設定

	ComPtr<ID3DBlob> pBlob;
	ComPtr<ID3DBlob> pErrorBlob;

	// シリアライズ
	auto hr = D3D12SerializeRootSignature(
		&desc,
		D3D_ROOT_SIGNATURE_VERSION_1_0,
		pBlob.GetAddressOf(),
		pErrorBlob.GetAddressOf()
	);

	if (FAILED(hr)) {
		DebugLog("ルートシグネチャシリアライズに失敗\n");
		return;
	}

	// ルートシグネチャ生成
	hr = g_Engine->Device()->CreateRootSignature(
		0, // GPUが複数ある場合のノードマスク（今回は1個しかない想定なので０）
		pBlob->GetBufferPointer(), // シリアライズしたデータのポインタ
		pBlob->GetBufferSize(), // シリアライズしたデータのサイズ
		IID_PPV_ARGS(m_pRootSignature.GetAddressOf()) // ルートシグネチャ格納咲のポインタ
	);

	if (FAILED(hr)) {
		DebugLog("ルートシグネチャの生成に失敗\n");
		return;
	}

	m_IsValid = true;
}

bool RootSignature::IsValid() { return m_IsValid; }

ID3D12RootSignature* RootSignature::Get() { return m_pRootSignature.Get(); }