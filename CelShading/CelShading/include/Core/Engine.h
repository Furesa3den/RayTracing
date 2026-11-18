#pragma once
#include <functional>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include "Core/ComPtr.h"

#pragma comment(lib, "d3d12.lib") // d3d12ライブラリをリンクする
#pragma comment (lib, "dxgi.lib") // dxgiライブラリをリンクする

class Engine {
public:
	enum { FRAME_BUFFER_COUNT = 2 }; // ダブルバッファリングするので2

public:
	bool Init(HWND hwnd, UINT windowWidth, UINT windowHeight); // エンジン初期化

	void BeginRender(); // 描画の開始処理
	void EndRender(); // 描画の終了処理

	bool CheckDXRSupport(); // DXRサポート確認

	// コマンドを記録して即時実行し、GPUの完了まで待つ（初期化時のAS構築・リソース転送用）
	// record には記録用のコマンドリストが渡される。成功したら true を返す。
	// ・描画ループ（BeginRender～EndRender）の外で呼ぶこと
	// ・メインスレッド専用（スレッドセーフではない）
	bool ExecuteImmediate(const std::function<void(ID3D12GraphicsCommandList4*)>& record);

	// src の内容を現在のバックバッファへコピーする（DXRの出力画像などを画面へ出す用）
	// ・BeginRender と EndRender の間で呼ぶこと（バックバッファが RENDER_TARGET 状態の間）
	// ・src は現在 srcState の状態であること。コピー後は同じ srcState に戻す
	// ・src とバックバッファは、サイズとフォーマットが同じであること（CopyResource の条件）
	// ・コピー後もバックバッファは RENDER_TARGET 状態なので、続けて描画できる
	bool CopyToRenderTarget(ID3D12Resource* src,
		D3D12_RESOURCE_STATES srcState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

public: // 外からアクセスしたいのでGetterとして公開するもの
	ID3D12Device6* Device();
	ID3D12GraphicsCommandList* CommandList();
	UINT CurrentBackBufferIndex();
	ID3D12Resource* GetCurrentRenderTarget(); // 現在のバックバッファリソース（DXRコピー用）

private: // DirectX12初期化に使う関数たち
	bool CreateDevice(); // デバイスを生成
	bool CreateCommandQueue(); // コマンドキューを生成
	bool CreateSwapChain(); // スワップチェインを生成
	bool CreateCommandList(); // コマンドリストとコマンドアロケーターを生成
	bool CreateFence(); // フェンスを生成
	void CreateViewPort(); // ビューポートを生成
	void CreateScissorRect(); // シザー矩形を生成

private: // 描画に使うDirectX12のオブジェクトたち
	HWND m_hWnd;
	UINT m_FrameBufferWidth = 0;
	UINT m_FrameBufferHeight = 0;
	UINT m_CurrentBackBufferIndex = 0;

	ComPtr<ID3D12Device6> m_pDevice = nullptr; // デバイス
	ComPtr<ID3D12CommandQueue> m_pQueue = nullptr; // コマンドキュー
	ComPtr<IDXGISwapChain3> m_pSwapChain = nullptr; // スワップチェイン 
	ComPtr<ID3D12CommandAllocator> m_pAllocator[FRAME_BUFFER_COUNT] = {nullptr}; // コマンドアロケーたー
	ComPtr<ID3D12GraphicsCommandList> m_pCommandList = nullptr; // コマンドリスト
	HANDLE m_fenceEvent = nullptr; // フェンスで使うイベント
	ComPtr<ID3D12Fence> m_pFence = nullptr; // フェンス
	UINT64 m_fenceValue[FRAME_BUFFER_COUNT]; // フェンスの値（ダブルバッファリング用に2個）
	D3D12_VIEWPORT m_Viewport; // ビューポート
	D3D12_RECT m_Scissor; // シザー矩形

private: // 描画に使うオブジェクトとその生成関数たち
	bool CreateRenderTarget(); // レンダーターゲットを生成
	bool CreateDepthStencil(); // 深度ステンシルバッファを生成

	UINT m_RtvDescriptorSize = 0; // レンダーターゲットビューのディスクリプタサイズ
	ComPtr<ID3D12DescriptorHeap> m_pRtvHeap = nullptr; // レンダーターゲットのディスクリプタヒープ
	ComPtr<ID3D12Resource> m_pRenderTargets[FRAME_BUFFER_COUNT] = {nullptr}; // レンダーターゲット（ダブルバッファリングするので2個）

	UINT m_DsvDescriptorSize = 0; // 深度ステンシルのディスクリプターサイズ
	ComPtr<ID3D12DescriptorHeap> m_pDsvHeap = nullptr; // 深度ステンシルのディスクリプタヒープ
	ComPtr<ID3D12Resource> m_pDepthStencilBuffer = nullptr; // 深度ステンシルバッファ（こっちは1つでいい）

private: // ExecuteImmediate 用（初回呼び出し時に生成する）
	bool CreateImmediateContext();
	ComPtr<ID3D12CommandAllocator> m_pImmediateAllocator = nullptr;
	ComPtr<ID3D12GraphicsCommandList4> m_pImmediateList = nullptr;
	ComPtr<ID3D12Fence> m_pImmediateFence = nullptr;
	UINT64 m_immediateFenceValue = 0;

private: // 描画ループで使用するもの
	ID3D12Resource* m_currentRenderTarget = nullptr; // 現在のフレームのレンダーターゲットを一時的に保存しておく関数
	void WaitRender(); // 描画完了を待つ処理
};

extern Engine* g_Engine; // どこからでも参照したいのでグローバルにする

