#pragma once
#include <Windows.h>
#include <string>

namespace ManualMap {
    bool Inject(DWORD pid, const std::string& dllPath, std::string& error);
    bool InjectToProcess(HANDLE hProcess, const std::string& dllPath, std::string& error);
}
