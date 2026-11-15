#pragma once
#include "SharedStruct.h"
#include "VertexBuffer.h"
#include "IndexBuffer.h"
#include "DescriptorHeap.h"

namespace Objects {
	class Model {
	public:
		Model(const wchar_t* filePath);
		void LoadMaterial(DescriptorHeap* descriptorHeap);

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