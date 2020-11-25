#define _CRT_SECURE_NO_WARNINGS


#include "framework.h"
#include "resource.h"
#include <stdio.h>
#include <Dbt.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <time.h>
#include <mutex>
#include <vector>
#include <algorithm>

#define WM_TRAYICON (WM_USER + 1)

using namespace std;

// Global Variables:
HINSTANCE hInst;                                // Current instance
WCHAR szTitle[] = L"DTH";                       // The title bar text
WCHAR szWindowClass[] = L"My Window Class";     // The main window class name
WCHAR szgLogs[][128] = {                        // {0: Start, 1: Cfg Load, 2: Device Arrival, 3: Device Removal, 4: Err fMoving, 5: Cfg Created, 6: Complete fMoving, 7: Err tMonitor, 8: Cfg Chg, 9: Cfg Reload, 10: Log Created}
    L"\n[%ws] Program Start\n",
    L"[%ws] Config Loaded\n",
    L"[%ws] Device Inserted, VolumeName=\"%ws\", VolumeLetter=\"%ws\"\n",
    L"[%ws] Device Ejected, VolumeLetter=\"%ws\"\n",
    L"[%ws] Error encontered when moving file \"%ws\": %ws",
    L"[%ws] Config Created\n",
    L"[%ws] File move complete: \"%ws\" --> \"%ws\"\n",
    L"[%ws] Error encontered when opening monitor: %wsVardump: DesktopPath=\"%ws\"",
    L"[%ws] Config is being changed, program paused\n",
    L"[%ws] Config reloaded, program continues\n",
    L"[%ws] Log created\n"
};
WCHAR szLogBuffer[256];
WCHAR szHomePath[MAX_PATH], szConfigfilePath[MAX_PATH], szQueuefilePath[MAX_PATH], szLogfilePath[MAX_PATH], szDesktopPath[MAX_PATH];
HANDLE hConfigfile, hQueuefile, hLogfile;
NOTIFYICONDATA NotifyIconData;
bool bPaused;

// Structs
struct FILEINFO {
    WCHAR name[MAX_PATH] = L"";
    DWORD size = 0;
    FILEINFO(LPWSTR n, DWORD s) { lstrcpyW(name, n); size = s; }
    FILEINFO() {}
    bool operator< (const FILEINFO r) const {
        return lstrcmpW(this->name, r.name) < 0;
    }
    bool operator== (const FILEINFO r) const {
        return !lstrcmpW(this->name, r.name) && this->size == r.size;
    }
};
struct DRIVE {
    DWORD uuid = 0;
    WCHAR path[MAX_PATH] = L"";
    WCHAR letter[3] = L"";
    vector<FILEINFO> files;
    DRIVE* pnext = nullptr;
} driveHead;
struct EXEMPT {
    WCHAR name[MAX_PATH] = L"";
    EXEMPT* pnext = nullptr;
} exemptHead;

inline void WriteLog() {
    WriteFile(hLogfile, szLogBuffer, lstrlen(szLogBuffer) * sizeof(WCHAR), nullptr, nullptr);
    FlushFileBuffers(hLogfile);
}

const LPWSTR CurTime() {
    time_t t = time(nullptr);
    tm* ct = localtime(&t);
    static WCHAR szt[32] = L"";
    swprintf(szt, 32, L"%02d-%02d %02d:%02d:%02d", ct->tm_mon, ct->tm_mday, ct->tm_hour, ct->tm_min, ct->tm_sec);
    return szt;
}

inline void DeleteQueue() {
    hQueuefile = CreateFile(szQueuefilePath, FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (hQueuefile == INVALID_HANDLE_VALUE) return;
    DWORD fSize = GetFileSize(hQueuefile, nullptr);
    DWORD count = fSize / MAX_PATH;
    WCHAR fname[MAX_PATH] = L"";
    for (DWORD i = 0; i < count; i++)
    {
        if (!ReadFile(hQueuefile, fname, MAX_PATH * sizeof(WCHAR), nullptr, nullptr)) break;
        DeleteFile(fname);
    }
    CloseHandle(hQueuefile);
}

// Trim; Delete Notes
inline void trim(LPWSTR str) {
    int ps = 0, pe = 0, len = lstrlen(str);
    while (str[ps] == L' ' || str[ps] == L'\t') ps++;
    while (str[pe] != L'#' && pe < len) pe++;
    while (pe && (str[pe - 1] == L' ' || str[pe - 1] == L'\t' || str[pe - 1] == L'\n')) pe--;
    len = pe, str[len] = L'\0';
    for (int i = 0; i <= len; i++) str[i] = str[ps + i];
}

inline void ReadConfig() {
    if (hConfigfile == INVALID_HANDLE_VALUE) {
        wsprintf(szLogBuffer, szgLogs[5], CurTime());
        WriteLog();
        hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        WCHAR szString1[] = L"# Config\n\n";
        WriteFile(hConfigfile, szString1, sizeof(szString1) - sizeof(WCHAR), nullptr, nullptr);
    }
    else {
        WCHAR line[512] = L"";
        DWORD fSize = GetFileSize(hConfigfile, nullptr);
        LPWSTR pFileContent = new WCHAR[(long long)fSize / 2 + 1];
        RtlZeroMemory(pFileContent, fSize + sizeof(WCHAR));
        if (!ReadFile(hConfigfile, pFileContent, fSize, nullptr, nullptr)) return;
        WCHAR* ps = pFileContent, * pe = pFileContent - 1;
        while (true) {
            if (*pe == L'\0') break;
            ps = pe + 1; pe = ps;
            while (*pe != L'\n' && *pe != L'\0') pe++;
            RtlZeroMemory(line, 512 * sizeof(WCHAR));
            memcpy(line, ps, (pe - ps + 1) * sizeof(WCHAR));

            trim(line);
            int len = lstrlen(line);
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
            WCHAR key[MAX_PATH] = L"", value[MAX_PATH] = L"";
            int p = 0;
            while (line[p] != L'=' && p < len) p++;
            if (p == len) continue;
            memcpy(key, line, p * sizeof(WCHAR)), trim(key);
            lstrcpyW(value, line + p + 1), trim(value);
            
            if (!lstrcmpW(key, L"MovePath")) {
                if (!driveHead.pnext || driveHead.pnext->path[0] != L'\0') continue;
                if (value[0] == L'\"') { 
                    value[lstrlen(value) - 1] = L'\0';
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
                    value[lstrlen(value) - 1] = L'\0';
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
    HANDLE hDirectory = CreateFile(szDesktopPath, FILE_LIST_DIRECTORY, 
        FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    
    if (hDirectory == INVALID_HANDLE_VALUE) {
        // Log open directory error
        DWORD Err = GetLastError();
        WCHAR szErrorMsg[128] = L"";
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, Err, 0, szErrorMsg, 128, nullptr);
        wsprintf(szLogBuffer, szgLogs[7], CurTime(), szErrorMsg, szDesktopPath);
        return -1;
    }
    
    DWORD* Buf = new DWORD[8192], *Buf2 = new DWORD[1024], dwRet;
    FILE_NOTIFY_INFORMATION* pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)Buf, *pFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)Buf2;
    while (true) {
        RtlZeroMemory(pFirstFileNotifyInfo, 8192);
        BOOL Ret = ReadDirectoryChangesW(hDirectory, pFirstFileNotifyInfo, 8192, FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
            &dwRet, NULL, NULL);
        if (!Ret) {
            // Log open directory error
            DWORD Err = GetLastError();
            WCHAR szErrorMsg[128] = L"";
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, Err, 0, szErrorMsg, 128, nullptr);
            wsprintf(szLogBuffer, szgLogs[7], CurTime(), szErrorMsg, szDesktopPath);
            return -1;
        }
        if (pFirstFileNotifyInfo->Action == FILE_ACTION_ADDED) {
            do {
                RtlZeroMemory(pFileNotifyInfo, 1024);
                memcpy(pFileNotifyInfo, pFirstFileNotifyInfo, pFirstFileNotifyInfo->NextEntryOffset ? pFirstFileNotifyInfo->NextEntryOffset - 1 : 1024);
                pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)((BYTE*)pFirstFileNotifyInfo + pFirstFileNotifyInfo->NextEntryOffset);
                if (!lstrcmpW(pFileNotifyInfo->FileName + pFileNotifyInfo->FileNameLength - 4, L".lnk")) continue;
                WCHAR fname[MAX_PATH];
                wsprintf(fname, L"%ws\\%ws", szDesktopPath, pFileNotifyInfo->FileName);
                WIN32_FIND_DATAW fdata;
                FindClose(FindFirstFile(fname, &fdata));
                FILEINFO fInfo = { fdata.cFileName, fdata.nFileSizeLow };
                DRIVE* p = driveHead.pnext;
                while (p) {
                    auto pos = find(p->files.begin(), p->files.end(), fInfo);
                    if (pos != p->files.end()) break;
                    p = p->pnext;
                }
                if (!p) continue;
                WCHAR szMovedName[MAX_PATH];
                wsprintf(szMovedName, L"%ws\\%ws\\%ws", szDesktopPath, p->path, pFileNotifyInfo->FileName);

                bool flag = true;
                while (!MoveFile(fname, szMovedName)) { 
                    DWORD Err = GetLastError();
                    WCHAR szErrorMsg[128] = L"";
                    
                    // Log Error
                    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, Err, 0, szErrorMsg, 128, nullptr);
                    wsprintf(szLogBuffer, szgLogs[4], CurTime(), fname, szErrorMsg);
                    WriteLog();
                    
                    if (Err == ERROR_SHARING_VIOLATION) Sleep(500);
                    else if (Err == ERROR_ALREADY_EXISTS)
                        wsprintf(szMovedName, L"%ws\\%ws\\%d - %ws", szDesktopPath, p->path, (int)time(nullptr), pFileNotifyInfo->FileName);
                    else { flag = false; break; }
                }
                if (!flag) continue;

                WCHAR szShortcutName[MAX_PATH];
                wsprintf(szShortcutName, L"%ws.lnk", fname);
                IShellLink* psl;
                if (!SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) continue;
                HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
                if (SUCCEEDED(hr))
                {
                    psl->SetPath(szMovedName);
                    IPersistFile* ppf;
                    hr = psl->QueryInterface(&ppf);
                    if (SUCCEEDED(hr)) {
                        hr = ppf->Save(szShortcutName, TRUE);
                        ppf->Release();
                    }
                    psl->Release();
                }
                CoUninitialize();
                WriteFile(hQueuefile, szShortcutName, MAX_PATH * sizeof(WCHAR), nullptr, nullptr);
                FlushFileBuffers(hQueuefile);

                //Log file move complete
                wsprintf(szLogBuffer, szgLogs[6], CurTime(), fname, szMovedName);
                WriteLog();

            } while (pFileNotifyInfo->NextEntryOffset);
        }
    }
    delete[] Buf, Buf2;
    return 0;
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

    // Init Error log
    wsprintf(szLogfilePath, L"%ws\\error.log", szHomePath);
    hLogfile = CreateFile(szLogfilePath, FILE_GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD Err = GetLastError();
    if (!Err) {
        wsprintf(szLogBuffer, szgLogs[10], CurTime());
        WriteLog();
    }
    SetFilePointer(hLogfile, 0, nullptr, FILE_END);
    wsprintf(szLogBuffer, szgLogs[0], CurTime());
    WriteLog();

    // Check if needed to create folder
    auto attr = GetFileAttributes(szHomePath);
    if (attr == -1 || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        CreateDirectory(szHomePath, NULL);

    // Init Config
    wsprintf(szConfigfilePath, L"%ws\\config.ini", szHomePath);
    hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    ReadConfig();
    CloseHandle(hConfigfile);
    hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    SetFilePointer(hConfigfile, 0, nullptr, FILE_END);

    // Log config init
    wsprintf(szLogBuffer, szgLogs[1], CurTime());
    WriteLog();
    

    // Init Queue
    wsprintf(szQueuefilePath, L"%ws\\queue", szHomePath);
    DeleteQueue();
    hQueuefile = CreateFile(szQueuefilePath, FILE_GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    // Init Desktop Path
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

    // Notification Icon
    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(TRAY_ICON));
    NotifyIconData.cbSize = sizeof(NOTIFYICONDATA);
    NotifyIconData.hWnd = hWnd;
    NotifyIconData.uID = 0;
    NotifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    NotifyIconData.uCallbackMessage = WM_TRAYICON;
    NotifyIconData.hIcon = hIcon;
    NotifyIconData.uVersion = 3;
    lstrcpy(NotifyIconData.szTip, L"Desktop Tidiness Helper");
    if (!Shell_NotifyIcon(NIM_ADD, &NotifyIconData)) return FALSE;
    if (!Shell_NotifyIcon(NIM_SETVERSION, &NotifyIconData)) return FALSE;

    return TRUE;
}

void ExitInstance() {
    CloseHandle(hConfigfile);
    CloseHandle(hQueuefile);
    Shell_NotifyIcon(NIM_DELETE, &NotifyIconData);
}

vector<FILEINFO> IndexerWorker(LPWSTR dir) {
    vector<FILEINFO> fInfo;
    WIN32_FIND_DATAW findData = {};
    WCHAR szFindCommand[MAX_PATH];
    wsprintf(szFindCommand, L"%ws\\*", dir);
    HANDLE hFind = FindFirstFileW(szFindCommand, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return fInfo;
    do {
        if (!lstrcmpW(findData.cFileName, L".") || !lstrcmpW(findData.cFileName, L"..")) continue;
        fInfo.push_back({ findData.cFileName, findData.nFileSizeLow });
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            WCHAR dfs[MAX_PATH];
            wsprintf(dfs, L"%ws\\%ws", dir, findData.cFileName);
            vector<FILEINFO> ret = IndexerWorker(dfs);
            fInfo.insert(fInfo.end(), ret.begin(), ret.end());
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    return fInfo;
}

DWORD WINAPI Indexer(LPVOID lpParameter) {
    DRIVE* d = (DRIVE*)lpParameter;
    vector<FILEINFO> fInfo = IndexerWorker(d->letter);
    sort(fInfo.begin(), fInfo.end());
    d->files.clear();
    d->files.insert(d->files.end(), fInfo.begin(), fInfo.end());
    return 0;
}

DWORD WINAPI ConfigEditHandler(LPVOID lpParameter) {
    bPaused = true;

    // Log config being changed
    wsprintf(szLogBuffer, szgLogs[8], CurTime());
    WriteLog();

    CloseHandle(hConfigfile);
    
    // Clear drive linking list
    DRIVE* pd = driveHead.pnext;
    while (pd) {
        driveHead.pnext = pd->pnext;
        delete pd;
        pd = driveHead.pnext;
    }

    // Exec
    SHELLEXECUTEINFO ExecInfo = {};
    ExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
    ExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    ExecInfo.hwnd = (HWND)lpParameter;
    ExecInfo.lpVerb = L"open";
    ExecInfo.lpFile = szConfigfilePath;
    ExecInfo.nShow = SW_NORMAL;
    ExecInfo.hInstApp = NULL;
    ExecInfo.hProcess = INVALID_HANDLE_VALUE;
    ShellExecuteEx(&ExecInfo);
    WaitForSingleObject(ExecInfo.hProcess, INFINITE);

    // Reload config
    hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    ReadConfig();
    CloseHandle(hConfigfile);
    hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    SetFilePointer(hConfigfile, 0, nullptr, FILE_END);

    // Log config reinit
    wsprintf(szLogBuffer, szgLogs[9], CurTime());
    WriteLog();
    bPaused = false;
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
        switch (wParam)
        {
        case DBT_DEVICEARRIVAL: {
            DWORD unitmask = lpdbv->dbcv_unitmask;
            for (char i = 0; i < 26; ++i, unitmask >>= 1) {
                if (!(unitmask & 0x1)) continue;
                volumeLetter[0] = i + L'A';
                WCHAR volumeName[MAX_PATH] = L"";
                DWORD serialNumber = 0;
                GetVolumeInformation(volumeLetter, volumeName, MAX_PATH, &serialNumber, nullptr, nullptr, nullptr, 0);

                // Log drive arrival
                wsprintf(szLogBuffer, szgLogs[2], CurTime(), volumeName, volumeLetter);
                WriteLog();

                if (bPaused) break;

                DRIVE* p = driveHead.pnext;
                while (p && p->uuid != serialNumber) p = p->pnext;
                if (!p) {
                    WCHAR szString1[MAX_PATH] = L"";
                    wsprintf(szString1, L"\n[%d]\n# VolumeName = %ws\nMovePath = \"\"\n", serialNumber, volumeName);
                    WriteFile(hConfigfile, szString1, lstrlen(szString1) * sizeof(WCHAR), nullptr, nullptr);
                    FlushFileBuffers(hConfigfile);
                    DRIVE* nDrive = new DRIVE;
                    nDrive->pnext = driveHead.pnext;
                    nDrive->uuid = serialNumber;
                    driveHead.pnext = nDrive;
                }
                else {
                    if (p->path[0] == L'\0') continue;
                    lstrcpyW(p->letter, volumeLetter);
                    CreateThread(nullptr, 0, Indexer, p, 0, nullptr);
                }
            }
            break;
        }
        case DBT_DEVICEREMOVECOMPLETE: {
            DWORD unitmask = lpdbv->dbcv_unitmask;
            for (char i = 0; i < 26; ++i, unitmask >>= 1) {
                if (!(unitmask & 0x1)) continue;
                volumeLetter[0] = i + L'A';

                // Log device removal
                wsprintf(szLogBuffer, szgLogs[3], CurTime(), volumeLetter);
                WriteLog();
            }
            break;
        }
        default: {
            break;
        }
        }
        break;
    }
                   // Tray Icon
    case WM_TRAYICON: {
        if (bPaused) break;
        static clock_t cl = 0;
        clock_t cc = clock();
        switch LOWORD(lParam) {
        case WM_CONTEXTMENU: {
            if (cc - cl <= 1000) { cl = cc; break; }
            cl = cc;
            CreateThread(nullptr, 0, ConfigEditHandler, hWnd, 0, nullptr);
            break;
        }
        case NIN_SELECT:
        case NIN_KEYSELECT: {
            if (cc - cl <= 1000) { cl = cc; break; }
            cl = cc;
            ShellExecute(hWnd, L"open", szLogfilePath, nullptr, nullptr, SW_NORMAL);
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
    MyRegisterClass(hInstance); if (!InitInstance (hInstance, nCmdShow)) return FALSE;
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int) msg.wParam;
}