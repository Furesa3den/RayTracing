#include "Scene/RasterScene.h"
#include "Core/Engine.h"
#include "Core/App.h"
#include <d3dx12.h>
#include "Graphics/SharedStruct.h"
#include "Objects/AssimpLoader.h"
#include "Graphics/Texture2D.h"

#include <filesystem>

using namespace DirectX;

namespace fs = std::filesystem;

bool RasterScene::Init() {
	fbxModel = new Objects::Model(L"assets/models/Alicia/Alicia_solid_Unity.FBX");
	
	auto eyePos    = XMVectorSet(0.0f, 150.0f, 75.0f, 0.0f); // 視点の位置
	auto targetPos = XMVectorSet(0.0f, 120.0f,  0.0f, 0.0f); // 視点を向ける座標
	auto upward    = XMVectorSet(0.0f,   1.0f,  0.0f, 0.0f); // 上方向を表すベクトル
	auto fov       = XMConvertToRadians(60);                 // 視野角
	auto aspect    = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);

	for (size_t i = 0; i < Engine::FRAME_BUFFER_COUNT; i++) {
		m_constantBuffer[i] = new ConstantBuffer(sizeof(Transform));
		if (!m_constantBuffer[i]->IsValid()) {
			DebugLog("変換行列用定数バッファの生成に失敗\n");
			return false;
		}

		// 変換行列の登録
		auto ptr    = m_constantBuffer[i]->GetPtr<Transform>();
		ptr->World  = XMMatrixIdentity();
		ptr->View   = XMMatrixLookAtRH(eyePos, targetPos, upward);
		ptr->Proj   = XMMatrixPerspectiveFovRH(fov, aspect, 0.3f, 1000.0f);
	}

	m_descriptorHeap = new DescriptorHeap();

	// マテリアルの読み込み
	fbxModel->LoadMaterial(m_descriptorHeap);

	m_rootSignature = new RootSignature();
	if (!m_rootSignature->IsValid()) {
		DebugLog("ルートシグネチャの生成に失敗\n");
		return false;
	}

	m_pipelineState = new PipelineState();
	m_pipelineState->SetInputLayout(Vertex::InputLayout);
	m_pipelineState->SetRootSignature(m_rootSignature->Get());
	m_pipelineState->SetVS(L"../x64/Debug/SimpleVS.cso");
	m_pipelineState->SetPS(L"../x64/Debug/SimplePS.cso");
	m_pipelineState->Create();

	if (!m_pipelineState->IsValid()) {
		DebugLog("パイプラインステートの生成に失敗\n");
		return false;
	}

	DebugLog("シーンの初期化に成功\n");
	return true;
}

void RasterScene::Update() {
	m_rotateY += 0.02f;
	auto currentIndex     = g_Engine->CurrentBackBufferIndex();
	auto currentTransform = m_constantBuffer[currentIndex]->GetPtr<Transform>();
	currentTransform->World = XMMatrixRotationY(m_rotateY);
}

void RasterScene::Draw() {
	auto currentIndex = g_Engine->CurrentBackBufferIndex();
	auto commandList  = g_Engine->CommandList();
	auto materialHeap = m_descriptorHeap->GetHeap();

	for (size_t i = 0; i < fbxModel->GetMeshes().size(); i++) {
		auto vbView = fbxModel->GetVertexBuffers()[i]->View();
		auto ibView = fbxModel->GetIndexBuffers()[i]->View();

		commandList->SetGraphicsRootSignature(m_rootSignature->Get());
		commandList->SetPipelineState(m_pipelineState->Get());
		commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer[currentIndex]->GetAddress());

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &vbView);
		commandList->IASetIndexBuffer(&ibView);
		
		// テクスチャありで描画
		commandList->SetDescriptorHeaps(1, &materialHeap);
		commandList->SetGraphicsRootDescriptorTable(1, fbxModel->GetMaterialHandles()[i]->HandleGPU);

		commandList->DrawIndexedInstanced(static_cast<UINT>(fbxModel->GetMeshes()[i].Indices.size()), 1, 0, 0, 0);
	}
}

