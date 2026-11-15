#pragma once
#include <Windows.h>
#include <cstdio>

const UINT WINDOW_WIDTH = 1280;
const UINT WINDOW_HEIGHT = 720;

void StartApp(const TCHAR* appName);

// ─────────────────────────────────────────────────────────────────────────────
//  DebugLog
//  printf と同じ書式でデバッグメッセージを出力する。
//  Visual Studio の「出力」ウィンドウ（OutputDebugString）に表示され、
//  CUI ウィンドウを必要としない。
//  内部でワイド文字列に変換するため日本語も正しく表示される。
// ─────────────────────────────────────────────────────────────────────────────
inline void DebugLog(const char* fmt, ...)
{
    // まず char バッファに書式展開
    char narrow[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(narrow, sizeof(narrow), fmt, args);
    va_end(args);

    // UTF-8 → UTF-16 に変換して OutputDebugStringW で出力
    wchar_t wide[1024];
    MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 1024);
    OutputDebugStringW(wide);
}
