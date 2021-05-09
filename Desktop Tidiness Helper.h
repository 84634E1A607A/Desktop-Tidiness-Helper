#pragma once

#include "framework.h"

// Structs
struct FILEINFO 
{
	TCHAR fullpath[MAX_PATH];
	int name_index;
	DWORD size = 0;

	inline const TCHAR* name() const
	{
		return &(fullpath[name_index]);
	}

	FILEINFO(LPWSTR p, DWORD s) :size(s)
	{ 
		lstrcpy(fullpath, p);
		initname(); 
	}
	FILEINFO() :fullpath(TEXT("")), name_index(0), size(0){}
	bool operator< (const FILEINFO r) const 
	{
		return lstrcmp(name(), r.name()) < 0;
	}
	bool operator== (const FILEINFO r) const
	{
		return (!lstrcmp(name(), r.name())) && size == r.size;
	}
	FILEINFO operator=(const FILEINFO r)
	{
		lstrcpy(fullpath, r.fullpath);
		name_index = r.name_index;
		size = r.size;
		return *this;
	}

	void initname()
	{
		int l = static_cast<int>(lstrlen(fullpath)), i;
		if (l == 0)
		{
			name_index = 0;
			return;
		}
		for (i = l - 1; fullpath[i] != TEXT('\\'); i--)
		{
			if (i < 0) break;
		}
		name_index = i + 1;
	}
	void getdir(TCHAR* path) const
	{
		int l = static_cast<int>(lstrlen(fullpath)), i;
		if (l == 0)
		{
			path[0] = TEXT('\0');
			return;
		}
		for (i = l - 1; fullpath[i] != TEXT('\\'); i--)
		{
			if (i < 0) break;
		}
		for (int j = 0; j <= i - 1; j++)
		{
			path[j] = fullpath[j];
		}
	}

	wstring dir() const
	{
		wstring _fullpath(fullpath);
		return std::move(_fullpath.substr(0, _fullpath.rfind(TEXT('\\'))));
	}
};

struct DRIVE 
{
	DWORD uuid = 0;
	TCHAR path[MAX_PATH] = TEXT("");
	TCHAR letter[3] = TEXT("");
	vector<FILEINFO> files;
	volatile bool isavailable = false;
	DRIVE* pnext = NULL;
} driveHead, curDriveHead;

struct EXEMPT 
{
	TCHAR name[MAX_PATH] = TEXT("");
	EXEMPT* pnext = NULL;
} exemptHead;


void WriteLog();

const LPWSTR CurTime();

void MoveQueue();

void trim(LPTSTR str);

bool CheckSingleInstance();

void ReadConfig();

DWORD WINAPI Monitor(LPVOID lpParameter);

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);

void ExitInstance();

vector<FILEINFO> IndexerWorker(LPWSTR dir, DRIVE* pDrive);

DWORD WINAPI Indexer(LPVOID lpParameter);

DWORD WINAPI ConfigEditHandler(LPVOID lpParameter);

DWORD WINAPI CopyUDisk(LPVOID pDrive);

void DeviceArrivalHandler(int volumeIndex);

DWORD WINAPI FindFileDlg(LPVOID unused);

INT_PTR CALLBACK FindDlgProc(HWND hDialog, UINT message, WPARAM wParam, LPARAM lParam);

bool ProcessRegex(LPCTSTR regex, LPCTSTR target);

vector<wstring> FindInUDisk(LPCTSTR fname);

void CopyToClipBoard(LPCTSTR fname);

void OpenIn(LPCTSTR exename, LPCTSTR cmdline);

void GetFileInfo(LPCTSTR fname, LPTSTR info);

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

ATOM MyRegisterClass(HINSTANCE hInstance);
