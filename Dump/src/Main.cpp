#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <Uxtheme.h>
#include "../include/Resource.h"
#include "../include/Dumper.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Colors
#define CLR_BG          RGB(30, 30, 30)
#define CLR_CARD        RGB(45, 45, 45)
#define CLR_TEXT        RGB(230, 230, 230)
#define CLR_TEXT_DIM    RGB(150, 150, 150)
#define CLR_ACCENT      RGB(86, 156, 214)
#define CLR_SUCCESS     RGB(78, 201, 176)
#define CLR_BTN         RGB(60, 60, 60)
#define CLR_BTN_HOVER   RGB(80, 80, 80)
#define CLR_PROGRESS_BG RGB(60, 60, 60)
#define CLR_PROGRESS    RGB(86, 156, 214)

// Window
#define WIN_W 520
#define WIN_H 620

// Brushes
static HBRUSH hBrushBg, hBrushCard, hBrushBtn;
static HFONT hFontUI, hFontBold, hFontMono, hFontTitle;

// Controls
static HWND hWnd;
static HWND hRadio[3];
static HWND hChkFilter[4];
static HWND hChkOutput[3];
static HWND hBtnStart, hBtnFolder;
static HWND hProgress, hStatus, hLog;
static HWND hLblCount;
static bool isDumping = false;

void Log(const std::string& text) {
    SendMessageA(hLog, EM_SETSEL, -1, -1);
    SendMessageA(hLog, EM_REPLACESEL, FALSE, (LPARAM)(text + "\r\n").c_str());
    SendMessageA(hLog, EM_SCROLLCARET, 0, 0);
}

void Status(const std::string& text) {
    SetWindowTextA(hStatus, text.c_str());
}

void Progress(int val, int max, const std::string& item = "") {
    SendMessage(hProgress, PBM_SETRANGE32, 0, max);
    SendMessage(hProgress, PBM_SETPOS, val, 0);
    if (!item.empty()) {
        Status(item);
    }
}

void RefreshUI() {
    bool human = SendMessage(hRadio[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool custom = SendMessage(hRadio[2], BM_GETCHECK, 0, 0) == BST_CHECKED;

    for (int i = 0; i < 4; i++) EnableWindow(hChkFilter[i], !human);
    for (int i = 0; i < 3; i++) EnableWindow(hChkOutput[i], custom);
    EnableWindow(hBtnStart, !isDumping);
}

void OnStart() {
    if (isDumping) return;
    isDumping = true;
    RefreshUI();
    SetWindowTextA(hBtnStart, "  Exporting...");
    SendMessage(hLog, WM_SETTEXT, 0, (LPARAM)"");
    Progress(0, 100);

    std::thread([]() {
        Log("Initializing IL2CPP API...");

        Dumper d;
        d.OnLog(Log);
        d.OnProgress(Progress);

        bool human = SendMessage(hRadio[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
        bool ai = SendMessage(hRadio[1], BM_GETCHECK, 0, 0) == BST_CHECKED;

        if (!human) {
            d.filters.skipUnityEngine = SendMessage(hChkFilter[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
            d.filters.skipSystem = SendMessage(hChkFilter[1], BM_GETCHECK, 0, 0) == BST_CHECKED;
            d.filters.skipPrivate = SendMessage(hChkFilter[2], BM_GETCHECK, 0, 0) == BST_CHECKED;
            d.filters.skipCompilerGenerated = SendMessage(hChkFilter[3], BM_GETCHECK, 0, 0) == BST_CHECKED;
        }

        if (d.images.empty()) {
            Log("");
            Log("[!] No assemblies found");
            Log("    Make sure the game is fully loaded");
            Status("Error: No assemblies");
        } else {
            char buf[64];
            sprintf_s(buf, "Found %zu assemblies", d.images.size());
            SetWindowTextA(hLblCount, buf);
            Log("");
            Log(std::string("[+] ") + buf);
            Log("");

            if (human) {
                d.ExportHuman();
            } else if (ai) {
                d.ExportAI();
            } else {
                bool cs = SendMessage(hChkOutput[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
                bool json = SendMessage(hChkOutput[1], BM_GETCHECK, 0, 0) == BST_CHECKED;
                bool sum = SendMessage(hChkOutput[2], BM_GETCHECK, 0, 0) == BST_CHECKED;
                d.ExportCustom(cs, json, sum);
            }
            Status("Complete");
        }

        Log("");
        Log("Done.");
        Progress(100, 100);

        isDumping = false;
        SetWindowTextA(hBtnStart, "  Start Export");
        EnableWindow(hBtnFolder, TRUE);
        RefreshUI();
    }).detach();
}

void OnOpenFolder() {
    bool human = SendMessage(hRadio[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
    ShellExecuteA(nullptr, "explore", human ? "C:\\IL2CPP_Dump" : "C:\\IL2CPP_Dump_JSON", nullptr, nullptr, SW_SHOW);
}

// Custom dark controls
LRESULT CALLBACK DarkEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (msg) {
        case WM_NCPAINT: {
            HDC hdc = GetWindowDC(hwnd);
            RECT rc; GetWindowRect(hwnd, &rc);
            rc.right -= rc.left; rc.bottom -= rc.top; rc.left = rc.top = 0;
            HBRUSH br = CreateSolidBrush(CLR_CARD);
            FrameRect(hdc, &rc, br);
            DeleteObject(br);
            ReleaseDC(hwnd, hdc);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void BuildUI(HWND parent) {
    // Fonts
    hFontUI = CreateFontA(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    hFontBold = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    hFontMono = CreateFontA(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Cascadia Code");
    hFontTitle = CreateFontA(22, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    // Brushes
    hBrushBg = CreateSolidBrush(CLR_BG);
    hBrushCard = CreateSolidBrush(CLR_CARD);
    hBrushBtn = CreateSolidBrush(CLR_BTN);

    int m = 20;
    int y = 15;
    int cardW = WIN_W - m * 2 - 16;

    // Title
    HWND hTitle = CreateWindowA("STATIC", "IL2CPP Dumper", WS_CHILD | WS_VISIBLE | SS_CENTER,
        m, y, cardW, 30, parent, nullptr, nullptr, nullptr);
    SendMessage(hTitle, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
    y += 35;

    // Subtitle
    hLblCount = CreateWindowA("STATIC", "Waiting for export...", WS_CHILD | WS_VISIBLE | SS_CENTER,
        m, y, cardW, 18, parent, nullptr, nullptr, nullptr);
    SendMessage(hLblCount, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 30;

    // Card 1: Output Mode
    auto MakeSection = [&](const char* title, int height) -> int {
        HWND lbl = CreateWindowA("STATIC", title, WS_CHILD | WS_VISIBLE,
            m + 10, y, 200, 18, parent, nullptr, nullptr, nullptr);
        SendMessage(lbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        y += 25;
        return height;
    };

    MakeSection("Output Mode", 0);

    const char* modeLabels[] = { "Human (C# code)", "AI (JSON filtered)", "Custom" };
    const char* modeDesc[] = { "Complete dump for analysis", "Optimized for LLM", "Select outputs below" };

    for (int i = 0; i < 3; i++) {
        hRadio[i] = CreateWindowA("BUTTON", modeLabels[i],
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0),
            m + 15, y, 150, 20, parent, (HMENU)(INT_PTR)(IDC_RADIO_HUMAN + i), nullptr, nullptr);
        SendMessage(hRadio[i], WM_SETFONT, (WPARAM)hFontUI, TRUE);

        HWND desc = CreateWindowA("STATIC", modeDesc[i], WS_CHILD | WS_VISIBLE,
            m + 170, y + 2, 200, 16, parent, nullptr, nullptr, nullptr);
        SendMessage(desc, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        y += 26;
    }
    SendMessage(hRadio[1], BM_SETCHECK, BST_CHECKED, 0);
    y += 15;

    // Card 2: Filters
    MakeSection("Filters", 0);

    const char* filterLabels[] = { "Skip UnityEngine.*", "Skip System.*", "Skip private members", "Skip compiler-generated" };
    int col1 = m + 15, col2 = m + 220;

    for (int i = 0; i < 4; i++) {
        int x = (i % 2 == 0) ? col1 : col2;
        if (i == 2) y += 26;

        hChkFilter[i] = CreateWindowA("BUTTON", filterLabels[i],
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            x, y, 180, 20, parent, (HMENU)(INT_PTR)(IDC_CHK_UNITY + i), nullptr, nullptr);
        SendMessage(hChkFilter[i], WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SendMessage(hChkFilter[i], BM_SETCHECK, BST_CHECKED, 0);
    }
    y += 40;

    // Card 3: Custom Output
    MakeSection("Custom Output Formats", 0);

    const char* outLabels[] = { "C# (.cs)", "JSON Full", "JSON Summary" };
    for (int i = 0; i < 3; i++) {
        hChkOutput[i] = CreateWindowA("BUTTON", outLabels[i],
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            m + 15 + i * 140, y, 120, 20, parent, (HMENU)(INT_PTR)(IDC_CHK_CS + i), nullptr, nullptr);
        SendMessage(hChkOutput[i], WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SendMessage(hChkOutput[i], BM_SETCHECK, BST_CHECKED, 0);
    }
    y += 40;

    // Buttons
    hBtnStart = CreateWindowA("BUTTON", "  Start Export",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        m, y, 160, 38, parent, (HMENU)IDC_BTN_START, nullptr, nullptr);
    SendMessage(hBtnStart, WM_SETFONT, (WPARAM)hFontBold, TRUE);

    hBtnFolder = CreateWindowA("BUTTON", "  Open Folder",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
        m + 170, y, 140, 38, parent, (HMENU)IDC_BTN_FOLDER, nullptr, nullptr);
    SendMessage(hBtnFolder, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 50;

    // Progress
    hProgress = CreateWindowExA(0, PROGRESS_CLASSA, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        m, y, cardW, 8, parent, nullptr, nullptr, nullptr);
    SendMessage(hProgress, PBM_SETBARCOLOR, 0, CLR_PROGRESS);
    SendMessage(hProgress, PBM_SETBKCOLOR, 0, CLR_PROGRESS_BG);
    y += 15;

    // Status
    hStatus = CreateWindowA("STATIC", "Ready", WS_CHILD | WS_VISIBLE,
        m, y, cardW, 18, parent, nullptr, nullptr, nullptr);
    SendMessage(hStatus, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += 28;

    // Log label
    HWND logLbl = CreateWindowA("STATIC", "Output Log", WS_CHILD | WS_VISIBLE,
        m, y, 100, 18, parent, nullptr, nullptr, nullptr);
    SendMessage(logLbl, WM_SETFONT, (WPARAM)hFontBold, TRUE);
    y += 22;

    // Log box
    hLog = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        m, y, cardW, WIN_H - y - 50, parent, nullptr, nullptr, nullptr);
    SendMessage(hLog, WM_SETFONT, (WPARAM)hFontMono, TRUE);
    SetWindowSubclass(hLog, DarkEditProc, 0, 0);

    RefreshUI();
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
            SetBkColor(hdc, CLR_BG);
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)hBrushBg;
        }

        case WM_CTLCOLOREDIT: {
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
                case IDC_BTN_START: OnStart(); break;
                case IDC_BTN_FOLDER: OnOpenFolder(); break;
                case IDC_RADIO_HUMAN:
                case IDC_RADIO_AI:
                case IDC_RADIO_CUSTOM:
                    RefreshUI();
                    break;
            }
            return 0;

        case WM_CLOSE:
            if (isDumping) {
                if (MessageBoxA(hwnd, "Export in progress. Close anyway?", "IL2CPP Dumper", MB_YESNO | MB_ICONWARNING) != IDYES)
                    return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            DeleteObject(hBrushBg);
            DeleteObject(hBrushCard);
            DeleteObject(hBrushBtn);
            DeleteObject(hFontUI);
            DeleteObject(hFontBold);
            DeleteObject(hFontMono);
            DeleteObject(hFontTitle);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void Run(HMODULE hMod) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hMod;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = "IL2CPP_Dumper_Window";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    int x = (GetSystemMetrics(SM_CXSCREEN) - WIN_W) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - WIN_H) / 2;

    hWnd = CreateWindowExA(
        WS_EX_TOPMOST,
        wc.lpszClassName,
        "IL2CPP Dumper",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, WIN_W, WIN_H,
        nullptr, nullptr, hMod, nullptr
    );

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

DWORD WINAPI Entry(LPVOID param) {
    Run((HMODULE)param);
    FreeLibraryAndExitThread((HMODULE)param, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, Entry, hModule, 0, nullptr);
    }
    return TRUE;
}
