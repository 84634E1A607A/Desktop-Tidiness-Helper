#define _CRT_SECURE_NO_WARNINGS

#include "framework.h"
#include "resource.h"
#include "Desktop Tidiness Helper.h"

#define UM_TRAYICON (WM_USER + 1)

using namespace std;

// Global Variables:
DWORD dwTmpNULL;
HINSTANCE hInst;                                // Current instance
TCHAR szTitle[] = TEXT("DTH");                       // The title bar text
TCHAR szWindowClass[] = TEXT("My Window Class");     // The main window class name
TCHAR szgLogs[][128] = {                        // {0: Start, 1: Cfg Load, 2: Device Arrival, 3: Device Removal, 4: Err fMoving, 5: Cfg Created, 6: Complete fMoving, 7: Err tMonitor, 8: Cfg Chg, 9: Cfg Reload, 10: Log Created, 11: Move Pending}
	TEXT("\n[%ws] Program Start\n"),
	TEXT("[%ws] Config Loaded\n"),
	TEXT("[%ws] Device Inserted, VolumeName=\"%ws\", VolumeLetter=\"%ws\", VolumeSerial=%d\n"),
	TEXT("[%ws] Device Ejected, VolumeLetter=\"%ws\"\n"),
	TEXT("[%ws] Error encontered when moving file \"%ws\": %ws"),
	TEXT("[%ws] Config Created\n"),
	TEXT("[%ws] File move complete: \"%ws\" --> \"%ws\"\n"),
	TEXT("[%ws] Error encontered when opening monitor: %wsVardump: DesktopPath=\"%ws\""),
	TEXT("[%ws] Config is being changed, program paused\n"),
	TEXT("[%ws] Config reloaded, program continues\n"),
	TEXT("[%ws] Log created\n"),
	TEXT("[%ws] Move pending: \"%ws\" --> \"%ws\"\n")
};
TCHAR szLogBuffer[256];
TCHAR szHomePath[MAX_PATH], szConfigfilePath[MAX_PATH], szQueuefilePath[MAX_PATH], szLogfilePath[MAX_PATH], szDesktopPath[MAX_PATH];
HANDLE hConfigfile, hQueuefile, hLogfile;
NOTIFYICONDATA NotifyIconData;
bool bPaused, bCopyUDisk;
HMENU hMenu;


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

inline void MoveQueue() {
	hQueuefile = CreateFile(szQueuefilePath, FILE_GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, 0);
	if (hQueuefile == INVALID_HANDLE_VALUE) return;
	DWORD fSize = GetFileSize(hQueuefile, NULL);
	DWORD count = fSize / MAX_PATH / sizeof(TCHAR) / 2;
	TCHAR szFileName[MAX_PATH] = TEXT("");
	TCHAR szOriginalMovedName[MAX_PATH] = TEXT("");
	TCHAR szMovedName[MAX_PATH] = TEXT("");
	for (DWORD i = 0; i < count; i++)
	{
		if (!ReadFile(hQueuefile, szFileName, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL)) break;
		if (!ReadFile(hQueuefile, szOriginalMovedName, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL)) break;
		if (!ReadFile(hQueuefile, szMovedName, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL)) break;
		if (!MoveFile(szFileName, szOriginalMovedName)) {
			if (!MoveFile(szFileName, szMovedName)) {
				DWORD Err = GetLastError();
				TCHAR szErrorMsg[128] = TEXT("");
				// Log Error
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Err, 0, szErrorMsg, 128, NULL);
				wsprintf(szLogBuffer, szgLogs[4], CurTime(), szFileName, szErrorMsg);
				WriteLog();
			}
			else {
				//Log file move complete
				wsprintf(szLogBuffer, szgLogs[6], CurTime(), szFileName, szMovedName);
				WriteLog();
			}
		}
		else {
			//Log file move complete
			wsprintf(szLogBuffer, szgLogs[6], CurTime(), szFileName, szOriginalMovedName);
			WriteLog();
		}
	}
	CloseHandle(hQueuefile);
}

// Trim; Delete Notes
inline void trim(LPTSTR str) {
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
		UCHAR BOM[] = { 0xFF,0xFE };
		TCHAR szString1[] = TEXT("# Config\n\n");
		TCHAR szString2[] = TEXT("CopyUDisk=false\n\n");
		WriteFile(hConfigfile, szString1, sizeof(szString1) - sizeof(TCHAR), &dwTmpNULL, NULL);
		WriteFile(hConfigfile, szString2, sizeof(szString2) - sizeof(TCHAR), &dwTmpNULL, NULL);
	}
	else {
		TCHAR line[512] = TEXT("");
		DWORD fSize = GetFileSize(hConfigfile, NULL);
		LPTSTR pFileContent = new TCHAR[(long long)fSize / 2 + 1];
		ReadFile(hConfigfile, pFileContent, 2, &dwTmpNULL, NULL);
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
				nDrive->uuid = _ttoi(line + 1);
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

			if (!lstrcmp(key, TEXT("CopyUDisk"))) {
				if (!lstrcmp(value, TEXT("true"))) {
					bCopyUDisk = true;
				}
			}
		}
	}
}

DWORD WINAPI Monitor(LPVOID lpParameter) {

	HANDLE hDirectory = CreateFile(szDesktopPath, FILE_LIST_DIRECTORY, 
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
			Sleep(50);
			do {
				RtlZeroMemory(pFileNotifyInfo, 1024);
				memcpy(pFileNotifyInfo, pFirstFileNotifyInfo, pFirstFileNotifyInfo->NextEntryOffset ? static_cast<size_t>(pFirstFileNotifyInfo->NextEntryOffset) - 1 : 1024);
				pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)((BYTE*)pFirstFileNotifyInfo + pFirstFileNotifyInfo->NextEntryOffset);
				if (!lstrcmp(pFileNotifyInfo->FileName + pFileNotifyInfo->FileNameLength - 4, TEXT(".lnk"))) continue;
				TCHAR fname[MAX_PATH];
				wsprintf(fname, TEXT("%ws\\%ws"), szDesktopPath, pFileNotifyInfo->FileName);
				WIN32_FIND_DATAW fdata = {};
				while (!fdata.nFileSizeLow && !(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) 
				{ FindClose(FindFirstFile(fname, &fdata)); Sleep(20); }
				FILEINFO fInfo = { fdata.cFileName, fdata.nFileSizeLow };
				DRIVE* p = driveHead.pnext;
				while (p) {
					auto pos = find(p->files.begin(), p->files.end(), fInfo);
					if (pos != p->files.end()) break;
					p = p->pnext;
				}
				if (!p) continue;
				TCHAR szMovedName[MAX_PATH], szOriginalMovedName[MAX_PATH], szOriginalName[MAX_PATH];
				wsprintf(szMovedName, TEXT("%ws\\%ws\\%d - %ws"), szDesktopPath, p->path, (int)time(NULL) , pFileNotifyInfo->FileName);
				wsprintf(szOriginalMovedName, TEXT("%ws\\%ws\\%ws"), szDesktopPath, p->path, pFileNotifyInfo->FileName);
				wsprintf(szOriginalName, TEXT("%ws\\%ws"), szDesktopPath, pFileNotifyInfo->FileName);
				wsprintf(szLogBuffer, szgLogs[11], CurTime(), szOriginalName, szMovedName);
				WriteLog();
				WriteFile(hQueuefile, szOriginalName, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL);
				WriteFile(hQueuefile, szOriginalMovedName, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL);
				WriteFile(hQueuefile, szMovedName, MAX_PATH * sizeof(TCHAR), &dwTmpNULL, NULL);
				FlushFileBuffers(hQueuefile);
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
	MoveQueue();
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
	NotifyIconData.uCallbackMessage = UM_TRAYICON;
	NotifyIconData.hIcon = hIcon;
	NotifyIconData.uVersion = 3;
	lstrcpy(NotifyIconData.szTip, TEXT("Desktop Tidiness Helper"));
	if (!Shell_NotifyIcon(NIM_ADD, &NotifyIconData)) return FALSE;
	if (!Shell_NotifyIcon(NIM_SETVERSION, &NotifyIconData)) return FALSE;

	// Init Menu
	hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MENU1));

	// Initial Scan
	for (int i = 3; i < 26; i++) {
		DeviceArrivalHandler(i);
	}

	return TRUE;
}

void ExitInstance() {
	CloseHandle(hConfigfile);
	CloseHandle(hQueuefile);
	Shell_NotifyIcon(NIM_DELETE, &NotifyIconData);
}

vector<FILEINFO> IndexerWorker(LPWSTR dir, bool docopy, DRIVE* pDrive) {
	vector<FILEINFO> fInfo;
	WIN32_FIND_DATAW findData = {};
	TCHAR szFindCommand[MAX_PATH];
	wsprintf(szFindCommand, TEXT("%ws\\*"), dir);
	HANDLE hFind = FindFirstFileW(szFindCommand, &findData);
	if (hFind == INVALID_HANDLE_VALUE) return fInfo;
	do {
		if (!lstrcmp(findData.cFileName, TEXT(".")) || !lstrcmp(findData.cFileName, TEXT(".."))) continue;
		if (docopy)
		{
			TCHAR copyname[MAX_PATH], tmp[MAX_PATH], srcname[MAX_PATH];
			wsprintf(tmp, TEXT("%ws\\%ws"), dir, findData.cFileName);
			int i1, i2;
			wsprintf(copyname, TEXT("%ws\\%ws\\%d\\"), szDesktopPath, pDrive->path, pDrive->uuid);
			i1 = static_cast<int>(wcslen(copyname));
			for (i2 = 0; tmp[i2] != TEXT('\0') && tmp[i2] != TEXT('\\'); i2++);
			if (tmp[i2] == TEXT('\\'))
			{
				i2++;
			}
			while (true)
			{
				if (i1 == MAX_PATH)
				{
					copyname[i1 - 1] = TEXT('\0');
					break;
				}
				copyname[i1] = tmp[i2];
				if (tmp[i2] == TEXT('\0')) break;
				i1++;
				i2++;
			}
			if (i1 < MAX_PATH)
			{
				copyname[i1] = TEXT('\0');
			}
			wsprintf(srcname, TEXT("%ws\\%ws\0"), dir, findData.cFileName);
			CopyFile(srcname, copyname, TRUE);// I don't know why SHFileOperation will regard all files as directories without this line
			SHFILEOPSTRUCT fo;
			fo.hwnd = NULL;
			fo.fAnyOperationsAborted = FALSE;
			//fo.fFlags = FOF_NO_UI;
			fo.hNameMappings = NULL;
			fo.wFunc = FO_COPY;
			fo.pFrom = srcname;
			fo.pTo = copyname;
			SHFileOperation(&fo);
		}
		else
			fInfo.push_back({ findData.cFileName, findData.nFileSizeLow });
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			TCHAR dfs[MAX_PATH];
			wsprintf(dfs, TEXT("%ws\\%ws"), dir, findData.cFileName);
			vector<FILEINFO> ret = IndexerWorker(dfs, docopy, pDrive);
			if(!docopy)
				fInfo.insert(fInfo.end(), ret.begin(), ret.end());
		}
	} while (FindNextFileW(hFind, &findData));
	FindClose(hFind);
	return fInfo;
}

DWORD WINAPI Indexer(LPVOID lpParameter) {
	DRIVE* d = (DRIVE*)lpParameter;
	vector<FILEINFO> fInfo = IndexerWorker(d->letter, false, d);
	sort(fInfo.begin(), fInfo.end());
	d->files = std::move(fInfo);
	/*d->files.clear();
	d->files.insert(d->files.end(), fInfo.begin(), fInfo.end());*/
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

DWORD WINAPI CopyUDisk(LPVOID pDrive)
{
	DRIVE* _pDrive = (DRIVE*)pDrive;
	IndexerWorker(_pDrive->letter, true, _pDrive);
	return 0;
}

void DeviceArrivalHandler(int volumeIndex) {
	TCHAR volumeLetter[] = TEXT("C:");
	volumeLetter[0] = volumeIndex + TEXT('A');
	TCHAR volumeName[MAX_PATH] = TEXT("");
	DWORD serialNumber = 0, fileSystemFlags = 0;

	if (GetDriveType(volumeLetter) != DRIVE_REMOVABLE) return;
	GetVolumeInformation(volumeLetter, volumeName, MAX_PATH, &serialNumber, NULL, &fileSystemFlags, NULL, 0);

	// Log drive arrival
	wsprintf(szLogBuffer, szgLogs[2], CurTime(), volumeName, volumeLetter, serialNumber);
	WriteLog();

	if (bPaused) return;
	if (serialNumber == 0) return;

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
		if (p->path[0] == TEXT('\0')) return;
		lstrcpy(p->letter, volumeLetter);
		CreateThread(NULL, 0, Indexer, p, 0, NULL);
	}

	if (bCopyUDisk) {
		CreateThread(NULL, 0, CopyUDisk, p, 0, NULL);
	}
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
				DeviceArrivalHandler(i);
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
	case UM_TRAYICON: {
		if (bPaused) break;
		static clock_t cl = 0;
		clock_t cc = clock();
		switch LOWORD(lParam) {
		case NIN_SELECT:
		case NIN_KEYSELECT:
		case WM_CONTEXTMENU: {
			POINT pt;
			GetCursorPos(&pt);
			SetForegroundWindow(hWnd);
			HMENU hPopupMenu = GetSubMenu(hMenu, 0);
			TrackPopupMenu(hPopupMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
			break;
		}
		}
		break;
	}
	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case ID_X_EXIT:
		{
			SendMessage(hWnd, WM_DESTROY, 0, 0);
			break;
		}
		case ID_X_VIEWLOG:
		{
			ShellExecute(hWnd, TEXT("open"), szLogfilePath, NULL, NULL, SW_NORMAL);
			break;
		}
		case ID_X_EDITCONFIG:
		{
			CreateThread(NULL, 0, ConfigEditHandler, hWnd, 0, NULL);
			break;
		}
		default:
			break;
		}
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