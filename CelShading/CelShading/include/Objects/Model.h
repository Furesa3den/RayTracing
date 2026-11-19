#pragma once
#include "Core/Engine.h"
#include "Core/ComPtr.h"
#include "Graphics/SharedStruct.h"
#include "Graphics/VertexBuffer.h"
#include "Graphics/IndexBuffer.h"
#include "Graphics/DescriptorHeap.h"
#include <d3d12.h>

namespace Objects {
	class Model {
	public:
		Model(const wchar_t* filePath);
		void LoadMaterial(DescriptorHeap* descriptorHeap); // ラスタライズ用（既存DescriptorHeap）
		void LoadMaterial(                                  // DXR用（直接ヒープに書き込む）
			ComPtr<ID3D12DescriptorHeap>& heap,
			UINT incrementSize,
			UINT slotBegin,
			UINT slotStride = 1); // メッシュ間のスロット間隔

		std::vector<Mesh>& GetMeshes();
		std::vector<VertexBuffer*>& GetVertexBuffers();
		std::vector<IndexBuffer*>& GetIndexBuffers();
		std::vector<DescriptorHandle*>& GetMaterialHandles();


	private:
		bool CreateVertexBuffer();
		bool CreateIndexBuffer();

	private:
		const wchar_t* modelFile;
		std::vector<Mesh> m_meshes;
		std::vector<VertexBuffer*> m_vertexBuffers;
		std::vector<IndexBuffer*> m_indexBuffers;
		std::vector<DescriptorHandle*> m_materialHandles;
	};
}