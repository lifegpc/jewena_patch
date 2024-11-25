#include <Windows.h>
#include "detours.h"
#include <stdio.h>
#include "wchar_util.h"

static HFONT(WINAPI *TrueCreateFontW)(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCWSTR lpFaceName) = CreateFontW;
static HFONT(WINAPI *TrueCreateFontA)(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCSTR lpFaceName) = CreateFontA;

char* to_utf8(char* target, const char* source, UINT cp) {
    int count = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, source, -1, NULL, 0);
    if (!count) return nullptr;
    WCHAR* ws = new WCHAR[count + 1];
    MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, source, -1, ws, count);
    char* result = nullptr;
    int ncount = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (ncount) {
        if (!target) {
            target = new char[ncount + 1];
        }
        result = target;
        WideCharToMultiByte(CP_UTF8, 0, ws, -1, result, ncount, nullptr, nullptr);
    }
    delete[] ws;
    return result;
}

char* WINAPI jis_to_utf8(char* target, const char* source) {
    char* result = to_utf8(target, source, CP_UTF8);
    if (!result) {
        result = to_utf8(target, source, 932);
    }
    return result;
}

PVOID GetHandle() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (char*)hModule + 0xf3c20;
}

static PVOID h = nullptr;

HFONT WINAPI HookedCreateFontW(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCWSTR lpFaceName) {
    std::wstring name(lpFaceName);
    if (name == L"Meiryo") {
        lpFaceName = L"微软雅黑";
    }
    return TrueCreateFontW(nHeight, nWidth, nEscapement, nOrientation, fnWeight, dwItalic, dwUnderline, dwStrikeOut, dwCharSet, dwOutPrecision, dwClipPrecision, dwQuality, dwPitchAndFamily, lpFaceName);
}

HFONT WINAPI HookedCreateFontA(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCSTR lpFaceName) {
    UINT cp[] = { CP_UTF8, CP_OEMCP, CP_ACP, 932 };
    std::wstring font;
    for (int i = 0; i < 4; i++) {
        if (wchar_util::str_to_wstr(font, lpFaceName, cp[i])) {
            if (font == L"Meiryo") {
                font = L"微软雅黑";
            }
            return TrueCreateFontW(nHeight, nWidth, nEscapement, nOrientation, fnWeight, dwItalic, dwUnderline, dwStrikeOut, dwCharSet, dwOutPrecision, dwClipPrecision, dwQuality, dwPitchAndFamily, font.c_str());
        }
    }
    if (!strcmp(lpFaceName, "Meiryo")) {
        lpFaceName = "Microsoft YaHei";
    }
    return TrueCreateFontA(nHeight, nWidth, nEscapement, nOrientation, fnWeight, dwItalic, dwUnderline, dwStrikeOut, dwCharSet, dwOutPrecision, dwClipPrecision, dwQuality, dwPitchAndFamily, lpFaceName);
}

extern "C" __declspec(dllexport) void Attach() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    h = GetHandle();
    DetourAttach(&h, (PVOID)jis_to_utf8);
    DetourAttach(&TrueCreateFontW, HookedCreateFontW);
    DetourAttach(&TrueCreateFontA, HookedCreateFontA);
    DetourTransactionCommit();
}

extern "C" __declspec(dllexport) void Detach() {
    if (!h) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&h, (PVOID)jis_to_utf8);
    DetourDetach(&TrueCreateFontW, HookedCreateFontW);
    DetourDetach(&TrueCreateFontA, HookedCreateFontA);
    DetourTransactionCommit();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID rev) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        Attach();
        break;
    case DLL_PROCESS_DETACH:
        Detach();
        break;
    }
    return TRUE;
}
