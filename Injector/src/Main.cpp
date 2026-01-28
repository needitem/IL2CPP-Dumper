#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <string>
#include <vector>
#include "Injector.h"

#pragma comment(lib, "comdlg32.lib")

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Colors
#define CLR_BG          RGB(30, 30, 30)
#define CLR_CARD        RGB(45, 45, 45)
#define CLR_TEXT        RGB(230, 230, 230)
#define CLR_ACCENT      RGB(86, 156, 214)

// Window
#define WIN_W 500
#define WIN_H 420

// Control IDs
#define IDC_LIST        1001
#define IDC_BTN_REFRESH 1002
#define IDC_BTN_INJECT  1003
#define IDC_BTN_BROWSE  1004
#define IDC_EDIT_DLL    1005
#define IDC_STATUS      1006

struct ProcessInfo {
    DWORD pid;
    std::string name;
};

static HBRUSH hBrushBg, hBrushCard;
static HFONT hFontUI, hFontBold, hFontMono;
static HWND hWnd, hList, hEditDll, hStatus;
static std::vector<ProcessInfo> processes;
static std::string dllPath;

void Status(const std::string& text) {
    SetWindowTextA(hStatus, text.c_str());
}

std::string GetDefaultDllPath() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string path = exePath;
    size_t pos = path.find_last_of("\\/");
    if (pos != std::string::npos) {
        path = path.substr(0, pos + 1);
    }
    return path + "IL2CPP_Dumper.dll";
}

void RefreshProcessList() {
    processes.clear();
    SendMessage(hList, LB_RESETCONTENT, 0, 0);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (hProcess) {
                HMODULE hMods[1024];
                DWORD cbNeeded;
                bool hasGameAssembly = false;

                if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
                    for (DWORD i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
                        char modName[MAX_PATH];
                        if (GetModuleBaseNameA(hProcess, hMods[i], modName, sizeof(modName))) {
                            if (_stricmp(modName, "GameAssembly.dll") == 0) {
                                hasGameAssembly = true;
                                break;
                            }
                        }
                    }
                }

                if (hasGameAssembly) {
                    ProcessInfo info;
                    info.pid = pe.th32ProcessID;
                    // Convert wide string to narrow
                    char narrowName[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, pe.szExeFile, -1, narrowName, MAX_PATH, nullptr, nullptr);
                    info.name = narrowName;
                    processes.push_back(info);

                    char buf[512];
                    sprintf_s(buf, "[%d] %s", info.pid, info.name.c_str());
                    SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)buf);
                }

                CloseHandle(hProcess);
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    if (processes.empty()) {
        Status("No IL2CPP games found. Launch a game first.");
    } else {
        char buf[64];
        sprintf_s(buf, "Found %zu IL2CPP game(s)", processes.size());
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
        SetWindowTextA(hEditDll, dllPath.c_str());
    }
}

void DoInject() {
    int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel >= (int)processes.size()) {
        Status("Select a process first");
        return;
    }

    char path[MAX_PATH];
    GetWindowTextA(hEditDll, path, MAX_PATH);
    if (strlen(path) == 0) {
        Status("Select DLL path first");
        return;
    }

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        Status("DLL file not found");
        return;
    }

    Status("Injecting...");

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processes[sel].pid);
    if (!hProcess) {
        Status("Failed to open process");
        return;
    }

    std::string error;
    if (Injector::ManualMap(hProcess, path, error)) {
        CloseHandle(hProcess);
        Status("Injection successful!");
        MessageBoxA(hWnd, "DLL injected successfully!\n\nThe IL2CPP Dumper GUI should appear in the game.",
            "Success", MB_OK | MB_ICONINFORMATION);
    } else {
        CloseHandle(hProcess);
        Status(("Failed: " + error).c_str());
        MessageBoxA(hWnd, ("Injection failed:\n" + error).c_str(), "Error", MB_OK | MB_ICONERROR);
    }
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
    y += 40;

    // Label
    HWND hLbl1 = CreateWindowA("STATIC", "Running IL2CPP Games:", WS_CHILD | WS_VISIBLE,
        m, y, 200, 20, parent, nullptr, nullptr, nullptr);
    SendMessage(hLbl1, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    // Refresh button
    HWND hBtnRefresh = CreateWindowA("BUTTON", "Refresh", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m + w - 80, y - 3, 80, 26, parent, (HMENU)IDC_BTN_REFRESH, nullptr, nullptr);
    SendMessage(hBtnRefresh, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 28;

    // Process list
    hList = CreateWindowExA(0, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_BORDER,
        m, y, w, 140, parent, (HMENU)IDC_LIST, nullptr, nullptr);
    SendMessage(hList, WM_SETFONT, (WPARAM)hFontMono, TRUE);
    y += 155;

    // DLL Path label
    HWND hLbl2 = CreateWindowA("STATIC", "DLL Path:", WS_CHILD | WS_VISIBLE,
        m, y, 100, 20, parent, nullptr, nullptr, nullptr);
    SendMessage(hLbl2, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 25;

    // DLL path edit + browse button
    hEditDll = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        m, y, w - 90, 26, parent, (HMENU)IDC_EDIT_DLL, nullptr, nullptr);
    SendMessage(hEditDll, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hBtnBrowse = CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m + w - 80, y, 80, 26, parent, (HMENU)IDC_BTN_BROWSE, nullptr, nullptr);
    SendMessage(hBtnBrowse, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 45;

    // Inject button
    HWND hBtnInject = CreateWindowA("BUTTON", "Inject DLL (Manual Map)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m, y, w, 40, parent, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);
    SendMessage(hBtnInject, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 55;

    // Status
    hStatus = CreateWindowA("STATIC", "Click 'Refresh' to find IL2CPP games", WS_CHILD | WS_VISIBLE | SS_CENTER,
        m, y, w, 20, parent, (HMENU)IDC_STATUS, nullptr, nullptr);
    SendMessage(hStatus, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // Set default DLL path
    dllPath = GetDefaultDllPath();
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
                case IDC_BTN_INJECT: DoInject(); break;
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
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
