#pragma once

// シーンのベースクラス
// 各レンダリング方式に対応した具体的なシーンはこのクラスを継承して実装する
class Scene {
public:
	virtual ~Scene() = default;

	virtual bool Init() = 0;     // 初期化
	virtual void Update() = 0;   // 更新処理
	virtual void Draw() = 0;     // 描画処理
};

extern Scene* g_Scene;
