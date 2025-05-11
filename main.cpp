#include <windows.h>
#include <stdio.h>
#include "hash_lib.h"
#include "wchar_util.h"
#include "str_util.h"

using namespace hash_lib;

static std::string dir;

std::wstring FindStartProcess() {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0) {
        return L"";
    }
    std::string tPath;
    if (!wchar_util::wstr_to_str(tPath, path, CP_UTF8)) {
        return L"";
    }
    dir = fileop::dirname(tPath);
    dir = str_util::str_replace(dir, "/", "\\");
    std::string name = fileop::join(dir, "*.exe");
    std::wstring wName;
    if (!wchar_util::str_to_wstr(wName, name, CP_UTF8)) {
        return L"";
    }
    WIN32_FIND_DATAW data;
    auto re = FindFirstFileW(wName.c_str(), &data);
    if (re == INVALID_HANDLE_VALUE) {
        return L"";
    }
    do {
        std::string tmp;
        if (!wchar_util::wstr_to_str(tmp, data.cFileName, CP_UTF8)) {
            FindClose(re);
            return L"";
        }
        if (hashHexFile<SHA512>(tmp) == "01bb9a1b072faa2e10e5489c624eb3b65a7304fd1e32f9015779c341102439e3ec229aa9cb4e1c1fe76e42803320a9c68cbf62cfaa8b145ac7f1481eb37185dc") {
            FindClose(re);
            return data.cFileName;
        }
        BOOL r = FindNextFileW(re, &data);
        if (!r) {
            FindClose(re);
            if (GetLastError() == ERROR_NO_MORE_FILES) break;
            return L"";
        }
    } while (1);
    return L"";
}

void ShowErrorMsg(LPCWSTR text) {
    wchar_t* buf[1024];
    _swprintf((wchar_t *const)buf, L"%s%i", text, GetLastError());
    MessageBoxW(nullptr, (LPCWSTR)buf, L"错误消息", MB_OK);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 要启动的进程名
    auto processName = FindStartProcess();
    // 要注入的 DLL 路径
    const wchar_t* dllPath = L"jewena_patch.dll";

    // 启动进程
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);

    if (processName.empty()) {
        MessageBoxW(nullptr, L"无法找到游戏的启动文件。", L"错误", MB_ICONERROR);
        return 1;
    }

    if (hashHexFile<SHA512>(fileop::join(dir, "system.arc")) != "c69925d1016d1a4b327193bf97c6ac5d1940da0a5c5c28bd693d5655c33a6e0ea7c85e5fa057b98d9dde885f8bca320dee3e46f8b79913dc7a03cebddd1ac771") {
        MessageBoxW(nullptr, L"system.arc 校验失败。请检查游戏是否正确。", L"错误", MB_ICONERROR);
        return 1;
    }

    // 创建新进程
    if (!CreateProcessW(processName.c_str(), nullptr, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        ShowErrorMsg(L"CreateProcessW failed: ");
        return 1;
    }

    size_t memSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);

    // 在新进程中分配内存以存放 DLL 路径
    LPVOID pDllPath = VirtualAllocEx(pi.hProcess, NULL, memSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pDllPath) {
        ShowErrorMsg(L"VirtualAllocEx failed: ");
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 将 DLL 路径写入新进程的内存
    if (!WriteProcessMemory(pi.hProcess, pDllPath, (LPVOID)dllPath, memSize, NULL)) {
        ShowErrorMsg(L"WriteProcessMemory failed: ");
        VirtualFreeEx(pi.hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 创建远程线程以加载 DLL
    HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryW"), pDllPath, 0, NULL);
    if (!hThread) {
        ShowErrorMsg(L"CreateRemoteThread failed: ");
        VirtualFreeEx(pi.hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 等待线程完成
    WaitForSingleObject(hThread, INFINITE);

    // 清理
    VirtualFreeEx(pi.hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hThread);
    ResumeThread(pi.hThread); // 恢复新进程的执行
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
