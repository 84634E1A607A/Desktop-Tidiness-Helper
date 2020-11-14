#define _CRT_SECURE_NO_WARNINGS


#include "framework.h"
#include <stdio.h>
#include <Dbt.h>
#include <ShlObj.h>
#include <time.h>
#include <mutex>
#include <vector>
#include <algorithm>

using namespace std;

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[] = L"DTH";                       // The title bar text
WCHAR szWindowClass[] = L"My Window Class";     // the main window class name
WCHAR szHomePath[MAX_PATH], szConfigfilePath[MAX_PATH], szQueuefilePath[MAX_PATH], szDesktopPath[MAX_PATH];
FILE* fpConfigfile, * fpQueuefile;

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

const LPWSTR CurTime() {
    time_t t = time(nullptr);
    tm* ct = localtime(&t);
    static WCHAR szt[32] = L"";
    swprintf(szt, 32, L"%02d-%02d %02d:%02d:%02d", ct->tm_mon, ct->tm_mday, ct->tm_hour, ct->tm_min, ct->tm_sec);
    return szt;
}

inline void DeleteQueue() {
    fpQueuefile = _wfopen(szQueuefilePath, L"rb");
    if (!fpQueuefile) return;
    WCHAR fname[MAX_PATH] = L"";
    while (fread(fname, MAX_PATH, 1, fpQueuefile))
        DeleteFile(fname);
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
            WCHAR key[MAX_PATH] = L"", value[MAX_PATH] = L"";
            int p = 0;
            while (line[p] != L'=' && p < len) p++;
            if (p == len) continue;
            memcpy(key, line, p * sizeof(WCHAR)), trim(key);
            lstrcpyW(value, line + p + 1), trim(value);
            
            if (!lstrcmpW(key, L"MovePath")) {
                if (!driveHead.pnext || driveHead.pnext->path[0] != L'\0') continue;
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
    HANDLE hDirectory = CreateFile(szDesktopPath, FILE_LIST_DIRECTORY, 
        FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    DWORD* Buf = new DWORD[8192], *Buf2 = new DWORD[1024], dwRet;
    FILE_NOTIFY_INFORMATION* pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)Buf, *pFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)Buf2;
    while (true) {
        RtlZeroMemory(pFirstFileNotifyInfo, 8192);
        ReadDirectoryChangesW(hDirectory, pFirstFileNotifyInfo, 8192, FALSE,
            FILE_NOTIFY_CHANGE_LAST_ACCESS,
            &dwRet, NULL, NULL);
        if (pFirstFileNotifyInfo->Action == FILE_ACTION_MODIFIED) {
            do {
                RtlZeroMemory(pFileNotifyInfo, 1024);
                memcpy(pFileNotifyInfo, pFirstFileNotifyInfo, pFirstFileNotifyInfo->NextEntryOffset ? pFirstFileNotifyInfo->NextEntryOffset - 1 : 1024);
                pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)((BYTE*)pFirstFileNotifyInfo + pFirstFileNotifyInfo->NextEntryOffset);
                if (!lstrcmpW(pFileNotifyInfo->FileName + pFileNotifyInfo->FileNameLength - 4, L".lnk")) continue;
                WCHAR fname[MAX_PATH];
                swprintf(fname, MAX_PATH, L"%ws\\%ws", szDesktopPath, pFileNotifyInfo->FileName);
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
                swprintf(szMovedName, MAX_PATH, L"%ws\\%ws\\%ws", szDesktopPath, p->path, pFileNotifyInfo->FileName);

                if (!MoveFile(fname, szMovedName)) continue;

                WCHAR szShortcutName[MAX_PATH];
                swprintf(szShortcutName, MAX_PATH, L"%ws.lnk", fname);
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
                fwrite(szShortcutName, MAX_PATH, 1, fpQueuefile);
                fflush(fpQueuefile);
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

    // Check if needed to create folder
    auto attr = GetFileAttributes(szHomePath);
    if (attr == -1 || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        CreateDirectory(szHomePath, NULL);

    // Init Config
    wsprintf(szConfigfilePath, L"%ws\\config.ini", szHomePath);
    fpConfigfile = _wfopen(szConfigfilePath, L"r");
    ReadConfig();
    fpConfigfile = _wfreopen(szConfigfilePath, L"a", fpConfigfile);

    // Init Queue
    wsprintf(szQueuefilePath, L"%ws\\queue", szHomePath);
    DeleteQueue();
    fpQueuefile = _wfopen(szQueuefilePath, L"wb");

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

    return TRUE;
}

void ExitInstance() {
    fclose(fpConfigfile);
    fclose(fpQueuefile);
}

vector<FILEINFO> IndexerWorker(LPWSTR dir) {
    vector<FILEINFO> fInfo;
    WIN32_FIND_DATAW findData = {};
    WCHAR szFindCommand[MAX_PATH];
    swprintf(szFindCommand, MAX_PATH, L"%ws\\*", dir);
    HANDLE hFind = FindFirstFileW(szFindCommand, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return fInfo;
    do {
        if (!lstrcmpW(findData.cFileName, L".") || !lstrcmpW(findData.cFileName, L"..")) continue;
        fInfo.push_back({ findData.cFileName, findData.nFileSizeLow });
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            WCHAR dfs[MAX_PATH];
            swprintf(dfs, MAX_PATH, L"%ws\\%ws", dir, findData.cFileName);
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
                volumeLetter[0] = i + 'A';
                WCHAR volumeName[MAX_PATH] = L"";
                DWORD serialNumber = 0;
                GetVolumeInformation(volumeLetter, volumeName, MAX_PATH, &serialNumber, nullptr, nullptr, nullptr, 0);
                DRIVE* p = driveHead.pnext;
                while (p && p->uuid != serialNumber) p = p->pnext;
                if (!p) {
                    fwprintf(fpConfigfile, L"\n[%d]\n# VolumeName = %ws\nMovePath = \"\"\n", serialNumber, volumeName);
                    fflush(fpConfigfile);
                }
                else {
                    if (p->path[0] == L'\0') continue;
                    lstrcpyW(p->letter, volumeLetter);
                    CreateThread(nullptr, 0, Indexer, p, 0, nullptr);
                }
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