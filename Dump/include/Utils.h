#pragma once

#include <string>
#include <cstdint>

namespace Utils {

void CreateDir(const std::string& path);
std::string AccessModifier(uint32_t flags);

} // namespace Utils
