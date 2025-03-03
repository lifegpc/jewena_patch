#include <Windows.h>
#include "config.hpp"
#include "detours.h"
#include <stdio.h>
#include <shlwapi.h>
#include "wchar_util.h"
#include "vfs.hpp"
#include "str_util.h"
#include "fileop.h"
#include <algorithm>
#include <fcntl.h>

static HFONT(WINAPI *TrueCreateFontW)(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCWSTR lpFaceName) = CreateFontW;
static HFONT(WINAPI *TrueCreateFontA)(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCSTR lpFaceName) = CreateFontA;
static HANDLE(WINAPI *TrueCreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) = CreateFileW;
static BOOL(WINAPI *TrueReadFile)(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) = ReadFile;
static BOOL(WINAPI *TrueCloseHandle)(HANDLE hObject) = CloseHandle;
static DWORD(WINAPI *TrueGetFileSize)(HANDLE hFile, LPDWORD lpFileSizeHigh) = GetFileSize;
static DWORD(WINAPI *TrueSetFilePointer)(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) = SetFilePointer;
static decltype(FindFirstFileExW)* OriginalFindFirstFileExW = FindFirstFileExW;
static decltype(FindNextFileW)* OriginalFindNextFileW = FindNextFileW;
static decltype(FindClose)* OriginalFindClose = FindClose;

static Config config;
static std::wstring defaultFont;
static VFS vfs;

struct CustomFindContext {
    std::vector<WIN32_FIND_DATAW> entries;
    size_t current_index;
};

HANDLE WINAPI HookedFindFirstFileExW(
    LPCWSTR lpFileName,
    FINDEX_INFO_LEVELS fInfoLevelId,
    LPVOID lpFindFileData,
    FINDEX_SEARCH_OPS fSearchOp,
    LPVOID lpSearchFilter,
    DWORD dwAdditionalFlags
) {
    std::wstring filename_w(lpFileName);
    std::string filename_utf8;
    wchar_util::wstr_to_str(filename_utf8, filename_w, CP_UTF8);

    // 解析目录和模式
    std::string dir_part = fileop::dirname(filename_utf8);
    std::string pattern = fileop::basename(filename_utf8);

    // 标准化路径，判断是否为目标目录
    std::string abs_dir = fileop::isabs(dir_part) ? dir_part : fileop::join(vfs.GetBasePath(), dir_part);
    std::string base = fileop::join(vfs.GetBasePath(), ""); // 确保base_path以/结尾

    if (abs_dir != vfs.GetBasePath()) {
        // 非目标目录，调用原始函数
        return OriginalFindFirstFileExW(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags);
    }

    // 收集实际文件系统的条目
    HANDLE hFindReal = OriginalFindFirstFileExW(lpFileName, fInfoLevelId, lpFindFileData, fSearchOp, lpSearchFilter, dwAdditionalFlags);
    std::vector<WIN32_FIND_DATAW> real_entries;
    WIN32_FIND_DATAW data;
    if (hFindReal != INVALID_HANDLE_VALUE) {
        while (OriginalFindNextFileW(hFindReal, &data)) {
            real_entries.push_back(data);
        }
        OriginalFindClose(hFindReal);
    }

    // 转换模式为宽字符
    std::wstring wpattern;
    wchar_util::str_to_wstr(wpattern, pattern, CP_UTF8);

    // 收集虚拟条目
    std::vector<WIN32_FIND_DATAW> virtual_entries;
    for (const auto& entry : vfs.files) {
        std::string file_path = entry.first;
        std::vector<std::string> components = str_util::str_splitv(file_path, "\\");
        if (components.empty()) continue;

        if (components.size() == 1) {
            // 文件条目
            std::wstring wname;
            wchar_util::str_to_wstr(wname, components[0], CP_UTF8);
            if (PathMatchSpecW(wname.c_str(), wpattern.c_str())) {
                WIN32_FIND_DATAW vdata = {0};
                wcsncpy(vdata.cFileName, wname.c_str(), MAX_PATH);
                vdata.dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;
                vdata.nFileSizeLow = static_cast<DWORD>(entry.second & 0xFFFFFFFF);
                vdata.nFileSizeHigh = static_cast<DWORD>(entry.second >> 32);
                virtual_entries.push_back(vdata);
            }
        } else {
            // 目录条目
            std::wstring wdir;
            wchar_util::str_to_wstr(wdir, components[0], CP_UTF8);
            if (PathMatchSpecW(wdir.c_str(), wpattern.c_str())) {
                bool dir_exists = std::any_of(real_entries.begin(), real_entries.end(),
                    [&](const WIN32_FIND_DATAW& d) {
                        return (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                               _wcsicmp(d.cFileName, wdir.c_str()) == 0;
                    });
                if (!dir_exists) {
                    WIN32_FIND_DATAW vdata = {0};
                    wcsncpy(vdata.cFileName, wdir.c_str(), MAX_PATH);
                    vdata.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
                    virtual_entries.push_back(vdata);
                }
            }
        }
    }

    // 合并条目
    std::vector<WIN32_FIND_DATAW> all_entries;
    all_entries.reserve(real_entries.size() + virtual_entries.size());
    all_entries.insert(all_entries.end(), real_entries.begin(), real_entries.end());
    all_entries.insert(all_entries.end(), virtual_entries.begin(), virtual_entries.end());

    // 创建上下文
    CustomFindContext* ctx = new CustomFindContext{all_entries, 0};
    if (!all_entries.empty()) {
        *static_cast<WIN32_FIND_DATAW*>(lpFindFileData) = all_entries[1];
        ctx->current_index = 2;
    }
    return reinterpret_cast<HANDLE>(ctx);
}

BOOL WINAPI HookedFindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) {
    CustomFindContext* ctx = reinterpret_cast<CustomFindContext*>(hFindFile);
    if (ctx && ctx->current_index < ctx->entries.size()) {
        *lpFindFileData = ctx->entries[ctx->current_index++];
        return TRUE;
    }
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

BOOL WINAPI HookedFindClose(HANDLE hFindFile) {
    CustomFindContext* ctx = reinterpret_cast<CustomFindContext*>(hFindFile);
    if (ctx) {
        delete ctx;
        return TRUE;
    }
    return OriginalFindClose(hFindFile);
}

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
    DetourAttach(&TrueSetFilePointer, HookedSetFilePointer);
    // DetourAttach(&OriginalFindFirstFileExW, HookedFindFirstFileExW);
    // DetourAttach(&OriginalFindNextFileW, HookedFindNextFileW);
    // DetourAttach(&OriginalFindClose, HookedFindClose);
    DetourTransactionCommit();
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
    DetourDetach(&TrueSetFilePointer, HookedSetFilePointer);
    // DetourDetach(&OriginalFindFirstFileExW, HookedFindFirstFileExW);
    // DetourDetach(&OriginalFindNextFileW, HookedFindNextFileW);
    // DetourDetach(&OriginalFindClose, HookedFindClose);
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
