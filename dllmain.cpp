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
#include "player.h"

static HFONT(WINAPI *TrueCreateFontW)(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCWSTR lpFaceName) = CreateFontW;
static HFONT(WINAPI *TrueCreateFontA)(int nHeight, int nWidth, int nEscapement, int nOrientation, int fnWeight, DWORD dwItalic, DWORD dwUnderline, DWORD dwStrikeOut, DWORD dwCharSet, DWORD dwOutPrecision, DWORD dwClipPrecision, DWORD dwQuality, DWORD dwPitchAndFamily, LPCSTR lpFaceName) = CreateFontA;
static HANDLE(WINAPI *TrueCreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) = CreateFileW;
static BOOL(WINAPI *TrueReadFile)(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) = ReadFile;
static BOOL(WINAPI *TrueCloseHandle)(HANDLE hObject) = CloseHandle;
static DWORD(WINAPI *TrueGetFileSize)(HANDLE hFile, LPDWORD lpFileSizeHigh) = GetFileSize;
static decltype(GetFileSizeEx) *TrueGetFileSizeEx = GetFileSizeEx;
static DWORD(WINAPI *TrueSetFilePointer)(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) = SetFilePointer;
static decltype(SetFilePointerEx) *TrueSetFilePointerEx = SetFilePointerEx;
static decltype(GetFileType) *TrueGetFileType = GetFileType;
static decltype(GetFileAttributesW) *TrueGetFileAttributesW = GetFileAttributesW;
static decltype(GetFileAttributesExW) *TrueGetFileAttributesExW = GetFileAttributesExW;

typedef int64_t(*OpenMediaFileAndGetDuration)(DWORD* duration, const char* arcName, const char* videoName);
typedef int64_t(*IsPlaying)();
typedef int64_t(*IsMediaPlaying)();
typedef int64_t(*ReleaseDirectShowGraph)();

static Config config;
static std::wstring defaultFont;
static VFS vfs;
static StringReplaceFile replaceFile;
static PlayerSession* player = NULL;
static PlayerSettings* settings = NULL;
static OpenMediaFileAndGetDuration OpenMediaFileAndGetDurationFunc = NULL;
static IsPlaying IsPlayingFunc = NULL;
static IsMediaPlaying IsMediaPlayingFunc = NULL;
static ReleaseDirectShowGraph ReleaseDirectShowGraphFunc = NULL;

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

HWND* GetHwndPointer() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (HWND*)((char*)hModule + 0x1e1620);
}

OpenMediaFileAndGetDuration GetOpenMediaFileAndGetDuration() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (OpenMediaFileAndGetDuration)((char*)hModule + 0xed3d0);
}

IsPlaying GetIsPlaying() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (IsPlaying)((char*)hModule + 0xed810);
}

IsMediaPlaying GetIsMediaPlaying() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (IsMediaPlaying)((char*)hModule + 0xed2f0);
}

ReleaseDirectShowGraph GetReleaseDirectShowGraph() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (ReleaseDirectShowGraph)((char*)hModule + 0xed610);
}

static PVOID h = nullptr;

int64_t HookedOpenMediaFileAndGetDuration(DWORD* duration, const char* arcName, const char* videoName) {
    player_free(&player);
    player_log(AV_LOG_INFO, "BGI: Open Video: %s, %s\n", arcName, videoName);
    int64_t ok = 0;
    if (fileop::exists(videoName)) {
        player_log(AV_LOG_INFO, "Video file exists: %s\n", videoName);
        if (!settings) {
            settings = player_settings_init();
            if (!settings) {
                player_log(AV_LOG_ERROR, "Failed to initialize player settings.\n");
                goto end;
            }
        }
        player_settings_set_hWnd(settings, (void**)GetHwndPointer());
        if (player_create2(videoName, &player, settings)) {
            player_log(AV_LOG_ERROR, "Failed to create player session.\n");
            goto end;
        }
        if (wait_player_inited(player)) {
            player_log(AV_LOG_ERROR, "Failed to initialize player.\n");
            goto end;
        }
        if (player_play(player)) {
            player_log(AV_LOG_ERROR, "Failed to play video.\n");
            goto end;
        }
        int64_t dur;
        if (!player_get_duration(player, &dur)) {
            if (duration) {
                *duration = dur / 1000;
            }
        } else {
            player_log(AV_LOG_WARNING, "Failed to get video duration.\n");
        }
        goto works;
    }
end:
    ok = OpenMediaFileAndGetDurationFunc(duration, arcName, videoName);
works:
    if (duration) {
        char tmp[32];
        int64_t dur = *duration * 1000;
        player_ts_make_string(tmp, dur);
        player_log(AV_LOG_INFO, "Video duration: %s\n", tmp);
    }
    return ok;
}

int64_t HookedIsPlaying() {
    if (player) {
        return player_is_playing(player);
    }
    return IsPlayingFunc();
}

int64_t HookedIsMediaPlaying() {
    if (player) {
        int64_t re = player_is_playing(player);
        // 释放播放器，BGI在播放完毕后不会手动释放
        if (!re) {
            player_log(AV_LOG_INFO, "Auto Close\n");
            player_free(&player);
        }
        return re;
    }
    int64_t re = IsMediaPlayingFunc();
    return re;
}

int64_t HookedReleaseDirectShowGraph() {
    player_log(AV_LOG_INFO, "BGI: Close\n");
    player_free(&player);
    return ReleaseDirectShowGraphFunc();
}

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

BOOL WINAPI HookedSetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
    if (vfs.ContainsHandle(hFile)) {
        return vfs.SetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
    }
    return TrueSetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
}

DWORD WINAPI HookedGetFileType(HANDLE hFile) {
    if (vfs.ContainsHandle(hFile)) {
        return FILE_TYPE_DISK;
    }
    return TrueGetFileType(hFile);
}

DWORD WINAPI HookedGetFileAttributesW(LPCWSTR lpFileName) {
    if (vfs.ContainsFile(lpFileName)) {
        return FILE_ATTRIBUTE_READONLY;
    }
    return TrueGetFileAttributesW(lpFileName);
}

BOOL WINAPI HookedGetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation) {
    if (vfs.ContainsFile(lpFileName)) {
        return vfs.GetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
    }
    return TrueGetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
}

extern "C" __declspec(dllexport) void Attach() {
    config.Load("config.txt");
    if (!wchar_util::str_to_wstr(defaultFont, config.configs["defaultFont"], CP_UTF8)) {
        defaultFont = L"微软雅黑";
    }
    if (defaultFont.empty()) {
        defaultFont = L"微软雅黑";
    }
    auto loggingFile = config.configs["loggingFile"];
    if (!loggingFile.empty()) {
        set_player_log_file(loggingFile.c_str(), config.IsAppendLogging() ? 1 : 0, config.LoggingLevel());
    }
    vfs.AddArchive("jewena-chs.dat");
    vfs.AddArchive("video.dat");
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    h = GetHandle();
    DetourAttach(&h, (PVOID)jis_to_utf8);
    OpenMediaFileAndGetDurationFunc = GetOpenMediaFileAndGetDuration();
    DetourAttach(&OpenMediaFileAndGetDurationFunc, HookedOpenMediaFileAndGetDuration);
    IsPlayingFunc = GetIsPlaying();
    DetourAttach(&IsPlayingFunc, HookedIsPlaying);
    IsMediaPlayingFunc = GetIsMediaPlaying();
    DetourAttach(&IsMediaPlayingFunc, HookedIsMediaPlaying);
    ReleaseDirectShowGraphFunc = GetReleaseDirectShowGraph();
    DetourAttach(&ReleaseDirectShowGraphFunc, HookedReleaseDirectShowGraph);
    DetourAttach(&TrueCreateFontW, HookedCreateFontW);
    DetourAttach(&TrueCreateFontA, HookedCreateFontA);
    DetourAttach(&TrueCreateFileW, HookedCreateFileW);
    DetourAttach(&TrueReadFile, HookedReadFile);
    DetourAttach(&TrueCloseHandle, HookedCloseHandle);
    DetourAttach(&TrueGetFileSize, HookedGetFileSize);
    DetourAttach(&TrueGetFileSizeEx, HookedGetFileSizeEx);
    DetourAttach(&TrueSetFilePointer, HookedSetFilePointer);
    DetourAttach(&TrueSetFilePointerEx, HookedSetFilePointerEx);
    DetourAttach(&TrueGetFileType, HookedGetFileType);
    DetourAttach(&TrueGetFileAttributesW, HookedGetFileAttributesW);
    DetourAttach(&TrueGetFileAttributesExW, HookedGetFileAttributesExW);
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
    DetourDetach(&TrueSetFilePointerEx, HookedSetFilePointerEx);
    DetourDetach(&TrueGetFileType, HookedGetFileType);
    DetourDetach(&TrueGetFileAttributesW, HookedGetFileAttributesW);
    DetourDetach(&TrueGetFileAttributesExW, HookedGetFileAttributesExW);
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
