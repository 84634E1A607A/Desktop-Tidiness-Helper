// Desktop Tidiness Helper.cpp : Defines the entry point for the application.
//
#define _CRT_SECURE_NO_WARNINGS


#include "framework.h"
#include <stdio.h>
#include <Dbt.h>
#include <ShlObj.h>
#include <time.h>

#define MAX_PATH_LENGTH 260

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[] = L"DTH";                       // The title bar text
WCHAR szWindowClass[] = L"My Window Class";     // the main window class name
WCHAR szHomePath[MAX_PATH_LENGTH], szLogfilePath[MAX_PATH_LENGTH], szConfigfilePath[MAX_PATH_LENGTH], szQueuefilePath[MAX_PATH_LENGTH], szDesktopPath[MAX_PATH_LENGTH];
FILE* fpLogfile, * fpConfigfile, * fpQueuefile;

// Structs
struct DRIVE {
    DWORD uuid = 0;
    WCHAR path[MAX_PATH_LENGTH] = L"";
    DRIVE* pnext = nullptr;
} driveHead;
struct EXEMPT {
    WCHAR name[MAX_PATH_LENGTH] = L"";
    EXEMPT* pnext = nullptr;
} exemptHead;

const LPWSTR CurTime() {
    time_t t = time(nullptr);
    tm* ct = localtime(&t);
    static WCHAR szt[32] = L"";
    swprintf(szt, 32, L"%02d-%02d %02d:%02d:%02d", ct->tm_mon, ct->tm_mday, ct->tm_hour, ct->tm_min, ct->tm_sec);
    delete ct;
    return szt;
}

inline void DeleteQueue() {
    fpQueuefile = _wfopen(szQueuefilePath, L"r");
    if (!fpQueuefile) return;
    WCHAR fname[MAX_PATH_LENGTH] = L"";
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
            WCHAR key[MAX_PATH_LENGTH] = L"", value[MAX_PATH_LENGTH] = L"";
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

DWORD WINAPI Monitor(LPVOID lpParameter) {
    HANDLE hDirectory = CreateFile(szDesktopPath, FILE_LIST_DIRECTORY, FILE_SHARE_READ ,NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD Buf[1024], dwRet;
    BOOL bRet;
    FILE_NOTIFY_INFORMATION* pFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)&Buf;

    do
    {
        RtlZeroMemory(pFileNotifyInfo, 1024);
        bRet = ReadDirectoryChangesW(hDirectory, pFileNotifyInfo, 1024, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &dwRet, NULL, NULL);
    } while (bRet);
    return 0;
}

void SaveLog(HWND hWnd, UINT timerMsg, UINT_PTR tId, DWORD t) {
    fflush(fpLogfile);
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
    fwprintf(fpLogfile, L"[%ws]Program start\n", CurTime());

    // Init Config
    wsprintf(szConfigfilePath, L"%ws\\config.ini", szHomePath);
    fpConfigfile = _wfopen(szConfigfilePath, L"r");
    ReadConfig();
    fpConfigfile = _wfreopen(szConfigfilePath, L"a", fpConfigfile);

    // Init Queue
    wsprintf(szQueuefilePath, L"%ws\\queue", szHomePath);
    DeleteQueue();
    fpQueuefile = _wfopen(szQueuefilePath, L"w");

    // Init Desktop
    LPITEMIDLIST pidl;
    LPMALLOC pShellMalloc; 
    if (SUCCEEDED(SHGetMalloc(&pShellMalloc))) {
        if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidl))) {
            SHGetPathFromIDList(pidl, szDesktopPath);
            pShellMalloc->Free(pidl);
        }
        pShellMalloc->Release();
    }

    HANDLE hMonitor = CreateThread(nullptr, 0, Monitor, nullptr, 0, nullptr);

    SetTimer(hWnd, 0, 5000, SaveLog);

    return TRUE;
}

void ExitInstance() {
    fclose(fpConfigfile);
    fclose(fpQueuefile);
    fwprintf(fpLogfile, L"[%ws]Program Stopped", CurTime());
    fclose(fpLogfile);
}

DWORD WINAPI Indexer(LPVOID lpParameter) {
    LPWSTR volumeLetter = (LPWSTR)lpParameter;
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DESTROY: {
        ExitInstance();
        PostQuitMessage(0);
        break;
    }
                   // Device Change
    case WM_DEVICECHANGE: {
        PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
        PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
        WCHAR volumeLetter[] = L"C:";
        DWORD unitmask = lpdbv->dbcv_unitmask;
        switch (wParam)
        {
        case DBT_DEVICEARRIVAL: {
            for (char i = 0; i < 26; ++i) {
                if (!(unitmask & 0x1)) continue;
                volumeLetter[0] = i + 'A';
                WCHAR volumeName[MAX_PATH_LENGTH] = L"";
                DWORD serialNumber = 0;
                GetVolumeInformation(volumeLetter, volumeName, MAX_PATH_LENGTH, &serialNumber, nullptr, nullptr, nullptr, 0);
                fwprintf(fpLogfile, L"[%ws]Drive arrival, VolumeLetter=\"%ws\" Serial=%d, VolumeName=\"%ws\"\n", CurTime(), volumeLetter, serialNumber, volumeName);
                DRIVE* p = driveHead.pnext;
                while (p && p->uuid != serialNumber) p = p->pnext;
                if (!p) {
                    fwprintf(fpConfigfile, L"\n[%d]\n# VolumeName = %ws\nMovePath = \"\"\n", serialNumber, volumeName);
                    fflush(fpConfigfile);
                }
                else {
                    if (p->path[0] == L'\0') continue;
                    CreateThread(nullptr, 0, Indexer, volumeLetter, 0, nullptr);
                }
                unitmask >>= 1;
            }
            break;
        }
        case DBT_DEVICEREMOVECOMPLETE: {
            for (char i = 0; i < 26; ++i) {
                if (!(unitmask & 0x1)) continue;
                volumeLetter[0] = i + 'A';
                fwprintf(fpLogfile, L"[%ws]Drive remove, VolumeLetter=\"%ws\"\n", CurTime(), volumeLetter);
                unitmask >>= 1;
            }
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