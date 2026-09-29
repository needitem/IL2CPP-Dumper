#include "../include/Utils.h"
#include <Windows.h>
#include <cctype>
#include <cstdlib>

namespace Utils {

static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string WithTrailingSlash(std::string p) {
    if (!p.empty() && p.back() != '\\' && p.back() != '/') p += '\\';
    return p;
}

Config ParseConfig(const std::string& text) {
    Config cfg;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = Trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) { // legacy: bare path
            if (cfg.outDir.empty()) cfg.outDir = WithTrailingSlash(line);
            continue;
        }

        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        for (char& c : key) c = (char)tolower((unsigned char)c);

        if (key == "out")                        cfg.outDir = WithTrailingSlash(val);
        else if (key == "auto")                  { cfg.autoMode = val; for (char& c : cfg.autoMode) c = (char)tolower((unsigned char)c); }
        else if (key == "chunk")                 cfg.chunkSize = atoi(val.c_str());
        else if (key == "skipunity")             cfg.skipUnity = atoi(val.c_str()) != 0;
        else if (key == "skipsystem")            cfg.skipSystem = atoi(val.c_str()) != 0;
        else if (key == "skipprivate")           cfg.skipPrivate = atoi(val.c_str()) != 0;
        else if (key == "skipcompilergenerated") cfg.skipCompilerGenerated = atoi(val.c_str()) != 0;
    }
    if (cfg.chunkSize < 0) cfg.chunkSize = 0;
    return cfg;
}

const Config& GetConfig() {
    static Config cfg;
    static bool initialized = false;
    if (initialized) return cfg;
    initialized = true;

    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string gamePath(buf, n);
    size_t pos = gamePath.find_last_of("\\/");
    std::string gameDir = (pos != std::string::npos) ? gamePath.substr(0, pos + 1) : "";

    std::string cfgPath = gameDir + "IL2CPP_Dumper.cfg";
    HANDLE hFile = CreateFileA(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        std::string text(8192, '\0');
        DWORD bytesRead = 0;
        ReadFile(hFile, &text[0], (DWORD)text.size(), &bytesRead, nullptr);
        CloseHandle(hFile);
        text.resize(bytesRead);
        DeleteFileA(cfgPath.c_str());
        cfg = ParseConfig(text);
    }
    if (cfg.outDir.empty()) cfg.outDir = gameDir;
    return cfg;
}

std::string GetGameDir() {
    return GetConfig().outDir;
}

void CreateDir(const std::string& path) {
    CreateDirectoryA(path.c_str(), nullptr);
}

// MethodAttributes / FieldAttributes member-access values (flags & 7).
std::string AccessModifier(uint32_t flags) {
    switch (flags & 0x0007) {
        case 0x0006: return "public";
        case 0x0005: return "protected internal";
        case 0x0004: return "protected";
        case 0x0003: return "internal";
        case 0x0002: return "private protected";
        default:     return "private";
    }
}

} // namespace Utils
