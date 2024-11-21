#include <Windows.h>
#include "detours.h"
#include <stdio.h>

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

extern "C" __declspec(dllexport) void Attach() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    PVOID h = GetHandle();
    DetourAttach(&h, (PVOID)jis_to_utf8);
    DetourTransactionCommit();
}

extern "C" __declspec(dllexport) void Detach() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    PVOID h = GetHandle();
    DetourDetach(&h, (PVOID)jis_to_utf8);
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
