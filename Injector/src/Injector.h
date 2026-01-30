#pragma once
#include <Windows.h>
#include <string>

using f_LoadLibraryA = HINSTANCE(WINAPI*)(const char* lpLibFilename);
using f_GetProcAddress = FARPROC(WINAPI*)(HMODULE hModule, LPCSTR lpProcName);
using f_DLL_ENTRY_POINT = BOOL(WINAPI*)(void* hDll, DWORD dwReason, void* pReserved);

#ifdef _WIN64
using f_RtlAddFunctionTable = BOOL(WINAPIV*)(PRUNTIME_FUNCTION FunctionTable, DWORD EntryCount, DWORD64 BaseAddress);
#endif

struct MANUAL_MAPPING_DATA {
    f_LoadLibraryA pLoadLibraryA;
    f_GetProcAddress pGetProcAddress;
#ifdef _WIN64
    f_RtlAddFunctionTable pRtlAddFunctionTable;
#endif
    BYTE* pbase;
    HINSTANCE hMod;
    DWORD fdwReasonParam;
    LPVOID reservedParam;
    BOOL SEHSupport;
};

namespace Injector {
    bool ManualMap(HANDLE hProc, const std::string& dllPath, std::string& error,
        bool clearHeader = true, bool clearNonNeeded = true, bool adjustProtections = true, bool sehSupport = true);
    bool ManualMap(HANDLE hProc, BYTE* pSrcData, SIZE_T fileSize, std::string& error,
        bool clearHeader = true, bool clearNonNeeded = true, bool adjustProtections = true, bool sehSupport = true);
    bool LoadLibraryInject(HANDLE hProc, const std::string& dllPath, std::string& error);
}
