// Desktop Tidiness Helper.cpp : Defines the entry point for the application.
//
#define _CRT_SECURE_NO_WARNINGS


#include "framework.h"
#include <stdio.h>
#include <Dbt.h>

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[] = L"DTH";                       // The title bar text
WCHAR szWindowClass[] = L"My Window Class";     // the main window class name
WCHAR szHomePath[128], szLogfilePath[128], szConfigfilePath[128], szQueuefilePath[128];
FILE* fpLogfile, * fpConfigfile, * fpQueuefile;

// Structs
struct DRIVE {
    DWORD uuid = 0;
    WCHAR path[128] = L"";
    DRIVE* pnext = nullptr;
} driveHead;

struct EXEMPT {
    WCHAR name[128] = L"";
    EXEMPT* pnext = nullptr;
} exemptHead;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    // Initialize global strings
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }


    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int) msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = nullptr;
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = nullptr;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = nullptr;

    return RegisterClassExW(&wcex);
}

inline void DeleteQueue() {
    fpQueuefile = _wfopen(szQueuefilePath, L"r");
    if (!fpQueuefile) return;
    WCHAR fname[128] = L"";
    while (fwscanf(fpQueuefile, L"%ws", fname) != -1)
    {
        DeleteFile(fname);
    }
    fclose(fpQueuefile);
}

// Trim; Delete Notes
inline void trim(LPWSTR str) {
    int ps = 0, pe = 0, len = lstrlenW(str);
    while (str[ps] == L' ' || str[ps] == L'\t') ps++;
    while (str[pe] != L'#' && pe < len) pe++;
    while (pe && (str[pe - 1] == L' ' || str[pe - 1] == L'\t' || str[pe - 1] == L'\n')) pe--;
    len = pe, str[len] = L'\0';
    for (int i = 0; i <= len; i++) str[i] = str[ps + i];
}

inline void ReadConfig() {
    if (!fpConfigfile) {
        fpConfigfile = _wfopen(szConfigfilePath, L"w");
        fwprintf(fpConfigfile, L"# Config\n\n");
        fclose(fpConfigfile);
    }
    else {
        WCHAR line[512] = L"";
        while (fgetws(line, 512, fpConfigfile)) {

            trim(line);
            int len = lstrlenW(line);
            if (!len) continue;

            // Drive UUID row
            if (line[0] == L'[') {
                if (line[len - 1] == L']') line[--len] = L'\0';
                DRIVE* nDrive = new DRIVE;
                nDrive->pnext = driveHead.pnext;
                nDrive->uuid = _wtoi(line + 1);
                driveHead.pnext = nDrive;
                continue;
            }

            // Config row
            WCHAR key[128] = L"", value[128] = L"";
            int p = 0;
            while (line[p] != L'=' && p < len) p++;
            if (p == len) {
                fwprintf(fpLogfile, L"Error in config file: %ws\n", line);
                continue;
            }
            memcpy(key, line, p * sizeof(WCHAR)), trim(key);
            lstrcpyW(value, line + p + 1), trim(value);
            
            if (!lstrcmpW(key, L"MovePath")) {
                if (!driveHead.pnext || driveHead.pnext->path[0] != L'\0') {
                    fwprintf(fpLogfile, L"Error in config file: %ws\n", line);
                    continue;
                }
                if (value[0] == L'\"') { 
                    value[lstrlenW(value) - 1] = L'\0';
                    lstrcpyW(driveHead.pnext->path, value + 1);
                }
                else {
                    lstrcpyW(driveHead.pnext->path, value);
                }
            }

            if (!lstrcmpW(key, L"Exempt")) {
                EXEMPT* nExempt = new EXEMPT;
                nExempt->pnext = exemptHead.pnext;
                if (value[0] == L'\"') {
                    value[lstrlenW(value) - 1] = L'\0';
                    lstrcpyW(nExempt->name, value + 1);
                }
                else {
                    lstrcpyW(nExempt->name, value);
                }
                exemptHead.pnext = nExempt;
            }
        }
    }
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    HWND hWnd;

    hInst = hInstance;

    hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

    if (!hWnd)
    {
        return FALSE;
    }
    lstrcpyW(szHomePath, _wgetenv(L"appdata"));
    lstrcatW(szHomePath, L"\\..\\Local\\Desktop Tidiness Helper");

    // Check if needed to create folder
    auto attr = GetFileAttributes(szHomePath);
    if (attr == -1 || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        CreateDirectory(szHomePath, NULL);

    // Init Log
    wsprintf(szLogfilePath, L"%ws\\main.log", szHomePath);
    fpLogfile = _wfopen(szLogfilePath, L"a");

    // Init Config
    wsprintf(szConfigfilePath, L"%ws\\config.ini", szHomePath);
    fpConfigfile = _wfopen(szConfigfilePath, L"r");
    ReadConfig();
    fpConfigfile = _wfreopen(szConfigfilePath, L"a", fpConfigfile);

    // Init Queue
    wsprintf(szQueuefilePath, L"%ws\\queue", szHomePath);
    DeleteQueue();
    fpQueuefile = _wfopen(szQueuefilePath, L"w");

    // Init Timer
    SetTimer(hWnd, 1, 3000, nullptr);

    return TRUE;
}

inline char FirstDriveFromMask(ULONG unitmask) {
    char i;
    for (i = 0; i < 26; ++i) {
        if (unitmask & 0x1) break;
        unitmask >>= 1;
    }
    return i + 'A';
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TIMER: {
        //detect_file_moves();
        break; 
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        break;
    }
    case WM_DEVICECHANGE: {
        PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
        PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
        WCHAR szMsg[80], volumeLetter[] = L"C:";
        switch (wParam)
        {
        case DBT_DEVICEARRIVAL: {
            volumeLetter[0] = FirstDriveFromMask(lpdbv->dbcv_unitmask);
            WCHAR volumeName[128] = L"";
            DWORD serialNumber = 0;
            GetVolumeInformation(volumeLetter, volumeName, 128, &serialNumber, nullptr, nullptr, nullptr, 0);
            DRIVE* p = driveHead.pnext;
            while (p && p->uuid != serialNumber) p = p->pnext;
            if (!p) {
                fwprintf(fpConfigfile, L"\n[%d]\nMovePath = \"\"\n", serialNumber);
                fpConfigfile = _wfreopen(szConfigfilePath, L"a", fpConfigfile);
            }
            break;
        }
        case DBT_DEVICEREMOVECOMPLETE: {
            volumeLetter[0] = FirstDriveFromMask(lpdbv->dbcv_unitmask);
            break;
        }
        default: {
            break;
        }
        }
        break;
    }
    default: {
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    }
    return 0;
}