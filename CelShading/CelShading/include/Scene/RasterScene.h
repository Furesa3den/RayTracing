#pragma once
#include "Scene/Scene.h"
#include "Core/Engine.h"
#include "Graphics/SharedStruct.h"
#include "Graphics/VertexBuffer.h"
#include "Graphics/IndexBuffer.h"
#include "Graphics/ConstantBuffer.h"
#include "Graphics/RootSignature.h"
#include "Graphics/PipelineState.h"
#include "Graphics/DescriptorHeap.h"
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
