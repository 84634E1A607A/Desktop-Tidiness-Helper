#define _CRT_SECURE_NO_WARNINGS

#include "framework.h"
#include "resource.h"
#include "Desktop Tidiness Helper.h"

#define UM_TRAYICON (WM_USER + 1)

using namespace std;

// Global Variables:
DWORD dwTmpNULL;
HINSTANCE hInst;                                // Current instance
HWND hThisWnd;
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
TCHAR szHomePath[MAX_PATH], szConfigfilePath[MAX_PATH], szQueuefilePath[MAX_PATH], szLogfilePath[MAX_PATH], szDesktopPath[MAX_PATH], szDefaultMovePath[MAX_PATH];
HANDLE hConfigfile, hQueuefile, hLogfile;
NOTIFYICONDATA NotifyIconData;
bool bPaused, bCopyUDisk, bDeleteIndexOnEject;
HMENU hMenu;



inline void WriteLog() 
{
	WriteFile(hLogfile, szLogBuffer, lstrlen(szLogBuffer) * sizeof(TCHAR), &dwTmpNULL, NULL);
	FlushFileBuffers(hLogfile);
}

const LPWSTR CurTime() 
{
	time_t tTime = time(NULL);
	tm* tmTime = localtime(&tTime);
	static TCHAR szTime[32] = TEXT("");
	swprintf(szTime, 32, TEXT("%02d-%02d %02d:%02d:%02d"), tmTime->tm_mon + 1, tmTime->tm_mday, tmTime->tm_hour, tmTime->tm_min, tmTime->tm_sec);
	return szTime;
}

inline void MoveQueue() 
{
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
		if (!MoveFile(szFileName, szOriginalMovedName)) 
		{
			if (!MoveFile(szFileName, szMovedName)) 
			{
				DWORD Err = GetLastError();
				TCHAR szErrorMsg[128] = TEXT("");
				// Log Error
				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, Err, 0, szErrorMsg, 128, NULL);
				wsprintf(szLogBuffer, szgLogs[4], CurTime(), szFileName, szErrorMsg);
				WriteLog();
			}
			else 
			{
				//Log file move complete
				wsprintf(szLogBuffer, szgLogs[6], CurTime(), szFileName, szMovedName);
				WriteLog();
			}
		}
		else 
		{
			//Log file move complete
			wsprintf(szLogBuffer, szgLogs[6], CurTime(), szFileName, szOriginalMovedName);
			WriteLog();
		}
	}
	CloseHandle(hQueuefile);
}

// Trim; Delete Notes
inline void Trim(LPTSTR szString) 
{
	int nStartPos = 0, nEndPos = 0, nLength = lstrlen(szString);
	while (szString[nStartPos] == TEXT(' ') || szString[nStartPos] == TEXT('\t') || szString[nStartPos] == TEXT('\"')) nStartPos++;
	while (szString[nEndPos] != TEXT('#') && nEndPos < nLength) nEndPos++;
	while (nEndPos && (szString[nEndPos - 1] == TEXT(' ') || szString[nEndPos - 1] == TEXT('\t') || szString[nEndPos - 1] == TEXT('\n') || szString[nEndPos - 1] == TEXT('\"'))) nEndPos--;
	nLength = nEndPos, szString[nLength] = TEXT('\0');
	for (int i = 0; i <= nLength; i++) szString[i] = szString[nStartPos + i];
}

bool CheckSingleInstance()
{
	TCHAR szExeName[MAX_PATH];
	GetModuleFileName(hInst, szExeName, MAX_PATH);

	FILEINFO fInfo(szExeName, 0);

	int nCount = 0;

	PROCESSENTRY32 Entry;
	Entry.dwSize = sizeof(PROCESSENTRY32);

	HANDLE SnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

	if (Process32First(SnapShot, &Entry))
	{
		while (Process32Next(SnapShot, &Entry))
		{
			if (!lstrcmp(fInfo.Name(), Entry.szExeFile))
			{
				nCount++;
			}
		}
	}

	return nCount <= 1;
}

void LoadExempt(LPTSTR szExempt)
{
	wstring sExempt(szExempt);
	size_t szFind = 0, szLast = 0;
	EXEMPT* pExempt = &exemptHead;
	if (sExempt.size() > 0)
	{
		while (true)
		{
			szFind = sExempt.find(TEXT('|'), szFind);
			if (szFind == wstring::npos) break;
			if (szFind > szLast + 1)
			{
				pExempt->pNext = new EXEMPT;
				pExempt = pExempt->pNext;
				pExempt->pNext = NULL;
				lstrcpy(pExempt->szName, sExempt.substr(szLast + 1, szFind - 1).c_str());
			}
			szLast = szFind;
			szFind++;
		}
		if (szLast + 1 < sExempt.size())
		{
			pExempt->pNext = new EXEMPT;
			pExempt = pExempt->pNext;
			pExempt->pNext = NULL;
			lstrcpy(pExempt->szName, sExempt.substr(szLast + 1, sExempt.size() - 1).c_str());
		}
	}
}

inline void ReadConfig() 
{

	if (hConfigfile == INVALID_HANDLE_VALUE) 
	{
		wsprintf(szLogBuffer, szgLogs[5], CurTime());
		WriteLog();
		hConfigfile = CreateFile(szConfigfilePath, FILE_GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
		UCHAR BOM[] = { 0xFF,0xFE };
		TCHAR szString1[] = TEXT("# Config\n\n");
		TCHAR szString2[] = TEXT("DefaultMovePath = \"\"\n");
		TCHAR szString3[] = TEXT("CopyUDisk = false\n");
		TCHAR szString4[] = TEXT("DeleteIndexOnEject = false\n");
		TCHAR szString5[] = TEXT("Exempt = \".lnk\"\n");
		WriteFile(hConfigfile, szString1, sizeof(szString1) - sizeof(TCHAR), &dwTmpNULL, NULL);
		WriteFile(hConfigfile, szString2, sizeof(szString2) - sizeof(TCHAR), &dwTmpNULL, NULL);
		WriteFile(hConfigfile, szString3, sizeof(szString3) - sizeof(TCHAR), &dwTmpNULL, NULL);
		WriteFile(hConfigfile, szString4, sizeof(szString4) - sizeof(TCHAR), &dwTmpNULL, NULL);
		WriteFile(hConfigfile, szString5, sizeof(szString5) - sizeof(TCHAR), &dwTmpNULL, NULL);
	}
	else 
	{
		bCopyUDisk = false;
		bDeleteIndexOnEject = false;
		TCHAR szLine[512] = TEXT("");
		DWORD dwFileSize = GetFileSize(hConfigfile, NULL);
		LPTSTR pFileContent = new TCHAR[(long long)dwFileSize / 2 + 1];
		if (!ReadFile(hConfigfile, pFileContent, 2, &dwTmpNULL, NULL))
			return;
		RtlZeroMemory(pFileContent, dwFileSize + sizeof(TCHAR));
		if (!ReadFile(hConfigfile, pFileContent, dwFileSize, &dwTmpNULL, NULL))
			return;
		TCHAR* pStart = pFileContent, * pEnd = pFileContent - 1;
		while (true) 
		{
			
			if (*pEnd == TEXT('\0')) break;
			pStart = pEnd + 1; pEnd = pStart;
			while ((*pEnd != TEXT('\n')) && *pEnd != TEXT('\0')) pEnd++;
			RtlZeroMemory(szLine, 512 * sizeof(TCHAR));
			memcpy(szLine, pStart, (pEnd - pStart + 1) * sizeof(TCHAR));

			Trim(szLine);
			int nLen = lstrlen(szLine);
			if (!nLen) continue;

			// Drive UUID row
			if (szLine[0] == TEXT('[')) 
			{
				if (szLine[nLen - 1] == TEXT(']')) szLine[--nLen] = TEXT('\0');
				DRIVE* nDrive = new DRIVE;
				nDrive->pNext = driveHead.pNext;
				nDrive->dwUUID = _ttoi(szLine + 1);
				driveHead.pNext = nDrive;
				continue;
			}

			// Config row
			TCHAR szKey[MAX_PATH] = TEXT(""), szValue[MAX_PATH] = TEXT("");
			int nPos = 0;
			while (szLine[nPos] != TEXT('=') && nPos < nLen) nPos++;
			if (nPos == nLen) continue;
			memcpy(szKey, szLine, nPos * sizeof(TCHAR)), Trim(szKey);
			lstrcpy(szValue, szLine + nPos + 1), Trim(szValue);
			
			if (!lstrcmp(szKey, TEXT("MovePath"))) 
			{
				if (!driveHead.pNext || driveHead.pNext->szPath[0] != TEXT('\0')) continue;
				if (szValue[0] == TEXT('\"')) 
				{ 
					szValue[lstrlen(szValue) - 1] = TEXT('\0');
					lstrcpy(driveHead.pNext->szPath, szValue + 1);
				}
				else
				{
					lstrcpy(driveHead.pNext->szPath, szValue);
				}
			}

			if (!lstrcmp(szKey, TEXT("Exempt"))) 
			{
				LoadExempt(szValue);
			}

			if (!lstrcmp(szKey, TEXT("DesktopPath"))) 
			{
				lstrcpy(szDesktopPath, szValue);
			} 

			if (!lstrcmp(szKey, TEXT("DefaultMovePath"))) 
			{
				lstrcpy(szDefaultMovePath, szValue);
			}

			if (!lstrcmp(szKey, TEXT("CopyUDisk"))) 
			{
				if (!lstrcmp(szValue, TEXT("true"))) 
				{
					bCopyUDisk = true;
				}
			}

			if (!lstrcmp(szKey, TEXT("DeleteIndexOnEject")))
			{
				if (!lstrcmp(szValue, TEXT("true")))
				{
					bDeleteIndexOnEject = true;
				}
			}
		}
	}
}

DWORD WINAPI Monitor(LPVOID lpParameter) 
{

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
	
	DWORD* dwBuffer = new DWORD[8192], *dwBuffer2 = new DWORD[1024], dwRet;
	RtlZeroMemory(dwBuffer2, 1024 * sizeof(DWORD));
	FILE_NOTIFY_INFORMATION* pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)dwBuffer, *pFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)dwBuffer2;
	while (true)
	{
		RtlZeroMemory(pFirstFileNotifyInfo, 8192 * sizeof(DWORD));
		BOOL bRet = ReadDirectoryChangesW(hDirectory, pFirstFileNotifyInfo, 8192, FALSE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
			&dwRet, NULL, NULL);
		if (!bRet) 
		{
			// Log open directory error
			DWORD dwErr = GetLastError();
			TCHAR szErrorMsg[128] = TEXT("");
			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dwErr, 0, szErrorMsg, 128, NULL);
			wsprintf(szLogBuffer, szgLogs[7], CurTime(), szErrorMsg, szDesktopPath);
			return -1;
		}
		if (pFirstFileNotifyInfo->Action == FILE_ACTION_ADDED || pFirstFileNotifyInfo->Action == FILE_ACTION_MODIFIED) 
		{
			Sleep(50);
			do 
			{
				RtlZeroMemory(pFileNotifyInfo, 1024);
				memcpy(pFileNotifyInfo, pFirstFileNotifyInfo, pFirstFileNotifyInfo->NextEntryOffset ? static_cast<size_t>(pFirstFileNotifyInfo->NextEntryOffset) - 1 : 1024);
				pFirstFileNotifyInfo = (FILE_NOTIFY_INFORMATION*)((BYTE*)pFirstFileNotifyInfo + pFirstFileNotifyInfo->NextEntryOffset);
				TCHAR szFileName[MAX_PATH], szFullName[MAX_PATH];
				wsprintf(szFileName, TEXT("%ws\\%ws"), szDesktopPath, pFileNotifyInfo->FileName);
				WIN32_FIND_DATAW FindData = {};
				int i = 0;
				while (!FindData.nFileSizeLow && !(FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					FindClose(FindFirstFile(szFileName, &FindData));
					Sleep(20);
					i++;
					if (i == 100) break;
				}
				wsprintf(szFullName, TEXT("%ws\\%ws"), szDesktopPath, FindData.cFileName);
				FILEINFO fInfo = { szFullName, FindData.nFileSizeLow };
				DRIVE* pDrive = driveHead.pNext;
				while (pDrive) 
				{
					//auto pos = find(p->files.begin(), p->files.end(), fInfo);
					if (binary_search(pDrive->vFiles.begin(), pDrive->vFiles.end(), fInfo)) break;
					pDrive = pDrive->pNext;
				}
				TCHAR szMovedName[MAX_PATH], szOriginalMovedName[MAX_PATH], szOriginalName[MAX_PATH];
				bool bMove = true;
				EXEMPT* pExempt = exemptHead.pNext;
				while (pExempt)
				{
					bool bFind = false;
					for (auto ch : pExempt->szName)
					{
						if (ch == TEXT('\\')) bFind = true;
					}
					if (bFind)
					{
						if (ProcessRegex(pExempt->szName, fInfo.szFullPath))
							bMove = false;
					}
					else
					{
						if(ProcessRegex(pExempt->szName, fInfo.Name()))
							bMove = false;
					}
					pExempt = pExempt->pNext;
				}
				if (!bMove) continue;
				if (!pDrive)
				{
					if (szDefaultMovePath[0] == TEXT('\0')) continue;
					wsprintf(szMovedName, TEXT("%ws\\%ws\\%d - %ws"), szDesktopPath, szDefaultMovePath, (int)time(NULL), pFileNotifyInfo->FileName);
					if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						wsprintf(szOriginalMovedName, TEXT("%ws\\%ws\\folder-%ws-%d"), szDesktopPath, szDefaultMovePath, pFileNotifyInfo->FileName, (int)time(NULL));
					else
						wsprintf(szOriginalMovedName, TEXT("%ws\\%ws\\%ws"), szDesktopPath, szDefaultMovePath, pFileNotifyInfo->FileName);
				}
				else
				{
					wsprintf(szMovedName, TEXT("%ws\\%ws\\%d - %ws"), szDesktopPath, pDrive->szPath, (int)time(NULL), pFileNotifyInfo->FileName);
					if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						wsprintf(szOriginalMovedName, TEXT("%ws\\%ws\\folder-%ws-%d"), szDesktopPath, pDrive->szPath, pFileNotifyInfo->FileName, (int)time(NULL));
					else
						wsprintf(szOriginalMovedName, TEXT("%ws\\%ws\\%ws"), szDesktopPath, pDrive->szPath, pFileNotifyInfo->FileName);
				}
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
	delete[] dwBuffer, dwBuffer2;
	return 0;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	HWND hWnd;

	hInst = hInstance;

	if (!CheckSingleInstance())
	{
		MessageBox(NULL, TEXT("Another instance of this program is already running!"), TEXT("Error!"), MB_ICONERROR);
		ExitProcess(0);
	}

	hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);

	if (!hWnd)
	{
		return FALSE;
	}
	hThisWnd = hWnd;

	lstrcpy(szHomePath, _wgetenv(TEXT("appdata")));
	lstrcat(szHomePath, TEXT("\\..\\Local\\Desktop Tidiness Helper"));

	// Check if needed to create folder
	DWORD dwFileAttr = GetFileAttributes(szHomePath);
	if (dwFileAttr == -1 || !(dwFileAttr & FILE_ATTRIBUTE_DIRECTORY))
		CreateDirectory(szHomePath, NULL);

	// Init Error log
	wsprintf(szLogfilePath, TEXT("%ws\\error.log"), szHomePath);
	hLogfile = CreateFile(szLogfilePath, FILE_GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	DWORD dwErr = GetLastError();
	if (!dwErr) 
	{
		wsprintf(szLogBuffer, szgLogs[10], CurTime());
		WriteLog();
	}
	LONG lTmp = 0;
	SetFilePointer(hLogfile, 0, &lTmp, FILE_END);
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
	if (szDesktopPath[0] == TEXT('\0')) 
	{
		LPITEMIDLIST pIDL;
		LPMALLOC pShellMalloc;
		if (SUCCEEDED(SHGetMalloc(&pShellMalloc))) 
		{
			if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_DESKTOP, &pIDL))) 
			{
				SHGetPathFromIDList(pIDL, szDesktopPath);
				pShellMalloc->Free(pIDL);
			}
			pShellMalloc->Release();
		}
		TCHAR chTmp[MAX_PATH] = TEXT("");
		wsprintf(chTmp, TEXT("DesktopPath = \"%ws\"\n"), szDesktopPath);
		WriteFile(hConfigfile, chTmp, lstrlen(chTmp) * sizeof(TCHAR), &dwTmpNULL, NULL);
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
	for (int i = 3; i < 26; i++)
	{
		DeviceArrivalHandler(i);
	}

	return TRUE;
}

void ExitInstance() 
{
	CloseHandle(hConfigfile);
	CloseHandle(hQueuefile);
	Shell_NotifyIcon(NIM_DELETE, &NotifyIconData);
}

vector<FILEINFO> IndexerWorker(LPTSTR szDir, DRIVE* pDrive) 
{
	vector<FILEINFO> fInfo;
	WIN32_FIND_DATAW findData = {};
	TCHAR szFindCommand[MAX_PATH], szFullName[MAX_PATH];
	wsprintf(szFindCommand, TEXT("%ws\\*"), szDir);
	HANDLE hFind = FindFirstFileW(szFindCommand, &findData);
	if (hFind == INVALID_HANDLE_VALUE) return std::move(fInfo);
	do 
	{
		if (!lstrcmp(findData.cFileName, TEXT(".")) || !lstrcmp(findData.cFileName, TEXT(".."))) continue;
		wsprintf(szFullName, TEXT("%ws\\%ws"), szDir, findData.cFileName);
		fInfo.push_back({ szFullName, findData.nFileSizeLow });
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) 
		{
			TCHAR szDFS[MAX_PATH];
			wsprintf(szDFS, TEXT("%ws\\%ws"), szDir, findData.cFileName);
			vector<FILEINFO> vRet = IndexerWorker(szDFS, pDrive);
			fInfo.insert(fInfo.end(), vRet.begin(), vRet.end());
		}
	} while (FindNextFileW(hFind, &findData));
	FindClose(hFind);
	return std::move(fInfo);
}

DWORD WINAPI Indexer(LPVOID lpParameter) 
{
	DRIVE* pDrive = (DRIVE*)lpParameter;
	pDrive->vFiles = IndexerWorker(pDrive->szLetter, pDrive);
	for (auto& File : pDrive->vFiles)
	{
		File.InitName();
	}
	sort(pDrive->vFiles.begin(), pDrive->vFiles.end());
	/*d->files.clear();
	d->files.insert(d->files.end(), fInfo.begin(), fInfo.end());*/
	return 0;
}

DWORD WINAPI ConfigEditHandler(LPVOID lpParameter)
{
	bPaused = true;

	// Log config being changed
	wsprintf(szLogBuffer, szgLogs[8], CurTime());
	WriteLog();

	CloseHandle(hConfigfile);
	
	// Clear drive linking list
	DeleteList(&driveHead);

	//Clear exempt list
	DeleteList(&exemptHead);

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

	//Scan
	for (int i = 3; i < 26; i++)
	{
		DeviceArrivalHandler(i);
	}

	return 0;
}

DWORD WINAPI CopyUDisk(LPVOID pDrive)
{
	DRIVE* _pDrive = (DRIVE*)pDrive;
	TCHAR szDir[MAX_PATH];
	wsprintf(szDir, TEXT("%ws\\%ws\\%d"), szDesktopPath, _pDrive->szPath, _pDrive->dwUUID);
	SHFILEOPSTRUCT FO;
	FO.hwnd = NULL;
	FO.fAnyOperationsAborted = FALSE;
	FO.fFlags = FOF_NO_UI; // or it will create popup windows
	FO.hNameMappings = NULL;
	FO.wFunc = FO_COPY;
	FO.pFrom = _pDrive->szLetter;
	FO.pTo = szDir;
	SHFileOperation(&FO);
	return 0;
}

void DeviceArrivalHandler(int nVolumeIndex) {
	TCHAR szVolumeLetter[] = TEXT("C:");
	szVolumeLetter[0] = nVolumeIndex + TEXT('A');
	TCHAR szVolumeName[MAX_PATH] = TEXT("");
	DWORD dwSerialNumber = 0, dwFileSystemFlags = 0;

	if (GetDriveType(szVolumeLetter) != DRIVE_REMOVABLE) return;
	GetVolumeInformation(szVolumeLetter, szVolumeName, MAX_PATH, &dwSerialNumber, NULL, &dwFileSystemFlags, NULL, 0);

	// Log drive arrival
	wsprintf(szLogBuffer, szgLogs[2], CurTime(), szVolumeName, szVolumeLetter, dwSerialNumber);
	WriteLog();

	if (bPaused) return;
	if (dwSerialNumber == 0) return;

	DRIVE* pDrive = driveHead.pNext;
	while (pDrive && pDrive->dwUUID != dwSerialNumber) pDrive = pDrive->pNext;
	if (!pDrive)
	{
		TCHAR szString1[MAX_PATH] = TEXT("");
		wsprintf(szString1, TEXT("\n[%d]\n# VolumeName = %ws\nMovePath = \"\"\n"), dwSerialNumber, szVolumeName);
		WriteFile(hConfigfile, szString1, lstrlen(szString1) * sizeof(TCHAR), &dwTmpNULL, NULL);
		FlushFileBuffers(hConfigfile);
		DRIVE* nDrive = new DRIVE;
		nDrive->pNext = driveHead.pNext;
		nDrive->dwUUID = dwSerialNumber;
		nDrive->bIsAvailable = true;
		//lstrcpy(nDrive->letter, volumeLetter);
		driveHead.pNext = nDrive;
	}
	else 
	{
		//if (p->path[0] == TEXT('\0')) return;
		pDrive->bIsAvailable = true;
		lstrcpy(pDrive->szLetter, szVolumeLetter);
		CreateThread(NULL, 0, Indexer, pDrive, 0, NULL);
	}

	if (bCopyUDisk) 
	{
		//if (p->path[0] == TEXT('\0')) return;
		Sleep(1000);
		CreateThread(NULL, 0, CopyUDisk, pDrive, 0, NULL);
	}
}

DWORD WINAPI FindFileDlg(LPVOID lpUnused)
{
	DialogBox(hInst, MAKEINTRESOURCE(IDD_FINDDIALOG), hThisWnd, FindDlgProc);
	return 0;
}

INT_PTR CALLBACK FindDlgProc(HWND hDialog, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
	{
		break;
	}
	case WM_DEVICECHANGE:
	{
		Sleep(2000);

		break;
	}
	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case IDC_FIND:
		{
			SendDlgItemMessage(hDialog, IDC_FILELIST, LB_RESETCONTENT, 0, 0);

			HDC hDC = GetDC(GetDlgItem(hDialog, IDC_FILELIST));
			RECT rcRect;
			GetWindowRect(GetDlgItem(hDialog, IDC_FILELIST), &rcRect);
			SIZE szSize;
			int nMaxWidth = rcRect.right - rcRect.left;

			TCHAR szFindName[MAX_PATH];
			GetDlgItemText(hDialog, IDC_FINDEDIT, szFindName, MAX_PATH - 1);
			if (szFindName[0] == TEXT('\0')) break;
			auto vResults = FindInUDisk(szFindName);
			for (auto& sResult : vResults)
			{
				SendDlgItemMessage(hDialog, IDC_FILELIST, LB_ADDSTRING, 0, (LPARAM)sResult.c_str());
				GetTextExtentPoint(hDC, sResult.c_str(), static_cast<int>(sResult.size() + 1), &szSize);
				if (szSize.cx > nMaxWidth) nMaxWidth = szSize.cx;
			}
			nMaxWidth -= 320; // maybe OK
			SendDlgItemMessage(hDialog, IDC_FILELIST, LB_SETHORIZONTALEXTENT, static_cast<WPARAM>(nMaxWidth), 0);
			break;
		}
		case IDC_FILELIST:
		{
			switch (HIWORD(wParam))
			{
			case LBN_DBLCLK:
			{
				TCHAR szFileName[MAX_PATH], szCmdLine[MAX_PATH];
				GetDlgItemText(hDialog, IDC_CMDEDIT, szCmdLine, MAX_PATH - 1);
				LRESULT lIndex = SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETCURSEL, 0, 0);
				SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETTEXT, (WPARAM)lIndex, (LPARAM)szFileName);
				OpenIn(szFileName, szCmdLine);
				break;
			}
			case LBN_SELCHANGE:
			{
				TCHAR szFileName[MAX_PATH], szFileInfo[MAX_PATH * 2];
				LRESULT index = SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETCURSEL, 0, 0);
				SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETTEXT, (WPARAM)index, (LPARAM)szFileName);
				GetFileInfo(szFileName, szFileInfo);
				SetDlgItemText(hDialog, IDC_FILEINFO, szFileInfo);
				break;
			}
			default:
				break;
			}
			break;
		}
		case IDC_OPEN:
		{
			TCHAR szFileName[MAX_PATH],szCmdLine[MAX_PATH];
			GetDlgItemText(hDialog, IDC_CMDEDIT, szCmdLine, MAX_PATH - 1);
			LRESULT lIndex = SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETCURSEL, 0, 0);
			SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETTEXT, (WPARAM)lIndex, (LPARAM)szFileName);
			OpenIn(szFileName, szCmdLine);
			break;
		}
		case IDC_OINE:
		{
			TCHAR szFileName[MAX_PATH], szCmdLine[MAX_PATH];
			LRESULT lIndex = SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETCURSEL, 0, 0);
			SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETTEXT, (WPARAM)lIndex, (LPARAM)szFileName);
			wsprintf(szCmdLine, TEXT("/select ,%ws"), szFileName);
			OpenIn(TEXT("explorer.exe"), szCmdLine);
			break;
		}
		case IDC_OINP:
		{
			TCHAR szFileName[MAX_PATH], szCmdLine[MAX_PATH], szCmdFinal[MAX_PATH];
			GetDlgItemText(hDialog, IDC_CMDEDIT, szCmdLine, MAX_PATH - 1);
			LRESULT lIndex = SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETCURSEL, 0, 0);
			SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETTEXT, (WPARAM)lIndex, (LPARAM)szFileName);
			wsprintf(szCmdFinal, TEXT("%ws %ws"), szFileName, szCmdLine);
			OpenIn(TEXT("powershell.exe"), szCmdFinal);
			break;
		}
		case IDC_COPYPATH:
		{
			TCHAR szFileName[MAX_PATH];
			LRESULT lIndex = SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETCURSEL, 0, 0);
			SendDlgItemMessage(hDialog, IDC_FILELIST, LB_GETTEXT, (WPARAM)lIndex, (LPARAM)szFileName);
			CopyToClipBoard(szFileName);
			break;
		}
		case IDC_EXIT:
		{
			EndDialog(hDialog, LOWORD(wParam));
			return INT_PTR(TRUE);
		}
		default:
			break;
		}
		break;
	}
	case WM_CLOSE:
	{
		EndDialog(hDialog, LOWORD(wParam));
		return INT_PTR(TRUE);
	}
	default:
		break;
	}
	return 0;
}

bool ProcessRegex(LPCTSTR szRegex, LPCTSTR szTarget)
{
	vector<size_t> vIndex;
	vector<wstring> sSubStrs;
	wstring sRegex(szRegex), sTarget(szTarget);
	if (!sRegex.size()) return true;
	if (!sTarget.size()) return false;
	for (auto& ch : sRegex)
	{
		ch = towlower(ch);
	}
	for (auto& ch : sTarget)
	{
		ch = towlower(ch);
	}
	size_t szFind = 0;
	while (true)
	{
		szFind = sRegex.find(TEXT('*'), szFind);
		if (szFind == wstring::npos) break;
		vIndex.push_back(szFind);
		szFind++;
	}
	size_t szIndex = 0;
	while (szIndex < vIndex.size())
	{
		if (szIndex + 1 < vIndex.size())
		{
			if (vIndex[szIndex + 1] - vIndex[szIndex] > 1)
			{
				sSubStrs.push_back(sRegex.substr(vIndex[szIndex] + 1, vIndex[szIndex + 1] - vIndex[szIndex] - 1));
			}
		}
		else
		{
			if (vIndex[szIndex] < sRegex.size() - 1)
			{
				sSubStrs.push_back(sRegex.substr(vIndex[szIndex] + 1, sRegex.size() - vIndex[szIndex] - 1));
			}
		}
		szIndex++;
	}
	if (szIndex == 0)
		sSubStrs.push_back(sRegex);
	szIndex = 0, szFind = 0;
	while (szIndex < sSubStrs.size())
	{
		szFind = sTarget.find(sSubStrs[szIndex], szFind);
		if (szFind == wstring::npos) break;
		szFind += sSubStrs[szIndex].size();
		szIndex++;
	}
	return szFind != wstring::npos;
}

vector<wstring> FindInUDisk(LPCTSTR szFileName)
{
	DWORD dwDrives = GetLogicalDrives();
	TCHAR szVolumeLetter[3] = TEXT("A:");
	vector<DWORD> vUUIDs;
	vector<wstring> vFiles;
	for (char i = 0; i < 26; ++i,dwDrives >>= 1)
	{
		if (!(dwDrives & 0x1)) continue;
		szVolumeLetter[0] = i + TEXT('A');
		if (GetDriveType(szVolumeLetter) != DRIVE_REMOVABLE) continue;
		TCHAR volumeName[MAX_PATH] = TEXT("");
		DWORD serialNumber = 0, fileSystemFlags = 0;
		GetVolumeInformation(szVolumeLetter, volumeName, MAX_PATH, &serialNumber, NULL, &fileSystemFlags, NULL, 0);
		vUUIDs.push_back(serialNumber);
	}
	DRIVE* pDrive = driveHead.pNext;
	while (pDrive)
	{
		if (!pDrive->bIsAvailable || find(vUUIDs.begin(), vUUIDs.end(), pDrive->dwUUID) == vUUIDs.end())
		{
			pDrive = pDrive->pNext;
			continue;
		}
		for (auto& File : pDrive->vFiles)
		{
			int nHead = 3, nTail = 3; // skip drive letter
			size_t szSize = lstrlen(File.szFullPath), fssize = lstrlen(szFileName);

			int nCur = 0;
			bool bFlag = false;
			while (nHead < szSize)
			{
				if (szFileName[nCur] == File.szFullPath[nHead]
					|| (szFileName[nCur] >= TEXT('A') && szFileName[nCur] <= TEXT('Z') && szFileName[nCur] - TEXT('A') == File.szFullPath[nHead] - TEXT('a'))
					|| (szFileName[nCur] >= TEXT('a') && szFileName[nCur] <= TEXT('z') && szFileName[nCur] - TEXT('a') == File.szFullPath[nHead] - TEXT('A'))
					)
				{
					bFlag = true;
					nCur++;
					if (nCur >= fssize) //succeeded
					{
						if (nHead == szSize)
							nTail = nHead;
						else
							nTail = nHead + 1;
						while (File.szFullPath[nTail] != TEXT('\\') && nTail < szSize)
							nTail++;
						TCHAR tmp = File.szFullPath[nTail];
						File.szFullPath[nTail] = TEXT('\0');
						vFiles.push_back(File.szFullPath);
						File.szFullPath[nTail] = tmp;
						break;
					}
				}
				else
				{
					bFlag = false;
					nCur = 0;
				}
				nHead++;
			}
			nHead = nTail + 1;

		}
		pDrive = pDrive->pNext;
	}
	set<wstring> s(vFiles.begin(), vFiles.end());
	vFiles.assign(s.begin(), s.end());
	return std::move(vFiles);
}

void CopyToClipBoard(LPCTSTR szFileName)
{
	HGLOBAL hGlobal = GlobalAlloc(GHND, (lstrlen(szFileName) + static_cast<size_t>(1)) * sizeof(TCHAR));
	if (hGlobal == INVALID_HANDLE_VALUE || hGlobal == 0) return;
	TCHAR* pGlobal = (TCHAR*)GlobalLock(hGlobal);
	if (pGlobal != 0)
	{
		lstrcpy(pGlobal, szFileName);
		GlobalUnlock(hGlobal);

		OpenClipboard(NULL);
		EmptyClipboard();
		SetClipboardData(CF_UNICODETEXT, hGlobal);
		CloseClipboard();
	}
}

void OpenIn(LPCTSTR szExeName, LPCTSTR szCmdLine)
{
	ShellExecute(NULL, TEXT("open"), szExeName, szCmdLine, TEXT(""), SW_SHOWNORMAL);
}

void GetFileInfo(LPCTSTR szFileName, LPTSTR szInfo)
{
	WIN32_FILE_ATTRIBUTE_DATA fAttrData;
	FILETIME fCreationTime,fLastAccessTime,fLastWriteTime;
	SYSTEMTIME CreationTime, LastAccessTime, LastWriteTime;
	GetFileAttributesEx(szFileName, GetFileExInfoStandard, &fAttrData);
	FileTimeToLocalFileTime(&fAttrData.ftCreationTime, &fCreationTime);
	FileTimeToSystemTime(&fCreationTime, &CreationTime);
	FileTimeToLocalFileTime(&fAttrData.ftLastAccessTime, &fLastAccessTime);
	FileTimeToSystemTime(&fLastAccessTime, &LastAccessTime);
	FileTimeToLocalFileTime(&fAttrData.ftLastWriteTime, &fLastWriteTime);
	FileTimeToSystemTime(&fLastWriteTime, &LastWriteTime);
	wsprintf(szInfo,
		TEXT("%ws [%ws]\nSize: %d Bytes\nCreation Time: %d/%02d/%02d %02d:%02d:%02d\nLast Access Time: %d/%02d/%02d %02d:%02d:%02d\nLast Write Time: %d/%02d/%02d %02d:%02d:%02d"),
		szFileName,
		(fAttrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? TEXT("DIR") : TEXT("FILE"),
		(fAttrData.nFileSizeHigh << 16) + fAttrData.nFileSizeLow,
		CreationTime.wYear, CreationTime.wMonth, CreationTime.wDay, CreationTime.wHour, CreationTime.wMinute, CreationTime.wSecond,
		LastAccessTime.wYear, LastAccessTime.wMonth, LastAccessTime.wDay, LastAccessTime.wHour, LastAccessTime.wMinute, LastAccessTime.wSecond,
		LastWriteTime.wYear, LastWriteTime.wMonth, LastWriteTime.wDay, LastWriteTime.wHour, LastWriteTime.wMinute, LastWriteTime.wSecond
	);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY: 
	{
		ExitInstance();
		PostQuitMessage(0);
		break;
	}
	// Device Change
	case WM_DEVICECHANGE: 
	{
		PDEV_BROADCAST_HDR pDB = (PDEV_BROADCAST_HDR)lParam;
		PDEV_BROADCAST_VOLUME pDBV = (PDEV_BROADCAST_VOLUME)pDB;
		TCHAR szVolumeLetter[] = TEXT("C:");
		switch (wParam)
		{
		case DBT_DEVICEARRIVAL: 
		{
			DWORD dwUnitMask = pDBV->dbcv_unitmask;
			for (char i = 0; i < 26; ++i, dwUnitMask >>= 1) 
			{
				if (!(dwUnitMask & 0x1)) continue;
				DeviceArrivalHandler(i);
			}
			break;
		}
		case DBT_DEVICEREMOVECOMPLETE: 
		{
			DWORD dwUnitMask = pDBV->dbcv_unitmask;
			for (char i = 0; i < 26; ++i, dwUnitMask >>= 1) 
			{
				if (!(dwUnitMask & 0x1)) continue;
				szVolumeLetter[0] = i + TEXT('A');
	
				DRIVE* pDrive = driveHead.pNext;
				while (pDrive && pDrive->szLetter[0] != szVolumeLetter[0]) pDrive = pDrive->pNext;
				if (pDrive)
				{
					pDrive->bIsAvailable = false;
					if (bDeleteIndexOnEject)
					{
						pDrive->vFiles.clear();
						pDrive->vFiles.shrink_to_fit();
					}
				}
				
				// Log device removal
				wsprintf(szLogBuffer, szgLogs[3], CurTime(), szVolumeLetter);
				WriteLog();
			}
			break;
		}
		default: 
		{
			break;
		}
		}
		break;
	}
	// Tray Icon
	case UM_TRAYICON: 
	{
		if (bPaused) break;
		static clock_t clLast = 0;
		clock_t clCur = clock();
		switch LOWORD(lParam) 
		{
		case NIN_SELECT:
		case NIN_KEYSELECT:
		case WM_CONTEXTMENU: 
		{
			POINT Pt;
			GetCursorPos(&Pt);
			SetForegroundWindow(hWnd);
			HMENU hPopupMenu = GetSubMenu(hMenu, 0);
			TrackPopupMenu(hPopupMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, Pt.x, Pt.y, 0, hWnd, NULL);
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
		case ID_X_FIND:
		{
			CreateThread(NULL, 0, FindFileDlg, hWnd, 0, NULL);
			break;
		}
		default:
			break;
		}
	}
	default: 
	{
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
	MyRegisterClass(hInstance); 
	if (!InitInstance (hInstance, nCmdShow)) return FALSE;
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0) 
	{ 
		TranslateMessage(&msg); 
		DispatchMessage(&msg); 
	}
	return (int) msg.wParam;
}