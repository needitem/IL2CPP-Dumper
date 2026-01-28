#include "../include/Utils.h"
#include <Windows.h>

namespace Utils {

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
