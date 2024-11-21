#include <windows.h>
#include <iostream>

int main() {
    // 要启动的进程名
    const char* processName = "jewena.exe";
    // 要注入的 DLL 路径
    const char* dllPath = "jewena_patch.dll";

    // 启动进程
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);

    // 创建新进程
    if (!CreateProcessA(processName, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        std::cerr << "CreateProcess failed: " << GetLastError() << std::endl;
        return 1;
    }

    // 在新进程中分配内存以存放 DLL 路径
    LPVOID pDllPath = VirtualAllocEx(pi.hProcess, NULL, strlen(dllPath) + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pDllPath) {
        std::cerr << "VirtualAllocEx failed: " << GetLastError() << std::endl;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 将 DLL 路径写入新进程的内存
    if (!WriteProcessMemory(pi.hProcess, pDllPath, (LPVOID)dllPath, strlen(dllPath) + 1, NULL)) {
        std::cerr << "WriteProcessMemory failed: " << GetLastError() << std::endl;
        VirtualFreeEx(pi.hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    // 创建远程线程以加载 DLL
    HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"), pDllPath, 0, NULL);
    if (!hThread) {
        std::cerr << "CreateRemoteThread failed: " << GetLastError() << std::endl;
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

    std::cout << "DLL injected successfully." << std::endl;
    return 0;
}
