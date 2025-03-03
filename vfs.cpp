#include "vfs.hpp"
#include "wchar_util.h"
#include "str_util.h"
#include "fileop.h"
#include "shlwapi.h"

VFS::VFS() {
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path = exePath;
    std::string pathStr;
    if (!wchar_util::wstr_to_str(pathStr, path, CP_UTF8)) {
        char buf[MAX_PATH];
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        pathStr = buf;
    }
    base_path = fileop::dirname(pathStr);
    base_path = str_util::str_replace(base_path, "/", "\\");
}

VFS::~VFS() {
    for (auto file : handles) {
        zip_fclose((zip_file_t*)file.first);
    }
    for (auto archive : archives) {
        zip_close(archive);
    }
}

bool VFS::AddArchive(std::string path) {
    zip_t* archive = zip_open(path.c_str(), ZIP_RDONLY, nullptr);
    if (!archive) return false;
    archives.push_back(archive);
    auto len = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < len; i++) {
        struct zip_stat st;
        zip_stat_init(&st);
        zip_stat_index(archive, i, 0, &st);
        // Skip directories/folders (directory entries usually end with a '/')
        if (st.name[strlen(st.name) - 1] == '/') {
            continue;
        }
        std::string name = st.name;
        name = str_util::str_replace(name, "/", "\\");
        files[name] = st.size;
    }
    return true;
}

bool VFS::ContainsFile(std::string path) {
    path = str_util::str_replace(path, "/", "\\");
    if (fileop::isabs(path)) {
        path = fileop::relpath(path, base_path);
    }
    return files.find(path) != files.end();
}

bool VFS::ContainsFile(std::wstring path) {
    std::string str;
    if (!wchar_util::wstr_to_str(str, path, CP_UTF8)) {
        return false;
    }
    return ContainsFile(str);
}

bool VFS::ContainsHandle(HANDLE hFile) {
    return handles.find(hFile) != handles.end();
}

HANDLE VFS::CreateFileW(std::wstring path) {
    std::string str;
    if (!wchar_util::wstr_to_str(str, path, CP_UTF8)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    str = fileop::relpath(str, base_path);
    str = str_util::str_replace(str, "\\", "/");
    auto c = files.find(str);
    if (c == files.end()) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    str = (*c).first;
    zip_t* archive = nullptr;
    zip_uint64_t index = 0;
    for (auto a : archives) {
        if (zip_name_locate(a, str.c_str(), 0) != -1) {
            archive = a;
            break;
        }
    }
    if (!archive) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    index = zip_name_locate(archive, str.c_str(), 0);
    zip_file_t* file = zip_fopen_index(archive, index, 0);
    handles[(HANDLE)file] = str;
    return (HANDLE)file;
}

bool VFS::ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead) {
    if (!ContainsHandle(hFile)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }
    zip_file_t* file = (zip_file_t*)hFile;
    if (!file) {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }
    zip_int64_t n = zip_fread(file, lpBuffer, nNumberOfBytesToRead);
    if (n == -1) {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }
    if (lpNumberOfBytesRead) {
        *lpNumberOfBytesRead = n;
    }
    return true;
}

void VFS::CloseHandle(HANDLE hFile) {
    if (!ContainsHandle(hFile)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return;
    }
    zip_fclose((zip_file_t*)hFile);
    handles.erase(hFile);
}

DWORD VFS::GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    auto f = handles.find(hFile);
    if (f == handles.end()) {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_FILE_SIZE;
    }
    auto data = *f;
    auto name = data.second;
    auto size = files[name];
    if (lpFileSizeHigh) {
        *lpFileSizeHigh = size >> 32;
    }
    return size;
}

DWORD VFS::SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    if (!ContainsHandle(hFile)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    zip_file_t* file = (zip_file_t*)hFile;
    if (!file) {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    zip_int64_t n = zip_fseek(file, lDistanceToMove, dwMoveMethod);
    if (n == -1) {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    return n;
}

std::string VFS::GetBasePath() {
    return base_path;
}
