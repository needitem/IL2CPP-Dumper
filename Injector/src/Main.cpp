#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "Injector.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

// Colors
#define CLR_BG          RGB(30, 30, 30)
#define CLR_CARD        RGB(45, 45, 45)
#define CLR_TEXT        RGB(230, 230, 230)
#define CLR_ACCENT      RGB(86, 156, 214)

// Window
#define WIN_W 520
#define WIN_H 560

// Control IDs
#define IDC_LIST          1001
#define IDC_BTN_REFRESH   1002
#define IDC_BTN_INJECT    1003
#define IDC_BTN_BROWSE    1004
#define IDC_EDIT_DLL      1005
#define IDC_STATUS        1006
#define IDC_EDIT_EXE      1007
#define IDC_BTN_BROWSE_EXE 1008
#define IDC_BTN_LAUNCH    1009
#define IDC_RADIO_MMAP    1010
#define IDC_RADIO_LOADLIB 1011

struct ProcessInfo {
    DWORD pid;
    std::string name;
    std::string path;
    std::string runtime;
    USHORT machine = IMAGE_FILE_MACHINE_UNKNOWN;
};

static HBRUSH hBrushBg, hBrushCard;
static HFONT hFontUI, hFontBold, hFontMono;
static HWND hWnd, hList, hEditDll, hEditExe, hStatus;
static std::vector<ProcessInfo> processes;
static std::string dllPath;
static std::string exePath;
static bool gDllPathManuallySet = false;

static const DWORD kScanAccess =
    PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
static const DWORD kInjectAccess =
    PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;

USHORT GetCurrentMachine() {
#ifdef _WIN64
    return IMAGE_FILE_MACHINE_AMD64;
#else
    return IMAGE_FILE_MACHINE_I386;
#endif
}

const char* MachineToLabel(USHORT machine) {
    switch (machine) {
        case IMAGE_FILE_MACHINE_I386: return "x86";
        case IMAGE_FILE_MACHINE_AMD64: return "x64";
        case IMAGE_FILE_MACHINE_ARM64: return "arm64";
        default: return "unknown";
    }
}

USHORT GetProcessMachine(HANDLE hProcess) {
    using fnIsWow64Process2 = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    static fnIsWow64Process2 pIsWow64Process2 =
        (fnIsWow64Process2)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process2");

    if (pIsWow64Process2) {
        USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (pIsWow64Process2(hProcess, &processMachine, &nativeMachine)) {
            if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN) {
                return nativeMachine;
            }
            return processMachine;
        }
    }

    BOOL wow64 = FALSE;
    if (IsWow64Process(hProcess, &wow64)) {
#ifdef _WIN64
        return wow64 ? IMAGE_FILE_MACHINE_I386 : IMAGE_FILE_MACHINE_AMD64;
#else
        return IMAGE_FILE_MACHINE_I386;
#endif
    }

    return IMAGE_FILE_MACHINE_UNKNOWN;
}

std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string NormalizePath(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
        if (c == '/') return '\\';
        return (char)std::tolower(c);
    });
    return path;
}

std::string FileNameOnly(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

std::string GetProcessPath(HANDLE hProcess) {
    char path[MAX_PATH] = {};
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameA(hProcess, 0, path, &len)) {
        return std::string(path, len);
    }
    if (GetModuleFileNameExA(hProcess, nullptr, path, MAX_PATH)) {
        return std::string(path);
    }
    return "";
}

bool IsMonoModuleName(const std::string& lowerName) {
    return lowerName.find("mono") != std::string::npos ||
        lowerName.find("sgen") != std::string::npos ||
        lowerName.find("bdwgc") != std::string::npos ||
        lowerName.find("coreclr") != std::string::npos ||
        lowerName.find("hostfxr") != std::string::npos ||
        lowerName.find("runtime") != std::string::npos;
}

bool HasIl2CppExportsInModulePath(const std::string& modulePath) {
    static std::unordered_map<std::string, bool> cache;
    std::string key = NormalizePath(modulePath);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    HMODULE h = LoadLibraryExA(modulePath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!h) {
        cache[key] = false;
        return false;
    }

    bool ok = GetProcAddress(h, "il2cpp_domain_get") && GetProcAddress(h, "il2cpp_assembly_get_image");
    FreeLibrary(h);
    cache[key] = ok;
    return ok;
}

std::vector<ProcessInfo> EnumerateUnityProcesses() {
    std::vector<ProcessInfo> result;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            HANDLE hProcess = OpenProcess(kScanAccess, FALSE, pe.th32ProcessID);
            if (!hProcess) continue;

            HMODULE hMods[1024];
            DWORD cbNeeded = 0;
            bool hasIl2Cpp = false;
            bool hasMono = false;
            bool hasUnityPlayer = false;

            if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
                DWORD modCount = cbNeeded / sizeof(HMODULE);
                std::vector<std::string> modulePaths;
                modulePaths.reserve(modCount);

                for (DWORD i = 0; i < modCount; i++) {
                    char modName[MAX_PATH] = {};
                    if (!GetModuleBaseNameA(hProcess, hMods[i], modName, sizeof(modName))) continue;

                    std::string lower = ToLowerCopy(modName);
                    if (lower == "unityplayer.dll") hasUnityPlayer = true;
                    if (lower == "gameassembly.dll") hasIl2Cpp = true;
                    if (IsMonoModuleName(lower)) hasMono = true;

                    char modPath[MAX_PATH] = {};
                    if (GetModuleFileNameExA(hProcess, hMods[i], modPath, MAX_PATH)) {
                        modulePaths.push_back(modPath);
                    }
                }

                if (!hasIl2Cpp && hasUnityPlayer) {
                    for (const auto& path : modulePaths) {
                        if (HasIl2CppExportsInModulePath(path)) {
                            hasIl2Cpp = true;
                            break;
                        }
                    }
                }
            }

            if (hasIl2Cpp || hasUnityPlayer) {
                ProcessInfo info;
                info.pid = pe.th32ProcessID;
                info.machine = GetProcessMachine(hProcess);

                char narrowName[MAX_PATH];
                WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, narrowName, MAX_PATH, nullptr, nullptr);
                info.name = narrowName;
                info.path = GetProcessPath(hProcess);
                info.runtime = hasIl2Cpp ? "IL2CPP" : (hasMono ? "Mono" : "Unity");

                result.push_back(info);
            }

            CloseHandle(hProcess);
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return result;
}

int FindBestProcessForLaunch(const std::vector<ProcessInfo>& list,
    const std::string& targetExePath,
    const std::unordered_set<DWORD>& attemptedPids) {

    if (list.empty()) return -1;
    std::string targetPath = NormalizePath(targetExePath);
    std::string targetName = ToLowerCopy(FileNameOnly(targetExePath));

    int nameMatch = -1;
    int fallback = -1;

    for (size_t i = 0; i < list.size(); i++) {
        if (attemptedPids.count(list[i].pid)) continue;

        if (!list[i].path.empty() && NormalizePath(list[i].path) == targetPath) {
            return (int)i;
        }

        if (nameMatch == -1 && ToLowerCopy(list[i].name) == targetName) {
            nameMatch = (int)i;
        }

        if (fallback == -1) fallback = (int)i;
    }

    if (nameMatch != -1) return nameMatch;
    return fallback;
}

bool EnableDebugPrivilege() {
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!LookupPrivilegeValueA(nullptr, "SeDebugPrivilege", &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return false;
    }

    BOOL result = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hToken);
    return result && GetLastError() == ERROR_SUCCESS;
}

bool IsRunAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

void RestartAsAdmin() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);

    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.lpVerb = "runas";
    sei.lpFile = path;
    sei.nShow = SW_NORMAL;

    if (ShellExecuteExA(&sei)) {
        ExitProcess(0);
    }
}

void Status(const std::string& text) {
    SetWindowTextA(hStatus, text.c_str());
}

std::string GetDefaultDllPath() {
    char exePathBuf[MAX_PATH];
    GetModuleFileNameA(nullptr, exePathBuf, MAX_PATH);
    std::string path = exePathBuf;
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        path = path.substr(0, pos + 1);
    }
    return path + "IL2CPP_Dumper.dll";
}

std::string GetExecutableDir() {
    char exePathBuf[MAX_PATH];
    GetModuleFileNameA(nullptr, exePathBuf, MAX_PATH);
    std::string path = exePathBuf;
    size_t pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
}

std::string GetDefaultDllPathForMachine(USHORT machine) {
    std::string dir = GetExecutableDir();
    const char* name = (machine == IMAGE_FILE_MACHINE_I386) ? "IL2CPP_Dumper_x86.dll" : "IL2CPP_Dumper_x64.dll";
    std::string preferred = dir + name;
    if (GetFileAttributesA(preferred.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return preferred;
    }
    return GetDefaultDllPath();
}

void RefreshProcessList() {
    processes = EnumerateUnityProcesses();
    SendMessage(hList, LB_RESETCONTENT, 0, 0);

    for (const auto& info : processes) {
        char buf[512];
        sprintf_s(buf, "[%d] [%s/%s] %s", info.pid, info.runtime.c_str(), MachineToLabel(info.machine), info.name.c_str());
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)buf);
    }

    if (processes.empty()) {
        Status("No Unity process found. Launch target or use 'Launch & Inject'.");
    } else {
        char buf[64];
        sprintf_s(buf, "Found %zu Unity process(es)", processes.size());
        Status(buf);
    }
}

void BrowseDll() {
    char filename[MAX_PATH] = {0};

    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = "DLL Files\0*.dll\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        dllPath = filename;
        gDllPathManuallySet = true;
        SetWindowTextA(hEditDll, dllPath.c_str());
    }
}

void BrowseExe() {
    char filename[MAX_PATH] = {0};

    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = "Executable Files\0*.exe\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        exePath = filename;
        SetWindowTextA(hEditExe, exePath.c_str());
    }
}

std::string ResolveDllPathForTargetMachine(USHORT machine) {
    char currentPath[MAX_PATH] = {};
    GetWindowTextA(hEditDll, currentPath, MAX_PATH);
    std::string configured = currentPath;

    if (gDllPathManuallySet && !configured.empty()) {
        return configured;
    }

    std::string autoPath = GetDefaultDllPathForMachine(machine);
    if (!autoPath.empty()) {
        SetWindowTextA(hEditDll, autoPath.c_str());
    }
    return autoPath;
}

void WriteOutputConfig(HANDLE hProcess) {
    // Write injector's directory as output path to game directory
    char injectorPath[MAX_PATH];
    GetModuleFileNameA(nullptr, injectorPath, MAX_PATH);
    std::string injDir(injectorPath);
    size_t pos = injDir.find_last_of("\\/");
    if (pos != std::string::npos) injDir = injDir.substr(0, pos);

    // Get game exe path
    char gameExePath[MAX_PATH] = {};
    GetModuleFileNameExA(hProcess, nullptr, gameExePath, MAX_PATH);
    std::string gameDir(gameExePath);
    pos = gameDir.find_last_of("\\/");
    if (pos != std::string::npos) gameDir = gameDir.substr(0, pos + 1);

    std::string cfgPath = gameDir + "IL2CPP_Dumper.cfg";
    HANDLE hFile = CreateFileA(cfgPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, injDir.c_str(), (DWORD)injDir.size(), &written, nullptr);
        CloseHandle(hFile);
    }
}

bool InjectWithMethod(HANDLE hProcess, const char* dllFile, bool useMMap, std::string& error) {
    if (useMMap) {
        bool success = Injector::ManualMap(hProcess, dllFile, error);
        if (!success) {
            // Fallback to LoadLibrary
            std::string mmError = error;
            error.clear();
            success = Injector::LoadLibraryInject(hProcess, dllFile, error);
            if (!success) {
                error = "ManualMap: " + mmError + "\nLoadLibrary: " + error;
            }
        }
        return success;
    } else {
        return Injector::LoadLibraryInject(hProcess, dllFile, error);
    }
}

bool InjectWithSelectedMethod(HANDLE hProcess, const char* dllFile, std::string& error) {
    bool useMMap = (IsDlgButtonChecked(hWnd, IDC_RADIO_MMAP) == BST_CHECKED);
    return InjectWithMethod(hProcess, dllFile, useMMap, error);
}

bool RunHelperInjector(DWORD pid, USHORT targetMachine, const std::string& dllFile, bool useMMap, std::string& error) {
    std::string dir = GetExecutableDir();
    std::string helper = dir + ((targetMachine == IMAGE_FILE_MACHINE_I386) ? "IL2CPP_Injector_x86.exe" : "IL2CPP_Injector_x64.exe");
    if (GetFileAttributesA(helper.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "Missing helper injector: " + helper;
        return false;
    }

    std::string cmd = "\"" + helper + "\" --inject-pid " + std::to_string(pid)
        + " --dll \"" + dllFile + "\" --method " + (useMMap ? "mmap" : "loadlib");

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
        error = "CreateProcess(helper) failed: " + std::to_string(GetLastError());
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        error = "Helper injector failed with exit code " + std::to_string(exitCode);
        return false;
    }
    return true;
}

bool InjectProcessByPidDirect(DWORD pid, const std::string& dllFile, bool useMMap, std::string& error) {
    HANDLE hProcess = OpenProcess(kInjectAccess, FALSE, pid);
    if (!hProcess) {
        error = "Failed to open process: " + std::to_string(GetLastError());
        return false;
    }

    WriteOutputConfig(hProcess);
    bool success = InjectWithMethod(hProcess, dllFile.c_str(), useMMap, error);
    CloseHandle(hProcess);
    return success;
}

bool InjectProcessByPidAuto(DWORD pid, USHORT targetMachine, const std::string& dllFile, bool useMMap, std::string& error) {
    USHORT selfMachine = GetCurrentMachine();
    if (targetMachine != IMAGE_FILE_MACHINE_UNKNOWN && selfMachine != targetMachine) {
        return RunHelperInjector(pid, targetMachine, dllFile, useMMap, error);
    }
    return InjectProcessByPidDirect(pid, dllFile, useMMap, error);
}

void DoInject() {
    int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel >= (int)processes.size()) {
        Status("Select a process first");
        return;
    }

    std::string dllToUse = ResolveDllPathForTargetMachine(processes[sel].machine);
    if (dllToUse.empty()) {
        Status("Select DLL path first");
        return;
    }

    if (GetFileAttributesA(dllToUse.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Status("DLL file not found");
        return;
    }

    Status("Injecting...");
    bool useMMap = (IsDlgButtonChecked(hWnd, IDC_RADIO_MMAP) == BST_CHECKED);
    std::string error;
    bool success = InjectProcessByPidAuto(processes[sel].pid, processes[sel].machine, dllToUse, useMMap, error);

    if (success) {
        Status("Injection successful!");
        MessageBoxA(hWnd, "DLL injected successfully!\n\nThe dumper UI should appear in the target process.",
            "Success", MB_OK | MB_ICONINFORMATION);
    } else {
        Status(("Failed: " + error).c_str());
        MessageBoxA(hWnd, ("Injection failed:\n" + error).c_str(), "Error", MB_OK | MB_ICONERROR);
    }
}

void DoLaunchAndInject() {
    char gameExe[MAX_PATH];
    GetWindowTextA(hEditExe, gameExe, MAX_PATH);
    if (strlen(gameExe) == 0) {
        Status("Select game EXE first");
        return;
    }

    if (GetFileAttributesA(gameExe) == INVALID_FILE_ATTRIBUTES) {
        Status("Game EXE not found");
        return;
    }

    if (gDllPathManuallySet) {
        char dllFile[MAX_PATH];
        GetWindowTextA(hEditDll, dllFile, MAX_PATH);
        if (strlen(dllFile) == 0 || GetFileAttributesA(dllFile) == INVALID_FILE_ATTRIBUTES) {
            Status("DLL file not found");
            return;
        }
    }

    // Get working directory from exe path
    std::string workDir = gameExe;
    size_t pos = workDir.find_last_of("\\/");
    if (pos != std::string::npos) {
        workDir = workDir.substr(0, pos);
    }

    // Launch target normally via ShellExecute
    Status("Launching target...");
    HINSTANCE launchResult = ShellExecuteA(nullptr, "open", gameExe, nullptr, workDir.c_str(), SW_SHOW);
    if ((INT_PTR)launchResult <= 32) {
        Status("Failed to launch target EXE");
        MessageBoxA(hWnd, "Failed to launch target EXE.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    std::unordered_set<DWORD> attemptedPids;
    std::string lastError;
    for (int attempt = 1; attempt <= 30; attempt++) {
        char statusBuf[96];
        sprintf_s(statusBuf, "Waiting for target process... (%d/30)", attempt);
        Status(statusBuf);
        Sleep(3000);

        RefreshProcessList();
        if (processes.empty()) continue;

        int bestIndex = FindBestProcessForLaunch(processes, gameExe, attemptedPids);
        if (bestIndex < 0 || bestIndex >= (int)processes.size()) continue;

        SendMessage(hList, LB_SETCURSEL, bestIndex, 0);
        DWORD targetPid = processes[bestIndex].pid;

        std::string dllToUse = ResolveDllPathForTargetMachine(processes[bestIndex].machine);
        if (dllToUse.empty() || GetFileAttributesA(dllToUse.c_str()) == INVALID_FILE_ATTRIBUTES) {
            lastError = "DLL file not found for target architecture";
            break;
        }

        bool useMMap = (IsDlgButtonChecked(hWnd, IDC_RADIO_MMAP) == BST_CHECKED);
        lastError.clear();
        bool success = InjectProcessByPidAuto(targetPid, processes[bestIndex].machine, dllToUse, useMMap, lastError);

        if (success) {
            Status("Injection successful!");
            MessageBoxA(hWnd, "Target launched and DLL injected.\n\nThe dumper UI should appear in the target process.",
                "Success", MB_OK | MB_ICONINFORMATION);
            return;
        }

        attemptedPids.insert(targetPid);
        // may not be fully initialized yet, keep polling
    }

    Status(("Injection failed: " + lastError).c_str());
    MessageBoxA(hWnd,
        ("Injection failed after 30 attempts:\n" + lastError + "\n\nTry manual 'Refresh -> Inject' and select the exact process.").c_str(),
        "Error", MB_OK | MB_ICONERROR);
}

void BuildUI(HWND parent) {
    hFontUI = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    hFontBold = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    hFontMono = CreateFontA(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");

    hBrushBg = CreateSolidBrush(CLR_BG);
    hBrushCard = CreateSolidBrush(CLR_CARD);

    int m = 20;
    int y = 15;
    int w = WIN_W - m * 2 - 16;

    // Title
    HWND hTitle = CreateWindowA("STATIC", "IL2CPP Dumper Injector", WS_CHILD | WS_VISIBLE | SS_CENTER,
        m, y, w, 28, parent, nullptr, nullptr, nullptr);
    SendMessage(hTitle, WM_SETFONT, (WPARAM)CreateFontA(20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI"), TRUE);
    y += 45;

    // === Section: Launch Game ===
    HWND hLblLaunch = CreateWindowA("STATIC", "Launch Game", WS_CHILD | WS_VISIBLE,
        m, y, 200, 20, parent, nullptr, nullptr, nullptr);
    SendMessage(hLblLaunch, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 25;

    // Game EXE
    HWND hLblExe = CreateWindowA("STATIC", "Game EXE:", WS_CHILD | WS_VISIBLE,
        m, y + 3, 70, 20, parent, nullptr, nullptr, nullptr);
    SendMessage(hLblExe, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hEditExe = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        m + 75, y, w - 170, 26, parent, (HMENU)IDC_EDIT_EXE, nullptr, nullptr);
    SendMessage(hEditExe, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hBtnBrowseExe = CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m + w - 85, y, 85, 26, parent, (HMENU)IDC_BTN_BROWSE_EXE, nullptr, nullptr);
    SendMessage(hBtnBrowseExe, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 35;

    // Launch & Inject button
    HWND hBtnLaunch = CreateWindowA("BUTTON", "Launch && Inject", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m, y, w, 36, parent, (HMENU)IDC_BTN_LAUNCH, nullptr, nullptr);
    SendMessage(hBtnLaunch, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 50;

    // Separator
    HWND hSep = CreateWindowA("STATIC", "- OR attach to running process -", WS_CHILD | WS_VISIBLE | SS_CENTER,
        m, y, w, 20, parent, nullptr, nullptr, nullptr);
    SendMessage(hSep, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 30;

    // === Section: Running Processes ===
    HWND hLbl1 = CreateWindowA("STATIC", "Running Unity Processes:", WS_CHILD | WS_VISIBLE,
        m, y, 200, 20, parent, nullptr, nullptr, nullptr);
    SendMessage(hLbl1, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    HWND hBtnRefresh = CreateWindowA("BUTTON", "Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m + w - 80, y - 3, 80, 26, parent, (HMENU)IDC_BTN_REFRESH, nullptr, nullptr);
    SendMessage(hBtnRefresh, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 28;

    // Process list
    hList = CreateWindowExA(0, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_BORDER,
        m, y, w, 100, parent, (HMENU)IDC_LIST, nullptr, nullptr);
    SendMessage(hList, WM_SETFONT, (WPARAM)hFontMono, TRUE);
    y += 115;

    // Inject button
    HWND hBtnInject = CreateWindowA("BUTTON", "Inject DLL", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m, y, w, 36, parent, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);
    SendMessage(hBtnInject, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 50;

    // === Section: DLL Path ===
    HWND hLbl2 = CreateWindowA("STATIC", "DLL Path:", WS_CHILD | WS_VISIBLE,
        m, y, 100, 20, parent, nullptr, nullptr, nullptr);
    SendMessage(hLbl2, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 25;

    hEditDll = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        m, y, w - 90, 26, parent, (HMENU)IDC_EDIT_DLL, nullptr, nullptr);
    SendMessage(hEditDll, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hBtnBrowse = CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m + w - 80, y, 80, 26, parent, (HMENU)IDC_BTN_BROWSE, nullptr, nullptr);
    SendMessage(hBtnBrowse, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 35;

    // === Section: Injection Method ===
    HWND hLblMethod = CreateWindowA("STATIC", "Injection Method:", WS_CHILD | WS_VISIBLE,
        m, y, 130, 20, parent, nullptr, nullptr, nullptr);
    SendMessage(hLblMethod, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    HWND hRadioMMap = CreateWindowA("BUTTON", "ManualMap (Stealth)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        m + 135, y, 170, 20, parent, (HMENU)IDC_RADIO_MMAP, nullptr, nullptr);
    SendMessage(hRadioMMap, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hRadioLoadLib = CreateWindowA("BUTTON", "LoadLibrary (Compatible)",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        m + 310, y, 180, 20, parent, (HMENU)IDC_RADIO_LOADLIB, nullptr, nullptr);
    SendMessage(hRadioLoadLib, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // Default to ManualMap
    CheckRadioButton(parent, IDC_RADIO_MMAP, IDC_RADIO_LOADLIB, IDC_RADIO_MMAP);
    y += 30;

    // Status
    hStatus = CreateWindowA("STATIC", "Select target EXE or click Refresh", WS_CHILD | WS_VISIBLE | SS_CENTER,
        m, y, w, 20, parent, (HMENU)IDC_STATUS, nullptr, nullptr);
    SendMessage(hStatus, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // Set default DLL path
    gDllPathManuallySet = false;
    dllPath = GetDefaultDllPathForMachine(GetCurrentMachine());
    SetWindowTextA(hEditDll, dllPath.c_str());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            BuildUI(hwnd);
            return 0;

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, CLR_TEXT);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)hBrushBg;
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, CLR_TEXT);
            SetBkColor(hdc, CLR_CARD);
            return (LRESULT)hBrushCard;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, hBrushBg);
            return 1;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_REFRESH: RefreshProcessList(); break;
                case IDC_BTN_BROWSE: BrowseDll(); break;
                case IDC_BTN_BROWSE_EXE: BrowseExe(); break;
                case IDC_BTN_INJECT: DoInject(); break;
                case IDC_BTN_LAUNCH: DoLaunchAndInject(); break;
            }
            return 0;

        case WM_DESTROY:
            DeleteObject(hBrushBg);
            DeleteObject(hBrushCard);
            DeleteObject(hFontUI);
            DeleteObject(hFontBold);
            DeleteObject(hFontMono);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

struct CliOptions {
    bool injectMode = false;
    DWORD pid = 0;
    std::string dllPath;
    bool useMMap = true;
};

std::string WideToAnsi(LPCWSTR ws) {
    if (!ws) return "";
    int len = WideCharToMultiByte(CP_ACP, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string out((size_t)len - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, ws, -1, out.data(), len, nullptr, nullptr);
    return out;
}

bool ParseCliOptions(CliOptions& opts) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc <= 1) {
        if (argv) LocalFree(argv);
        return false;
    }

    for (int i = 1; i < argc; i++) {
        std::string arg = ToLowerCopy(WideToAnsi(argv[i]));
        if (arg == "--inject-pid" && i + 1 < argc) {
            opts.injectMode = true;
            opts.pid = (DWORD)strtoul(WideToAnsi(argv[++i]).c_str(), nullptr, 10);
        } else if (arg == "--dll" && i + 1 < argc) {
            opts.dllPath = WideToAnsi(argv[++i]);
        } else if (arg == "--method" && i + 1 < argc) {
            std::string m = ToLowerCopy(WideToAnsi(argv[++i]));
            opts.useMMap = (m != "loadlib");
        }
    }
    LocalFree(argv);
    return opts.injectMode;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    CliOptions cli;
    bool cliMode = ParseCliOptions(cli);

    // Check admin rights
    if (!IsRunAsAdmin()) {
        if (cliMode) return 2;
        int result = MessageBoxA(nullptr,
            "This application requires Administrator privileges to inject DLLs.\n\n"
            "Click OK to restart as Administrator, or Cancel to exit.",
            "Administrator Required", MB_OKCANCEL | MB_ICONWARNING);

        if (result == IDOK) {
            RestartAsAdmin();
        }
        return 0;
    }

    EnableDebugPrivilege();

    if (cliMode) {
        if (cli.pid == 0 || cli.dllPath.empty()) return 3;
        if (GetFileAttributesA(cli.dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) return 4;

        std::string error;
        bool ok = InjectProcessByPidDirect(cli.pid, cli.dllPath, cli.useMMap, error);
        return ok ? 0 : 5;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "IL2CPP_Injector";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    int x = (GetSystemMetrics(SM_CXSCREEN) - WIN_W) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - WIN_H) / 2;

    hWnd = CreateWindowExA(0, wc.lpszClassName, "IL2CPP Dumper Injector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, WIN_W, WIN_H, nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
