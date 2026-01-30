#include "../include/Utils.h"
#include <Windows.h>

namespace Utils {

std::string GetGameDir() {
    static std::string cachedDir;
    static bool initialized = false;

    if (initialized) return cachedDir;
    initialized = true;

    // Read output path from config file in game directory
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string gamePath(buf);
    size_t pos = gamePath.find_last_of("\\/");
    std::string gameDir = (pos != std::string::npos) ? gamePath.substr(0, pos + 1) : "";

    std::string cfgPath = gameDir + "IL2CPP_Dumper.cfg";
    HANDLE hFile = CreateFileA(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        char outBuf[MAX_PATH] = {};
        DWORD bytesRead = 0;
        ReadFile(hFile, outBuf, MAX_PATH - 1, &bytesRead, nullptr);
        CloseHandle(hFile);
        std::string result(outBuf, bytesRead);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' '))
            result.pop_back();
        if (!result.empty()) {
            if (result.back() != '\\' && result.back() != '/')
                result += '\\';
            DeleteFileA(cfgPath.c_str());
            cachedDir = result;
            return cachedDir;
        }
    }
    cachedDir = gameDir;
    return cachedDir;
}

void CreateDir(const std::string& path) {
    CreateDirectoryA(path.c_str(), nullptr);
}

std::string AccessModifier(uint32_t flags) {
    switch (flags & 0x0007) {
        case 0x0006: return "public";
        case 0x0005: return "internal";
        case 0x0004: return "protected";
        case 0x0003: return "protected";
        case 0x0001: return "private";
        default: return "private";
    }
}

} // namespace Utils
