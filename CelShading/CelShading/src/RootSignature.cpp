#include "RootSignature.h"
#include "Engine.h"
#include <d3dx12.h>

RootSignature::RootSignature() {
	auto flag = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT; // アプリケーションの入力アセンブラを使用する
	flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS; // ドメインシェーダーのルートシグネチャへんアクセスを拒否する
	flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS; // ハルシェーダーのルートシグネチャへんアクセスを拒否する
	flag |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS; // ジオメトリシェーダーのルートシグネチャへんアクセスを拒否する

	CD3DX12_ROOT_PARAMETER rootParam[1] = {};
	rootParam[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL); // b0の定数バッファを設定、すべてのシェーダーから見えるようにする

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
		printf("ルートシグネチャシリアライズに失敗");
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
		printf("ルートシグネチャの生成に失敗");
		return;
	}

	m_IsValid = true;
}

bool RootSignature::IsValid() { return m_IsValid; }

ID3D12RootSignature* RootSignature::Get() { return m_pRootSignature.Get(); }