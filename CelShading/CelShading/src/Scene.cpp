#include "Scene.h"
#include "Engine.h"
#include "App.h"
#include <d3dx12.h>
#include "SharedStruct.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "ConstantBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "AssimpLoader.h"

using namespace DirectX;

Scene* g_Scene;

std::vector<Mesh> meshes; // メッシュの配列
std::vector<VertexBuffer*> vertexBuffers; // メッシュの数分の頂点バッファ
std::vector<IndexBuffer*> indexBuffers; // メッシュの数分のインデクスバッファ

ConstantBuffer* constantBuffer[Engine::FRAME_BUFFER_COUNT];
RootSignature* rootSignature;
PipelineState* pipelineState;

const wchar_t* modelFile = L"assets/models/Alicia/Alicia_solid_Unity.FBX";



bool Scene::Init() {
	ImportSettings importSetting = {
		modelFile,
		meshes,
		false,
		true
	};

	AssimpLoader loader;
	if (!loader.Load(importSetting)) return false;

	// メッシュの数だけ頂点バッファを用意する
	vertexBuffers.reserve(meshes.size());
	for (size_t i = 0; i < meshes.size(); i++) {
		auto size = sizeof(Vertex) * meshes[i].Vertices.size();
		auto stride = sizeof(Vertex);
		auto vertices = meshes[i].Vertices.data();
		auto pVB = new VertexBuffer(size, stride, vertices);

		if (!pVB->IsValid()) {
			printf("頂点バッファの生成に失敗\n");
			return false;
		}

		vertexBuffers.push_back(pVB);
	}

	// メッシュの数だけインデックスバッファを用意する
	indexBuffers.reserve(meshes.size());
	for (size_t i = 0;i < meshes.size(); i++) {
		auto size = sizeof(uint32_t) * meshes[i].Indices.size();
		auto indices = meshes[i].Indices.data();
		auto pIB = new IndexBuffer(size, indices);

		if (!pIB->IsValid()) {
			printf("インデックスバッファの生成に失敗\n");
			return false;
		}

		indexBuffers.push_back(pIB);
	}

	auto eyePos = XMVectorSet(0.0f, 120.0f, 75.0f, 0.0f); // 視点の位置
	auto targetPos = XMVectorSet(0.0f, 120.0f, 0.0f, 0.0f); // 視点を向ける座標
	auto upward = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // 上方向を表すベクトル
	auto fov = XMConvertToRadians(60); // 視野角
	auto aspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT); // アスペクト比

	for (size_t i = 0; i < Engine::FRAME_BUFFER_COUNT; i++) {
		constantBuffer[i] = new ConstantBuffer(sizeof(Transform));
		if (!constantBuffer[i]->IsValid()) {
			printf("変換行列用定数バッファの生成に失敗\n");
			return false;
		}

		// 変換行列の登録
		auto ptr = constantBuffer[i]->GetPtr<Transform>();
		ptr->World = XMMatrixIdentity();
		ptr->View = XMMatrixLookAtRH(eyePos, targetPos, upward);
		ptr->Proj = XMMatrixPerspectiveFovRH(fov, aspect, 0.3f, 1000.0f);
	}

	rootSignature = new RootSignature();
	if (!rootSignature->IsValid()) {
		printf("ルートシグネチャの生成に失敗\n");
		return false;
	}

	pipelineState = new PipelineState();
	pipelineState->SetInputLayout(Vertex::InputLayout);
	pipelineState->SetRootSignature(rootSignature->Get());
	pipelineState->SetVS(L"../x64/Debug/SimpleVS.cso");
	pipelineState->SetPS(L"../x64/Debug/SimplePS.cso");
	pipelineState->Create();

	if (!pipelineState->IsValid()) {
		printf("パイプラインステートの生成に失敗\n");
		return false;
	}

	printf("シーンの初期化に成功\n");
	return true;
}

float rotateY = 0;

void Scene::Update() {
	rotateY += 0.02f;
	auto currentIndex = g_Engine->CurrentBackBufferIndex(); // 現在のフレーム番号を取得
	auto currentTransform = constantBuffer[currentIndex]->GetPtr<Transform>(); // 現在のフレーム番号に対応する定数バッファを取得
	currentTransform->World = DirectX::XMMatrixRotationY(rotateY); // Y軸で回転させる
}

void Scene::Draw() {
	auto currentIndex = g_Engine->CurrentBackBufferIndex();
	auto commandList = g_Engine->CommandList();

	// メッシュの数だけインデックス分の描画を行う処理を回す
	for (size_t i = 0; i < meshes.size();i++) {
		auto vbView = vertexBuffers[i]->View(); // そのメッシュに対応する頂点バッファ
		auto ibView = indexBuffers[i]->View(); // そのメッシュに対応するインデックスバッファ

		commandList->SetGraphicsRootSignature(rootSignature->Get());
		commandList->SetPipelineState(pipelineState->Get());
		commandList->SetGraphicsRootConstantBufferView(0, constantBuffer[currentIndex]->GetAddress());

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &vbView);
		commandList->IASetIndexBuffer(&ibView);

		commandList->DrawIndexedInstanced(meshes[i].Indices.size(), 1, 0, 0, 0); // インデックスの数分描画する
	}
}