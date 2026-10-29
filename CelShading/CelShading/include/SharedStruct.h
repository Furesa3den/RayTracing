#pragma once
#include <d3dx12.h>
#include <DirectXMath.h>
#include "ComPtr.h"

struct Vertex {
public:
	DirectX::XMFLOAT3 Position; // 位置座標
	DirectX::XMFLOAT3 Normal; // 法線
	DirectX::XMFLOAT2 UV; // uv座標
	DirectX::XMFLOAT3 Tangent; // 接空間
	DirectX::XMFLOAT4 Color; // 頂点色

	static const D3D12_INPUT_LAYOUT_DESC InputLayout;

private:
	static const int InputElementCount = 5;
	static const D3D12_INPUT_ELEMENT_DESC InputElements[InputElementCount];
};

struct alignas(256) Transform {
	DirectX::XMMATRIX World; // ワールド行列
	DirectX::XMMATRIX View; // ビュー行列
	DirectX::XMMATRIX Proj; // 投影行列
};