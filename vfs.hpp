#include "zip.h"
#include <list>
#include <string>
#include <unordered_map>
#include <Windows.h>

class VFS {
    public:
        VFS();
        ~VFS();
        bool AddArchive(std::string path);
        bool ContainsFile(std::string path);
        bool ContainsFile(std::wstring path);
        bool ContainsHandle(HANDLE hFile);
        HANDLE CreateFileW(std::wstring path);
        bool ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead);
        void CloseHandle(HANDLE hFile);
        DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
        DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
        std::unordered_map<std::string, zip_uint64_t> files;
        std::string GetBasePath();
    private:
        std::string base_path;
        std::list<zip_t*> archives;
        std::unordered_map<HANDLE, std::string> handles;
};
