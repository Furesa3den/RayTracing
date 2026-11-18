#include "Texture2D.h"
#include <DirectXTex.h>
#include "Core/Engine.h"

#pragma comment(lib, "DirectXTex.lib");

using namespace DirectX;

// std::string(マルチバイト文字列)からstd::wstring(ワイド文字列)を得る。
std::wstring GetWideString(const std::string& str)
{
	auto num1 = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, str.c_str(), -1, nullptr, 0);

	std::wstring wstr;
	wstr.resize(num1);

	auto num2 = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, str.c_str(), -1, &wstr[0], num1);

	assert(num1 == num2);
	return wstr;
}

// 拡張子を返す
std::wstring FileExtension(const std::wstring& path) {
	auto idx = path.rfind(L'.');
	return path.substr(idx + 1, path.length() - idx - 1);
}

Texture2D::Texture2D(std::string path) { m_IsValid = Load(path); }
Texture2D::Texture2D(std::wstring path) { m_IsValid = Load(path); }

Texture2D::Texture2D(ID3D12Resource* buffer) {
	m_pResource = buffer;
	m_IsValid = m_pResource != nullptr;
}

bool Texture2D::Load(std::string& path) {
	auto wpath = GetWideString(path);
	return Load(wpath);
}

bool Texture2D::Load(std::wstring& path) {
	// WICテクスチャのロード
	TexMetadata meta = {};
	ScratchImage scratch = {};
	auto ext = FileExtension(path);

	HRESULT hr = E_FAIL; // 未対応の拡張子は失敗として扱う
	if (ext == L"png") // pngの時はWICFileを使う
		hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_NONE, &meta, scratch);
	else if (ext == L"tga") // tgaの時はTGAFileを使う
		hr = LoadFromTGAFile(path.c_str(), &meta, scratch);

	if (FAILED(hr)) return false;

	// ミップマップを生成する（元画像が1段だけの通常の2Dテクスチャが対象）
	// 縮小表示でも、ミップから適切な解像度を読めるようにしてエイリアシングを抑える
	if (meta.mipLevels == 1 && meta.arraySize == 1 && meta.dimension == TEX_DIMENSION_TEXTURE2D) {
		ScratchImage mipChain = {};
		hr = GenerateMipMaps(
			scratch.GetImages(),
			scratch.GetImageCount(),
			scratch.GetMetadata(),
			TEX_FILTER_DEFAULT,
			0, // 0 = 1x1 まで全段生成
			mipChain);

		if (SUCCEEDED(hr)) {
			scratch = std::move(mipChain);
			meta = scratch.GetMetadata();
		}
		// 生成に失敗した場合は、ミップなし（1段）のまま続行する
	}

	auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0);
	auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
		meta.format,
		meta.width,
		meta.height,
		static_cast<UINT16>(meta.arraySize),
		static_cast<UINT16>(meta.mipLevels)
	);

	// リソースを生成
	hr = g_Engine->Device()->CreateCommittedResource(
		&prop,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(m_pResource.ReleaseAndGetAddressOf())
	);

	if (FAILED(hr)) return false;

	// 全てのミップ（と配列スライス）を書き込む
	for (size_t item = 0; item < meta.arraySize; ++item) {
		for (size_t mip = 0; mip < meta.mipLevels; ++mip) {
			const Image* img = scratch.GetImage(mip, item, 0);
			if (img == nullptr) return false;

			hr = m_pResource->WriteToSubresource(
				D3D12CalcSubresource(
					static_cast<UINT>(mip),
					static_cast<UINT>(item),
					0,
					static_cast<UINT>(meta.mipLevels),
					static_cast<UINT>(meta.arraySize)),
				nullptr, // 全領域へコピー
				img->pixels, // 元データアドレス
				static_cast<UINT>(img->rowPitch), // １ラインサイズ
				static_cast<UINT>(img->slicePitch) // 全サイズ
			);

			if (FAILED(hr)) return false;
		}
	}

	return true;
}

Texture2D* Texture2D::Get(std::string path) {
	auto wpath = GetWideString(path);
	return Get(wpath);
}

Texture2D* Texture2D::Get(std::wstring path) {
	auto tex = new Texture2D(path);

	if (!tex->IsValid()) return GetWhite(); // 読み込みに失敗したときは白単色テクスチャを返す

	return tex;
}

Texture2D* Texture2D::GetWhite() {
	ID3D12Resource* buff = GetDefaultResource(4, 4);

	std::vector<unsigned char> data(4 * 4 * 4);
	std::fill(data.begin(), data.end(), 0xff);

	auto hr = buff->WriteToSubresource(0, nullptr, data.data(), 4 * 4, data.size());

	if (FAILED(hr)) return nullptr;

	return new Texture2D(buff);
}

ID3D12Resource* Texture2D::GetDefaultResource(size_t width, size_t height) {
	// ミップは1段だけ（省略すると全段確保され、未書き込みのミップを読んでしまう）
	auto resDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	auto texHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0);
	ID3D12Resource* buff = nullptr;
	auto result = g_Engine->Device()->CreateCommittedResource(
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE, // 特に指定なし
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&buff)
	);

	if (FAILED(result)) {
		assert(SUCCEEDED(result));
		return nullptr;
	}

	return buff;
}

bool Texture2D::IsValid() { return m_IsValid; }

ID3D12Resource* Texture2D::Resource() { return m_pResource.Get(); }

D3D12_SHADER_RESOURCE_VIEW_DESC Texture2D::ViewDesc() {
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = m_pResource->GetDesc().Format;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャ
	desc.Texture2D.MipLevels = m_pResource->GetDesc().MipLevels; // リソースが持つミップ段数をすべて使う
	return desc;
}