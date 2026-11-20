#include "Core/Engine.h"
#include "Core/App.h"
#include "Objects/Model.h"
#include "Objects/AssimpLoader.h"
#include "Graphics/Texture2D.h"

#include <filesystem>
#include <algorithm>
#include <cfloat>

namespace fs = std::filesystem;

// 拡張子を置き換える処理
static std::wstring ReplaceExtension(const std::wstring& origin, const char* ext) {
	fs::path p = origin.c_str();
	return p.replace_extension(ext).c_str();
}

Objects::Model::Model(const wchar_t* filePath) {
	modelFile = filePath;
	ImportSettings importSetting{
		modelFile,
		m_meshes,
		false,
		true
	};

	AssimpLoader loader;
	if (!loader.Load(importSetting)) {
		DebugLog("モデル読み込みに失敗\n");
		return;
	}

#ifdef _DEBUG
	// 診断用: メッシュごとの頂点数・三角形数・テクスチャ・バウンディングボックスを出力する。
	// メッシュ同士の重なり（バウンディングボックスの交差）や、テクスチャの割り当てを確認するためのもの。
	for (size_t i = 0; i < m_meshes.size(); i++) {
		const auto& m = m_meshes[i];
		float mn[3] = {  FLT_MAX,  FLT_MAX,  FLT_MAX };
		float mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (const auto& v : m.Vertices) {
			mn[0] = (std::min)(mn[0], v.Position.x); mx[0] = (std::max)(mx[0], v.Position.x);
			mn[1] = (std::min)(mn[1], v.Position.y); mx[1] = (std::max)(mx[1], v.Position.y);
			mn[2] = (std::min)(mn[2], v.Position.z); mx[2] = (std::max)(mx[2], v.Position.z);
		}
		DebugLog("mesh[%zu] verts=%zu tris=%zu tex=%ls bounds=(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n",
			i, m.Vertices.size(), m.Indices.size() / 3, m.DiffuseMap.c_str(),
			mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
	}
#endif

	if (!CreateVertexBuffer()) {
		DebugLog("頂点バッファの生成に失敗\n");
		return;
	}

	if (!CreateIndexBuffer()) {
		DebugLog("インデックスバッファの生成に失敗\n");
		return;
	}
}

void Objects::Model::LoadMaterial(DescriptorHeap* descriptorHeap) {
	m_materialHandles.clear();
	for (size_t i = 0; i < m_meshes.size(); i++) {
		auto texPath = ReplaceExtension(m_meshes[i].DiffuseMap, "tga");
		auto mainTex = Texture2D::Get(texPath);
		auto handle = descriptorHeap->Register(mainTex);
		m_materialHandles.push_back(handle);
	}
}

// DXR 用: シェーダービジブルなヒープに直接 SRV を書き込む
void Objects::Model::LoadMaterial(
	ComPtr<ID3D12DescriptorHeap>& heap,
	UINT incrementSize,
	UINT slotBegin,
	UINT slotStride)
{
	auto device = g_Engine->Device();
	for (size_t i = 0; i < m_meshes.size(); i++) {
		auto texPath = ReplaceExtension(m_meshes[i].DiffuseMap, "tga");
		auto mainTex = Texture2D::Get(texPath);

		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = heap->GetCPUDescriptorHandleForHeapStart();
		cpuHandle.ptr += (slotBegin + i * slotStride) * incrementSize;

		auto srvDesc = mainTex->ViewDesc();
		device->CreateShaderResourceView(mainTex->Resource(), &srvDesc, cpuHandle);
	}
}

// メッシュの数だけ頂点バッファを用意する
bool Objects::Model::CreateVertexBuffer() {
	m_vertexBuffers.reserve(m_meshes.size());
	for (size_t i = 0; i < m_meshes.size(); i++) {
		auto size = sizeof(Vertex) * m_meshes[i].Vertices.size();
		auto stride = sizeof(Vertex);
		auto vertices = m_meshes[i].Vertices.data();
		auto pVB = new VertexBuffer(size, stride, vertices);

		if (!pVB->IsValid()) return false;

		m_vertexBuffers.push_back(pVB);
	}

	return true;
}

bool Objects::Model::CreateIndexBuffer() {
	m_indexBuffers.reserve(m_meshes.size());
	for (size_t i = 0; i < m_meshes.size(); i++) {
		auto size = sizeof(uint32_t) * m_meshes[i].Indices.size();
		auto indices = m_meshes[i].Indices.data();
		auto pIB = new IndexBuffer(size, indices);

		if (!pIB->IsValid()) return false;

		m_indexBuffers.push_back(pIB);
	}

	return true;
}

std::vector<Mesh>& Objects::Model::GetMeshes() { return m_meshes; }

std::vector<VertexBuffer*>& Objects::Model::GetVertexBuffers() { return m_vertexBuffers; }

std::vector<IndexBuffer*>& Objects::Model::GetIndexBuffers() { return m_indexBuffers; }

std::vector<DescriptorHandle*>& Objects::Model::GetMaterialHandles() { return m_materialHandles; }