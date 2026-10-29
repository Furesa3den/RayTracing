#include "Engine.h"
#include <d3d12.h>
#include <stdio.h>
#include <Windows.h>

#include <DirectXTex.h>
#include <d3dx12.h>

Engine* g_Engine;

#pragma region Getter関数

ID3D12Device6* Engine::Device() {
	return m_pDevice.Get();
}

ID3D12GraphicsCommandList* Engine::CommandList() {
	return m_pCommandList.Get();
}

UINT Engine::CurrentBackBufferIndex() {
	return m_CurrentBackBufferIndex;
}

#pragma endregion


#pragma region 描画周り

bool Engine::Init(HWND hwnd, UINT windowWidth, UINT windowHeight) {
	m_FrameBufferWidth = windowWidth;
	m_FrameBufferHeight = windowHeight;
	m_hWnd = hwnd;

	if (!CreateDevice()) {
		printf("デバイスの生成に失敗\n");
		return false;
	}

	if (!CreateCommandQueue()) {
		printf("コマンドキューの生成に失敗\n");
		return false;
	}

	if (!CreateSwapChain()) {
		printf("スワップチェインの生成に失敗\n");
		return false;
	}

	if (!CreateCommandList()) {
		printf("コマンドリストの生成に失敗\n");
		return false;
	}

	if (!CreateFence()) {
		printf("フェンスの生成に失敗\n");
		return false;
	}

	// ビューポートとシザー矩形の生成
	CreateViewPort();
	CreateScissorRect();

	if (!CreateRenderTarget()) {
		printf("レンダーターゲットの生成に失敗\n");
		return false;
	}

	if (!CreateDepthStencil()) {
		printf("深度ステンシルバッファの生成に失敗\n");
		return false;
	}

	printf("描画エンジンの初期化に成功\n");
	return true;
}

void Engine::BeginRender() {
	// 現在のレンダーターゲットを更新
	m_currentRenderTarget = m_pRenderTargets[m_CurrentBackBufferIndex].Get();

	// コマンドを初期化してためる準備をする
	m_pAllocator[m_CurrentBackBufferIndex]->Reset();
	m_pCommandList->Reset(m_pAllocator[m_CurrentBackBufferIndex].Get(), nullptr);

	// ビューポートとシザー矩形を設定
	m_pCommandList->RSSetViewports(1, &m_Viewport);
	m_pCommandList->RSSetScissorRects(1, &m_Scissor);

	// 現在のフレームのレンダーターゲットビューのディスクリプタヒープの開始アドレスを取得
	auto currentRtvHandle = m_pRtvHeap->GetCPUDescriptorHandleForHeapStart();
	currentRtvHandle.ptr += m_CurrentBackBufferIndex * m_RtvDescriptorSize;

	// 深度ステンシルのディスクリプタヒープの開始アドレスを取得
	auto currentDsvHandle = m_pDsvHeap->GetCPUDescriptorHandleForHeapStart();

	// レンダーターゲットが使用可能になるまで待つ
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		m_currentRenderTarget,
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	m_pCommandList->ResourceBarrier(1, &barrier);

	// レンダーターゲットを設定
	m_pCommandList->OMSetRenderTargets(1, &currentRtvHandle, FALSE, &currentDsvHandle);

	// レンダーターゲットをクリア
	const float clearColor[] = { 0.25f, 0.25f, 0.25f, 1.0f };
	m_pCommandList->ClearRenderTargetView(currentRtvHandle, clearColor, 0, nullptr);

	// 深度ステンシルビューをクリア
	m_pCommandList->ClearDepthStencilView(currentDsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void Engine::WaitRender() {
	// 描画終了待ち
	const UINT64 fenceValue = m_fenceValue[m_CurrentBackBufferIndex];
	m_pQueue->Signal(m_pFence.Get(), fenceValue);
	m_fenceValue[m_CurrentBackBufferIndex]++;

	// 次のフレームの描画準備がまだであれば待機する
	if (m_pFence->GetCompletedValue() < fenceValue) {
		// 完了時にイベントを設定
		auto hr = m_pFence->SetEventOnCompletion(fenceValue, m_fenceEvent);

		if (FAILED(hr)) return;

		// 待機処理
		if (WAIT_OBJECT_0 != WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE)) return;
	}
}

void Engine::EndRender() {
	// レンダーターゲットに書き込み終わるまで待つ
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_currentRenderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	m_pCommandList->ResourceBarrier(1, &barrier);

	// コマンドの記録を終了
	m_pCommandList->Close();

	// コマンドを実行
	ID3D12CommandList* ppCmdLists[] = { m_pCommandList.Get() };
	m_pQueue->ExecuteCommandLists(1, ppCmdLists);

	// スワップチェーンを切り替え
	m_pSwapChain->Present(1, 0);

	// 描画完了を待つ
	WaitRender();

	// バックバッファ番号更新
	m_CurrentBackBufferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
}

#pragma endregion

#pragma region 生成処理

bool Engine::CreateDevice() {
	auto hr = D3D12CreateDevice(
		nullptr, // 既定のアダプターを使用
		D3D_FEATURE_LEVEL_11_0, // 機能レベル11.0以上のDirect3Dデバイスを作成
		IID_PPV_ARGS(m_pDevice.ReleaseAndGetAddressOf()) // 作成したDirect3Dデバイスのポインターを受け取る
	);
	return SUCCEEDED(hr);
}

bool Engine::CreateCommandQueue() {
	D3D12_COMMAND_QUEUE_DESC desc = {};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // 描画コマンドを実行するキュー
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL; // 標準の優先度
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE; // フラグはなし
	desc.NodeMask = 0; // 単一GPU環境なので0

	auto hr = m_pDevice->CreateCommandQueue(
		&desc,
		IID_PPV_ARGS(m_pQueue.ReleaseAndGetAddressOf())
	);
	return SUCCEEDED(hr);
}

bool Engine::CreateSwapChain() {
	// DXGIファクトリーの生成
	IDXGIFactory4* pFactory = nullptr;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&pFactory));

	if (FAILED(hr)) return false;

	// スワップチェインの設定
	DXGI_SWAP_CHAIN_DESC desc = {};
	desc.BufferDesc.Width = m_FrameBufferWidth; // 描画領域の幅
	desc.BufferDesc.Height = m_FrameBufferHeight; // 描画領域の高さ
	desc.BufferDesc.RefreshRate.Numerator = 60; // リフレッシュレートの分子
	desc.BufferDesc.RefreshRate.Denominator = 1; // リフレッシュレートの分母
	desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED; // スキャンラインの順序は指定なし
	desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED; // スケーリングの指定なし
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の書式はRGBA 8bit
	desc.SampleDesc.Count = 1; // マルチサンプルしない
	desc.SampleDesc.Quality = 0; // 標準クオリティ
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画結果をレンダーターゲットとして使用
	desc.BufferCount = FRAME_BUFFER_COUNT; // バッファ数は2
	desc.OutputWindow = m_hWnd; // 描画先のウィンドウハンドル
	desc.Windowed = TRUE; // ウィンドウモードで表示
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // フリップ後破棄
	desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // Alt+Enterでフルスクリーン切り替えを許可

	// スワップチェインの生成
	IDXGISwapChain* pSwapChain = nullptr;
	hr = pFactory->CreateSwapChain(
		m_pQueue.Get(), // 描画コマンドを積んだコマンドキュー
		&desc, // スワップチェインの設定
		&pSwapChain // 生成したスワップチェインのポインターを受け取る
	);
	if (FAILED(hr)) {
		pFactory->Release();
		return false;
	}

	// IDXGISwapChain3を取得
	hr = pSwapChain->QueryInterface(IID_PPV_ARGS(m_pSwapChain.ReleaseAndGetAddressOf()));
	if (FAILED(hr)) {
		pFactory->Release();
		pSwapChain->Release();
		return false;
	}

	// バックバッファ番号を取得
	m_CurrentBackBufferIndex = m_pSwapChain->GetCurrentBackBufferIndex();

	pFactory->Release();
	pSwapChain->Release();
	return true;
}

bool Engine::CreateCommandList() {
	// コマンドアロケーターの生成
	HRESULT hr;
	for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
		hr = m_pDevice->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, // 描画コマンドを実行するキュー
			IID_PPV_ARGS(m_pAllocator[i].ReleaseAndGetAddressOf()) // 生成したコマンドアロケーターのポインターを受け取る
		);
	}

	if (FAILED(hr)) return false;

	// コマンドリストの生成
	hr = m_pDevice->CreateCommandList(
		0, // 単一GPU環境なので0
		D3D12_COMMAND_LIST_TYPE_DIRECT, // 描画コマンドを実行するキュー
		m_pAllocator[m_CurrentBackBufferIndex].Get(), // コマンドアロケーター
		nullptr, // パイプラインステートオブジェクトは未指定
		IID_PPV_ARGS(m_pCommandList.ReleaseAndGetAddressOf()) // 生成したコマンドリストのポインターを受け取る
	);
	if (FAILED(hr)) return false;

	// コマンドリストは生成直後は記録状態になっているので、閉じておく
	m_pCommandList->Close();

	return true;
}

bool Engine::CreateFence() {
	for (auto i = 0u;i < FRAME_BUFFER_COUNT; i++) {
		m_fenceValue[i] = 0;
	}

	auto hr = m_pDevice->CreateFence(
		0, // 初期値は0
		D3D12_FENCE_FLAG_NONE, // フラグはなし
		IID_PPV_ARGS(m_pFence.ReleaseAndGetAddressOf()) // 生成したフェンスのポインターを受け取る
	);

	if (FAILED(hr)) return false;

	// 同期を行うときのイベントハンドラを作成
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	return m_fenceEvent != nullptr;
}

void Engine::CreateViewPort() {
	m_Viewport.TopLeftX = 0.0f; // ビューポート左上X座標
	m_Viewport.TopLeftY = 0.0f; // ビューポート左上Y座標
	m_Viewport.Width = static_cast<float>(m_FrameBufferWidth); // ビューポート幅
	m_Viewport.Height = static_cast<float>(m_FrameBufferHeight); // ビューポート高さ
	m_Viewport.MinDepth = 0.0f; // 深度の最小値
	m_Viewport.MaxDepth = 1.0f; // 深度の最大値
}

void Engine::CreateScissorRect() {
	m_Scissor.left = 0; // シザー矩形左上X座標
	m_Scissor.right = m_FrameBufferWidth; // シザー矩形右下X座標
	m_Scissor.top = 0; // シザー矩形左上Y座標
	m_Scissor.bottom = m_FrameBufferHeight; // シザー矩形右下Y座標
}

bool Engine::CreateRenderTarget() {
	// PRV用のディスクリプタヒープを作成
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = FRAME_BUFFER_COUNT; // ディスクリプタの数はフレームバッファの数と同じ
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // レンダーターゲットビュー用
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // シェーダーからアクセスしないのでフラグはなし

	auto hr = m_pDevice->CreateDescriptorHeap(
		&desc,
		IID_PPV_ARGS(m_pRtvHeap.ReleaseAndGetAddressOf())
	);

	if (FAILED(hr)) return false;

	// ディスクリプタのサイズを取得
	m_RtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_pRtvHeap->GetCPUDescriptorHandleForHeapStart();

	for (UINT i = 0; i < FRAME_BUFFER_COUNT; i++) {
		m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(m_pRenderTargets[i].ReleaseAndGetAddressOf()));
		m_pDevice->CreateRenderTargetView(m_pRenderTargets[i].Get(), nullptr, rtvHandle);
		rtvHandle.ptr += m_RtvDescriptorSize;
	}

	return true;
}

bool Engine::CreateDepthStencil() {
	// DSV用のディスクリプタヒープを作成
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1; // ディスクリプタの数は1
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; // 深度ステンシルビュー用
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // シェーダーからアクセスしないのでフラグはなし

	auto hr = m_pDevice->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(&m_pDsvHeap)
	);

	if (FAILED(hr)) return false;

	// ディスクリプタのサイズを取得
	m_DsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	D3D12_CLEAR_VALUE dsvClearValue;
	dsvClearValue.Format = DXGI_FORMAT_D32_FLOAT; // 深度値は32bit浮動小数点
	dsvClearValue.DepthStencil.Depth = 1.0f; // 深度の初期値は1.0f
	dsvClearValue.DepthStencil.Stencil = 0; // ステンシルの初期値は0

	auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resourceDesc(
		D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		0,
		m_FrameBufferWidth,
		m_FrameBufferHeight,
		1,
		1,
		DXGI_FORMAT_D32_FLOAT,
		1,
		0,
		D3D12_TEXTURE_LAYOUT_UNKNOWN,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	hr = m_pDevice->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&dsvClearValue,
		IID_PPV_ARGS(m_pDepthStencilBuffer.ReleaseAndGetAddressOf())
	);

	if (FAILED(hr))
	{
		return false;
	}

	//ディスクリプタを作成
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_pDsvHeap->GetCPUDescriptorHandleForHeapStart();

	m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer.Get(), nullptr, dsvHandle);

	return true;
}

#pragma endregion
