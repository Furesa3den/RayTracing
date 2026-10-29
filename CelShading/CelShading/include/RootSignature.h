#pragma once
#include "ComPtr.h"

struct ID3D12RootSignature;

class RootSignature {
public:
	RootSignature(); // コンストラクタでルートシグネチャを生成
	bool IsValid(); // ルートシグネチャの生成に成功したか
	ID3D12RootSignature* Get(); // ルートシグネチャを返す

private:
	bool m_IsValid = false; // ルートシグネチャの生成に成功したか
	ComPtr<ID3D12RootSignature> m_pRootSignature = nullptr; // ルートシグネチャ
};