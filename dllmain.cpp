#include <Windows.h>
#include "config.hpp"
#include "detours.h"
#include <stdio.h>
#include "wchar_util.h"
#include "vfs.hpp"
#include "str_util.h"
#include "fileop.h"
#include <fcntl.h>
#include "string_replace_file.hpp"

static HFONT(WINAPI *TrueCreateFontW)(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCWSTR lpFaceName) = CreateFontW;
static HFONT(WINAPI *TrueCreateFontA)(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCSTR lpFaceName) = CreateFontA;
static HANDLE(WINAPI *TrueCreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) = CreateFileW;
static BOOL(WINAPI *TrueReadFile)(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) = ReadFile;
static BOOL(WINAPI *TrueCloseHandle)(HANDLE hObject) = CloseHandle;
static DWORD(WINAPI *TrueGetFileSize)(HANDLE hFile, LPDWORD lpFileSizeHigh) = GetFileSize;
static decltype(GetFileSizeEx) *TrueGetFileSizeEx = GetFileSizeEx;
static DWORD(WINAPI *TrueSetFilePointer)(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) = SetFilePointer;

static Config config;
static std::wstring defaultFont;
static VFS vfs;
static StringReplaceFile replaceFile;

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
    if (!replaceFile.messages.empty() && result) {
        std::string str(result);
        auto re = replaceFile.messages.find(str);
        if (re != replaceFile.messages.end()) {
            str = (*re).second;
            if (target) {
                strcpy(target, str.c_str());
                result = target;
            } else {
                delete[] result;
                result = new char[str.size() + 1];
                strcpy(result, str.c_str());
            }
        }
    }
    return result;
}

PVOID GetHandle() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (char*)hModule + 0xf40e0;
}

static PVOID h = nullptr;

HFONT WINAPI HookedCreateFontW(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCWSTR lpFaceName) {
    std::wstring name(lpFaceName);
    if (name == L"Meiryo") {
        lpFaceName = defaultFont.c_str();
    }
    return TrueCreateFontW(nHeight, nWidth, nEscapement, nOrientation, fnWeight, dwItalic, dwUnderline, dwStrikeOut, dwCharSet, dwOutPrecision, dwClipPrecision, dwQuality, dwPitchAndFamily, lpFaceName);
}

HFONT WINAPI HookedCreateFontA(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCSTR lpFaceName) {
    UINT cp[] = { CP_UTF8, CP_OEMCP, CP_ACP, 932 };
    std::wstring font;
    for (int i = 0; i < 4; i++) {
        if (wchar_util::str_to_wstr(font, lpFaceName, cp[i])) {
            if (font == L"Meiryo") {
                font = defaultFont;
            }
            return TrueCreateFontW(nHeight, nWidth, nEscapement, nOrientation, fnWeight, dwItalic, dwUnderline, dwStrikeOut, dwCharSet, dwOutPrecision, dwClipPrecision, dwQuality, dwPitchAndFamily, font.c_str());
        }
    }
    if (!strcmp(lpFaceName, "Meiryo")) {
        lpFaceName = "Microsoft YaHei";
    }
    return TrueCreateFontA(nHeight, nWidth, nEscapement, nOrientation, fnWeight, dwItalic, dwUnderline, dwStrikeOut, dwCharSet, dwOutPrecision, dwClipPrecision, dwQuality, dwPitchAndFamily, lpFaceName);
}

HANDLE WINAPI HookedCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (vfs.ContainsFile(lpFileName)) {
        return vfs.CreateFileW(lpFileName);
    }
    return TrueCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

BOOL WINAPI HookedReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    if (vfs.ContainsHandle(hFile)) {
        if (lpOverlapped) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return vfs.ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead);
    }
    return TrueReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

BOOL WINAPI HookedCloseHandle(HANDLE hObject) {
    if (vfs.ContainsHandle(hObject)) {
        vfs.CloseHandle(hObject);
        return TRUE;
    }
    return TrueCloseHandle(hObject);
}

DWORD WINAPI HookedGetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    if (vfs.ContainsHandle(hFile)) {
        return vfs.GetFileSize(hFile, lpFileSizeHigh);
    }
    return TrueGetFileSize(hFile, lpFileSizeHigh);
}

BOOL WINAPI HookedGetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
    if (vfs.ContainsHandle(hFile)) {
        return vfs.GetFileSizeEx(hFile, lpFileSize);
    }
    return TrueGetFileSizeEx(hFile, lpFileSize);
}

DWORD WINAPI HookedSetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    if (vfs.ContainsHandle(hFile)) {
        return vfs.SetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
    }
    return TrueSetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
}

extern "C" __declspec(dllexport) void Attach() {
    config.Load("config.txt");
    if (!wchar_util::str_to_wstr(defaultFont, config.configs["defaultFont"], CP_UTF8)) {
        defaultFont = L"微软雅黑";
    }
    if (defaultFont.empty()) {
        defaultFont = L"微软雅黑";
    }
    if (!vfs.AddArchive("jewena-chs.dat")) {
        MessageBoxW(NULL, L"无法打开 jewena-chs.dat。请检查文件是否存在", L"错误", MB_ICONERROR);
        ExitProcess(1);
        return;
    }
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    h = GetHandle();
    DetourAttach(&h, (PVOID)jis_to_utf8);
    DetourAttach(&TrueCreateFontW, HookedCreateFontW);
    DetourAttach(&TrueCreateFontA, HookedCreateFontA);
    DetourAttach(&TrueCreateFileW, HookedCreateFileW);
    DetourAttach(&TrueReadFile, HookedReadFile);
    DetourAttach(&TrueCloseHandle, HookedCloseHandle);
    DetourAttach(&TrueGetFileSize, HookedGetFileSize);
    DetourAttach(&TrueGetFileSizeEx, HookedGetFileSizeEx);
    DetourAttach(&TrueSetFilePointer, HookedSetFilePointer);
    DetourTransactionCommit();
    std::string stringReplaceFile = config.configs["stringReplaceFile"];
    if (!stringReplaceFile.empty()) {
        if (!replaceFile.Load(stringReplaceFile)) {
            MessageBoxW(NULL, L"无法加载文本替换文件。", L"错误", MB_ICONERROR);
        }
    }
#if _DEBUG
    while( !::IsDebuggerPresent() )
    ::Sleep( 1000 );
#endif
}

extern "C" __declspec(dllexport) void Detach() {
    if (!h) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&h, (PVOID)jis_to_utf8);
    DetourDetach(&TrueCreateFontW, HookedCreateFontW);
    DetourDetach(&TrueCreateFontA, HookedCreateFontA);
    DetourDetach(&TrueCreateFileW, HookedCreateFileW);
    DetourDetach(&TrueReadFile, HookedReadFile);
    DetourDetach(&TrueCloseHandle, HookedCloseHandle);
    DetourDetach(&TrueGetFileSize, HookedGetFileSize);
    DetourDetach(&TrueGetFileSizeEx, HookedGetFileSizeEx);
    DetourDetach(&TrueSetFilePointer, HookedSetFilePointer);
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
