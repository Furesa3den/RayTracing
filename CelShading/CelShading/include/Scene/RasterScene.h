#pragma once
#include "Scene/Scene.h"
#include "Core/Engine.h"
#include "SharedStruct.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "ConstantBuffer.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "DescriptorHeap.h"
#include <vector>

#include "Objects/Model.h"

// ラスタライズレンダリングを行うシーン
class RasterScene : public Scene {
public:
	bool Init() override;     // 初期化
	void Update() override;   // 更新処理
	void Draw() override;     // 描画処理

private:
	Objects::Model* fbxModel;

	ConstantBuffer*               m_constantBuffer[Engine::FRAME_BUFFER_COUNT] = {};
	RootSignature*                m_rootSignature  = nullptr;
	PipelineState*                m_pipelineState  = nullptr;
	DescriptorHeap*               m_descriptorHeap = nullptr;
	std::vector<DescriptorHandle*> m_materialHandles;

	float m_rotateY = 0.0f;
};
