#include <windows.h>
#include <stdio.h>

void ShowErrorMsg(LPCWSTR text) {
    wchar_t* buf[1024];
    _swprintf((wchar_t *const)buf, L"%s%i", text, GetLastError());
    MessageBoxW(nullptr, (LPCWSTR)buf, L"错误消息", MB_OK);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 要启动的进程名
    const wchar_t* processName = L"jewena.exe";
    // 要注入的 DLL 路径
    const wchar_t* dllPath = L"jewena_patch.dll";

    // 启动进程
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);

    // 创建新进程
    if (!CreateProcessW((LPCWSTR)processName, nullptr, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
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
