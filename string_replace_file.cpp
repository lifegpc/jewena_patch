#include "string_replace_file.hpp"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "fileop.h"
#include "wchar_util.h"
#include <Windows.h>

bool StringReplaceFile::Load(std::string file) {
    size_t size;
    if (!fileop::get_file_size(file, size)) {
        return false;
    }
    std::wstring wfile;
    if (!wchar_util::str_to_wstr(wfile, file, CP_UTF8)) {
        return false;
    }
    HANDLE hFile = CreateFileW(wfile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    char* buffer = new char[size];
    DWORD read;
    if (!ReadFile(hFile, buffer, size, &read, NULL)) {
        CloseHandle(hFile);
        delete[] buffer;
        return false;
    }
    CloseHandle(hFile);
    rapidjson::Document doc;
    doc.Parse(buffer);
    delete[] buffer;
    if (doc.IsObject()) {
#undef GetObject
        for (auto& m : doc.GetObject()) {
            if (m.value.IsString()) {
                messages[m.name.GetString()] = m.value.GetString();
            }
        }
    }
    return true;
}

