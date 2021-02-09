#define _CRT_SECURE_NO_WARNINGS

#include "framework.h"
#include "resource.h"
#include <stdio.h>
#include <Dbt.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <time.h>
#include <vector>
#include <algorithm>

#define WM_TRAYICON (WM_USER + 1)

using namespace std;

// Global Variables:
DWORD dwTmpNULL;
HINSTANCE hInst;                                // Current instance
TCHAR szTitle[] = TEXT("DTH");                       // The title bar text
TCHAR szWindowClass[] = TEXT("My Window Class");     // The main window class name
TCHAR szgLogs[][128] = {                        // {0: Start, 1: Cfg Load, 2: Device Arrival, 3: Device Removal, 4: Err fMoving, 5: Cfg Created, 6: Complete fMoving, 7: Err tMonitor, 8: Cfg Chg, 9: Cfg Reload, 10: Log Created}
    TEXT("\n[%ws] Program Start\n"),
    TEXT("[%ws] Config Loaded\n"),
    TEXT("[%ws] Device Inserted, VolumeName=\"%ws\", VolumeLetter=\"%ws\"\n"),
    TEXT("[%ws] Device Ejected, VolumeLetter=\"%ws\"\n"),
    TEXT("[%ws] Error encontered when moving file \"%ws\": %ws"),
    TEXT("[%ws] Config Created\n"),
    TEXT("[%ws] File move complete: \"%ws\" --> \"%ws\"\n"),
    TEXT("[%ws] Error encontered when opening monitor: %wsVardump: DesktopPath=\"%ws\""),
    TEXT("[%ws] Config is being changed, program paused\n"),
    TEXT("[%ws] Config reloaded, program continues\n"),
    TEXT("[%ws] Log created\n")
};
TCHAR szLogBuffer[256];
TCHAR szHomePath[MAX_PATH], szConfigfilePath[MAX_PATH], szQueuefilePath[MAX_PATH], szLogfilePath[MAX_PATH], szDesktopPath[MAX_PATH];
HANDLE hConfigfile, hQueuefile, hLogfile;
NOTIFYICONDATA NotifyIconData;
bool bPaused;

// Structs
struct FILEINFO {
    TCHAR name[MAX_PATH] = TEXT("");
    DWORD size = 0;
    FILEINFO(LPWSTR n, DWORD s) { lstrcpy(name, n); size = s; }
    FILEINFO() {}
    bool operator< (const FILEINFO r) const {
        return lstrcmp(this->name, r.name) < 0;
    }
    //bool operator> (const FILEINFO r) const {
    //    return lstrcmp(this->name, r.name) > 0;
    //}
    bool operator== (const FILEINFO r) const {
        return !lstrcmp(this->name, r.name) && this->size == r.size;
    }
    //bool operator!= (const FILEINFO r) const {
    //    return !(*this == r);
    //}
};
struct DRIVE {
    DWORD uuid = 0;
    TCHAR path[MAX_PATH] = TEXT("");
    TCHAR letter[3] = TEXT("");
    vector<FILEINFO> files;
    DRIVE* pnext = NULL;
} driveHead;
struct EXEMPT {
    TCHAR name[MAX_PATH] = TEXT("");
    EXEMPT* pnext = NULL;
} exemptHead;

inline void WriteLog() {
    WriteFile(hLogfile, szLogBuffer, lstrlen(szLogBuffer) * sizeof(TCHAR), &dwTmpNULL, NULL);
    FlushFileBuffers(hLogfile);
}

const LPWSTR CurTime() {
    time_t t = time(NULL);
    tm* ct = localtime(&t);
    static TCHAR szt[32] = TEXT("");
    swprintf(szt, 32, TEXT("%02d-%02d %02d:%02d:%02d"), ct->tm_mon + 1, ct->tm_mday, ct->tm_hour, ct->tm_min, ct->tm_sec);
    return szt;
}

inline void DeleteQueue() {
    hQueuefile = CreateFile(szQueuefilePath, FILE_GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, 0);
    if (hQueuefile == INVALID_HANDLE_VALUE) return;
    DWORD fSize = GetFileSize(hQueuefile, NULL);
    DWORD count = fSize / MAX_PATH;
    TCHAR fname[MAX_PATH] = TEXT("");
    for (DWORD i = 0; i < count; i++)
    {
        if (!ReadFile(hQueuefile, fname, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL)) break;
        DeleteFile(fname);
    }
    CloseHandle(hQueuefile);
}

// Trim; Delete Notes
inline void trim(LPWSTR str) {
    int ps = 0, pe = 0, len = lstrlen(str);
    while (str[ps] == TEXT(' ') || str[ps] == TEXT('\t') || str[ps] == TEXT('\"')) ps++;
    while (str[pe] != TEXT('#') && pe < len) pe++;
    while (pe && (str[pe - 1] == TEXT(' ') || str[pe - 1] == TEXT('\t') || str[pe - 1] == TEXT('\n') || str[pe - 1] == TEXT('\"'))) pe--;
    len = pe, str[len] = TEXT('\0');
    for (int i = 0; i <= len; i++) str[i] = str[ps + i];
}

inline void ReadConfig() {
    
    if (hConfigfile == INVALID_HANDLE_VALUE) {
        wsprintf(szLogBuffer, szgLogs[5], CurTime());
        WriteLog();
        hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        TCHAR szString1[] = TEXT("# Config\n\n");
        WriteFile(hConfigfile, szString1, sizeof(szString1) - sizeof(TCHAR), &dwTmpNULL, NULL);
    }
    else {
        TCHAR line[512] = TEXT("");
        DWORD fSize = GetFileSize(hConfigfile, NULL);
        LPWSTR pFileContent = new TCHAR[(long long)fSize / 2 + 1];
        RtlZeroMemory(pFileContent, fSize + sizeof(TCHAR));
        if (!ReadFile(hConfigfile, pFileContent, fSize, &dwTmpNULL, NULL))
            return;
        TCHAR* ps = pFileContent, * pe = pFileContent - 1;
        while (true) {
            
            if (*pe == TEXT('\0')) break;
            ps = pe + 1; pe = ps;
            while ((*pe != TEXT('\n')) && *pe != TEXT('\0')) pe++;
            RtlZeroMemory(line, 512 * sizeof(TCHAR));
            memcpy(line, ps, (pe - ps + 1) * sizeof(TCHAR));

            trim(line);
            int len = lstrlen(line);
            if (!len) continue;

            // Drive UUID row
            if (line[0] == TEXT('[')) {
                if (line[len - 1] == TEXT(']')) line[--len] = TEXT('\0');
                DRIVE* nDrive = new DRIVE;
                nDrive->pnext = driveHead.pnext;
                nDrive->uuid = _wtoi(line + 1);
                driveHead.pnext = nDrive;
                continue;
            }

            // Config row
            TCHAR key[MAX_PATH] = TEXT(""), value[MAX_PATH] = TEXT("");
            int p = 0;
            while (line[p] != TEXT('=') && p < len) p++;
            if (p == len) continue;
            memcpy(key, line, p * sizeof(TCHAR)), trim(key);
            lstrcpy(value, line + p + 1), trim(value);
            
            if (!lstrcmp(key, TEXT("MovePath"))) {
                if (!driveHead.pnext || driveHead.pnext->path[0] != TEXT('\0')) continue;
                if (value[0] == TEXT('\"')) { 
                    value[lstrlen(value) - 1] = TEXT('\0');
                    lstrcpy(driveHead.pnext->path, value + 1);
                }
                else {
                    lstrcpy(driveHead.pnext->path, value);
                }
            }

            if (!lstrcmp(key, TEXT("Exempt"))) {
                EXEMPT* nExempt = new EXEMPT;
                nExempt->pnext = exemptHead.pnext;
                if (value[0] == TEXT('\"')) {
                    value[lstrlen(value) - 1] = TEXT('\0');
                    lstrcpy(nExempt->name, value + 1);
                }
                else {
                    lstrcpy(nExempt->name, value);
                }
                exemptHead.pnext = nExempt;
            }

            if (!lstrcmp(key, TEXT("DesktopPath"))) {
                lstrcpy(szDesktopPath, value);
            }           
        }
    }
}

HANDLE hDirectory;

void MonitorCompletionRotine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED Overlapped) {
    
    PFILE_NOTIFY_INFORMATION pFirstFileNotifyInfo = (PFILE_NOTIFY_INFORMATION)Overlapped->hEvent;
    DWORD* Buf2 = new DWORD[1024]; 
    PFILE_NOTIFY_INFORMATION pFileNotifyInfo = (PFILE_NOTIFY_INFORMATION)Buf2;
    if (pFirstFileNotifyInfo->Action == FILE_ACTION_ADDED) {
        do {
            RtlZeroMemory(pFileNotifyInfo, 1024 * sizeof(DWORD));
            memcpy(pFileNotifyInfo, pFirstFileNotifyInfo, pFirstFileNotifyInfo->NextEntryOffset ? static_cast<size_t>(pFirstFileNotifyInfo->NextEntryOffset) - 1 : 1024);
            pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)((BYTE*)pFirstFileNotifyInfo + pFirstFileNotifyInfo->NextEntryOffset);
            if (!lstrcmp(pFileNotifyInfo->FileName + pFileNotifyInfo->FileNameLength - 4, TEXT(".lnk"))) continue;
            TCHAR fname[MAX_PATH];
            wsprintf(fname, TEXT("%ws\\%ws"), szDesktopPath, pFileNotifyInfo->FileName);
            WIN32_FIND_DATA fdata;
            FindClose(FindFirstFile(fname, &fdata));
            while (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fdata.nFileSizeLow == 0)
            {
                Sleep(50);
                FindClose(FindFirstFile(fname, &fdata));
            }
            //if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue; // or it will delete all files in the directory
            FILEINFO fInfo = { fdata.cFileName, fdata.nFileSizeLow };
            DRIVE* p = driveHead.pnext;
            while (p) {
                auto pos = find(p->files.begin(), p->files.end(), fInfo);
                if (pos != p->files.end()) break;
                p = p->pnext;
            }
            if (!p) continue;
            TCHAR szMovedName[MAX_PATH];
            wsprintf(szMovedName, TEXT("%ws\\%ws\\%ws"), szDesktopPath, p->path, pFileNotifyInfo->FileName);

            bool flag = true;
            while (!MoveFile(fname, szMovedName)) {
                DWORD Err = GetLastError();
                TCHAR szErrorMsg[128] = TEXT("");

                // Log Error
                FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Err, 0, szErrorMsg, 128, NULL);
                wsprintf(szLogBuffer, szgLogs[4], CurTime(), fname, szErrorMsg);
                WriteLog();

                if (Err == ERROR_SHARING_VIOLATION) Sleep(500);
                else if (Err == ERROR_ALREADY_EXISTS)
                    wsprintf(szMovedName, TEXT("%ws\\%ws\\%d - %ws"), szDesktopPath, p->path, (int)time(NULL), pFileNotifyInfo->FileName);
                else { flag = false; break; }
            }
            if (!flag) continue;

            TCHAR szShortcutName[MAX_PATH];
            wsprintf(szShortcutName, TEXT("%ws.lnk"), fname);
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
            WriteFile(hQueuefile, szShortcutName, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL);
            FlushFileBuffers(hQueuefile);

            //Log file move complete
            wsprintf(szLogBuffer, szgLogs[6], CurTime(), fname, szMovedName);
            WriteLog();

        } while (pFileNotifyInfo->NextEntryOffset);
    }

    delete[] pFirstFileNotifyInfo, Buf2;

    DWORD* Buf1 = new DWORD[8192];
    RtlZeroMemory(Buf1, 8192 * sizeof(DWORD));
    Overlapped->hEvent = (HANDLE)Buf1;
    BOOL Ret = ReadDirectoryChangesW(hDirectory, (PFILE_NOTIFY_INFORMATION)Buf1, 8192 * sizeof(DWORD), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME, NULL, Overlapped, MonitorCompletionRotine);
    if (!Ret) {
        // Log open directory error
        DWORD Err = GetLastError();
        TCHAR szErrorMsg[128] = TEXT("");
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Err, 0, szErrorMsg, 128, NULL);
        wsprintf(szLogBuffer, szgLogs[7], CurTime(), szErrorMsg, szDesktopPath);
        return;
    }
}

DWORD WINAPI Monitor(LPVOID lpParameter) {

    hDirectory = CreateFile(szDesktopPath, FILE_LIST_DIRECTORY, 
        FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    if (hDirectory == INVALID_HANDLE_VALUE)
    {
        // Log open directory error
        DWORD Err = GetLastError();
        TCHAR szErrorMsg[128] = TEXT("");
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Err, 0, szErrorMsg, 128, NULL);
        wsprintf(szLogBuffer, szgLogs[7], CurTime(), szErrorMsg, szDesktopPath);
        WriteLog();
        return -1;
    }
    
<<<<<<< Updated upstream
    DWORD* Buf = new DWORD[8192], *Buf2 = new DWORD[1024], dwRet;
    FILE_NOTIFY_INFORMATION* pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)Buf, *pFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)Buf2;
    while (true)
    {
        RtlZeroMemory(pFirstFileNotifyInfo, 8192);
        BOOL Ret = ReadDirectoryChangesW(hDirectory, pFirstFileNotifyInfo, 8192, FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
            &dwRet, NULL, NULL);
        if (!Ret) {
            // Log open directory error
            DWORD Err = GetLastError();
            TCHAR szErrorMsg[128] = TEXT("");
            FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Err, 0, szErrorMsg, 128, NULL);
            wsprintf(szLogBuffer, szgLogs[7], CurTime(), szErrorMsg, szDesktopPath);
            return -1;
        }
        if (pFirstFileNotifyInfo->Action == FILE_ACTION_ADDED) {
            do {
                RtlZeroMemory(pFileNotifyInfo, 1024);
                memcpy(pFileNotifyInfo, pFirstFileNotifyInfo, pFirstFileNotifyInfo->NextEntryOffset ? static_cast<size_t>(pFirstFileNotifyInfo->NextEntryOffset) - 1 : 1024);
                pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)((BYTE*)pFirstFileNotifyInfo + pFirstFileNotifyInfo->NextEntryOffset);
                if (!lstrcmp(pFileNotifyInfo->FileName + pFileNotifyInfo->FileNameLength - 4, TEXT(".lnk"))) continue;
                TCHAR fname[MAX_PATH];
                wsprintf(fname, TEXT("%ws\\%ws"), szDesktopPath, pFileNotifyInfo->FileName);
                Sleep(500);
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
                TCHAR szMovedName[MAX_PATH];
                wsprintf(szMovedName, TEXT("%ws\\%ws\\%ws"), szDesktopPath, p->path, pFileNotifyInfo->FileName);

                bool flag = true;
                while (!MoveFile(fname, szMovedName)) { 
                    DWORD Err = GetLastError();
                    TCHAR szErrorMsg[128] = TEXT("");
                    
                    // Log Error
                    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Err, 0, szErrorMsg, 128, NULL);
                    wsprintf(szLogBuffer, szgLogs[4], CurTime(), fname, szErrorMsg);
                    WriteLog();
                    
                    if (Err == ERROR_SHARING_VIOLATION) Sleep(500);
                    else if (Err == ERROR_ALREADY_EXISTS)
                        wsprintf(szMovedName, TEXT("%ws\\%ws\\%d - %ws"), szDesktopPath, p->path, (int)time(NULL), pFileNotifyInfo->FileName);
                    else { flag = false; break; }
                }
                if (!flag) continue;

                TCHAR szShortcutName[MAX_PATH];
                wsprintf(szShortcutName, TEXT("%ws.lnk"), fname);
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
                WriteFile(hQueuefile, szShortcutName, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL);
                FlushFileBuffers(hQueuefile);

                //Log file move complete
                wsprintf(szLogBuffer, szgLogs[6], CurTime(), fname, szMovedName);
                WriteLog();

            } while (pFileNotifyInfo->NextEntryOffset);
        }
=======
    DWORD* Buf = new DWORD[8192];
    RtlZeroMemory(Buf, 8192 * sizeof(DWORD));
    OVERLAPPED overlapped;
    overlapped.hEvent = (HANDLE)Buf;
    BOOL Ret = ReadDirectoryChangesW(hDirectory, (PFILE_NOTIFY_INFORMATION)Buf, 8192, FALSE, 
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME, NULL, &overlapped, MonitorCompletionRotine);
    if (!Ret) {
        // Log open directory error
        DWORD Err = GetLastError();
        TCHAR szErrorMsg[128] = TEXT("");
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Err, 0, szErrorMsg, 128, NULL);
        wsprintf(szLogBuffer, szgLogs[7], CurTime(), szErrorMsg, szDesktopPath);
        return -1;
>>>>>>> Stashed changes
    }

    SleepEx(INFINITE, true);
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
    lstrcpy(szHomePath, _wgetenv(TEXT("appdata")));
    lstrcat(szHomePath, TEXT("\\..\\Local\\Desktop Tidiness Helper"));

    // Check if needed to create folder
    auto attr = GetFileAttributes(szHomePath);
    if (attr == -1 || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        CreateDirectory(szHomePath, NULL);

    // Init Error log
    wsprintf(szLogfilePath, TEXT("%ws\\error.log"), szHomePath);
    hLogfile = CreateFile(szLogfilePath, FILE_GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    DWORD Err = GetLastError();
    if (!Err) {
        wsprintf(szLogBuffer, szgLogs[10], CurTime());
        WriteLog();
    }
    LONG tmp = 0;
    SetFilePointer(hLogfile, 0, &tmp, FILE_END);
    wsprintf(szLogBuffer, szgLogs[0], CurTime());
    WriteLog();


    // Init Config
    wsprintf(szConfigfilePath, TEXT("%ws\\config.ini"), szHomePath);
    hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    ReadConfig();
    CloseHandle(hConfigfile);
    hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    SetFilePointer(hConfigfile, 0, NULL, FILE_END);
    wsprintf(szLogBuffer, szgLogs[1], CurTime());
    WriteLog();
    

    // Init Queue
    wsprintf(szQueuefilePath, TEXT("%ws\\queue"), szHomePath);
    DeleteQueue();
    hQueuefile = CreateFile(szQueuefilePath, FILE_GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    // Init Desktop Path
    if (szDesktopPath[0] == TEXT('\0')) {
        LPITEMIDLIST pidl;
        LPMALLOC pShellMalloc;
        if (SUCCEEDED(SHGetMalloc(&pShellMalloc))) {
            if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pidl))) {
                SHGetPathFromIDList(pidl, szDesktopPath);
                pShellMalloc->Free(pidl);
            }
            pShellMalloc->Release();
        }
        TCHAR tmp[MAX_PATH] = TEXT("");
        wsprintf(tmp, TEXT("DesktopPath = \"%ws\"\n"), szDesktopPath);
        WriteFile(hConfigfile, tmp, lstrlen(tmp) * sizeof(TCHAR), &dwTmpNULL, NULL);
    }

    // Init Monitor Thread
    HANDLE hMonitor = CreateThread(NULL, 0, Monitor, NULL, 0, NULL);

    // Notification Icon
    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(TRAY_ICON));
    NotifyIconData.cbSize = sizeof(NOTIFYICONDATA);
    NotifyIconData.hWnd = hWnd;
    NotifyIconData.uID = 0;
    NotifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    NotifyIconData.uCallbackMessage = WM_TRAYICON;
    NotifyIconData.hIcon = hIcon;
    NotifyIconData.uVersion = 3;
    lstrcpy(NotifyIconData.szTip, TEXT("Desktop Tidiness Helper"));
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
    TCHAR szFindCommand[MAX_PATH];
    wsprintf(szFindCommand, TEXT("%ws\\*"), dir);
    HANDLE hFind = FindFirstFileW(szFindCommand, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return fInfo;
    do {
        if (!lstrcmp(findData.cFileName, TEXT(".")) || !lstrcmp(findData.cFileName, TEXT(".."))) continue;
        fInfo.push_back({ findData.cFileName, findData.nFileSizeLow });
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            TCHAR dfs[MAX_PATH];
            wsprintf(dfs, TEXT("%ws\\%ws"), dir, findData.cFileName);
            vector<FILEINFO> ret = IndexerWorker(dfs);
            fInfo.insert(fInfo.end(), ret.begin(), ret.end());
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    return fInfo;
}

DWORD WINAPI Indexer(LPVOID lpParameter) {
//#ifdef _DEBUG
//	MessageBox(NULL, TEXT("Indexer started"), TEXT("Message"), MB_OK);
//#endif // DEBUG
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
    ExecInfo.lpVerb = TEXT("open");
    ExecInfo.lpFile = szConfigfilePath;
    ExecInfo.nShow = SW_NORMAL;
    ExecInfo.hInstApp = NULL;
    ExecInfo.hProcess = INVALID_HANDLE_VALUE;
    ShellExecuteEx(&ExecInfo);
    WaitForSingleObject(ExecInfo.hProcess, INFINITE);

    // Reload config
    hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    ReadConfig();
    CloseHandle(hConfigfile);
    hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    SetFilePointer(hConfigfile, 0, NULL, FILE_END);

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
        TCHAR volumeLetter[] = TEXT("C:");
        switch (wParam)
        {
        case DBT_DEVICEARRIVAL: {
            DWORD unitmask = lpdbv->dbcv_unitmask;
            for (char i = 0; i < 26; ++i, unitmask >>= 1) {
                if (!(unitmask & 0x1)) continue;
                volumeLetter[0] = i + TEXT('A');
                TCHAR volumeName[MAX_PATH] = TEXT("");
                DWORD serialNumber = 0;
                GetVolumeInformation(volumeLetter, volumeName, MAX_PATH, &serialNumber, NULL, NULL, NULL, 0);

                // Log drive arrival
                wsprintf(szLogBuffer, szgLogs[2], CurTime(), volumeName, volumeLetter);
                WriteLog();

                if (bPaused) break;

                DRIVE* p = driveHead.pnext;
                while (p && p->uuid != serialNumber) p = p->pnext;
                if (!p) {
                    TCHAR szString1[MAX_PATH] = TEXT("");
                    wsprintf(szString1, TEXT("\n[%d]\n# VolumeName = %ws\nMovePath = \"\"\n"), serialNumber, volumeName);
                    WriteFile(hConfigfile, szString1, lstrlen(szString1) * sizeof(TCHAR), &dwTmpNULL, NULL);
                    FlushFileBuffers(hConfigfile);
                    DRIVE* nDrive = new DRIVE;
                    nDrive->pnext = driveHead.pnext;
                    nDrive->uuid = serialNumber;
                    //lstrcpy(nDrive->letter, volumeLetter);
                    driveHead.pnext = nDrive;
                }
                else {
                    if (p->path[0] == TEXT('\0')) continue;
                    lstrcpy(p->letter, volumeLetter);
                    CreateThread(NULL, 0, Indexer, p, 0, NULL);
                }
            }
            break;
        }
        case DBT_DEVICEREMOVECOMPLETE: {
            DWORD unitmask = lpdbv->dbcv_unitmask;
            for (char i = 0; i < 26; ++i, unitmask >>= 1) {
                if (!(unitmask & 0x1)) continue;
                volumeLetter[0] = i + TEXT('A');

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
            CreateThread(NULL, 0, ConfigEditHandler, hWnd, 0, NULL);
            break;
        }
        case NIN_SELECT:
        case NIN_KEYSELECT: {
            if (cc - cl <= 1000) { cl = cc; break; }
            cl = cc;
            ShellExecute(hWnd, TEXT("open"), szLogfilePath, NULL, NULL, SW_NORMAL);
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
    wcex.hIcon          = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = NULL;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = NULL;

    return RegisterClassExW(&wcex);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    MyRegisterClass(hInstance); if (!InitInstance (hInstance, nCmdShow)) return FALSE;
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int) msg.wParam;
}