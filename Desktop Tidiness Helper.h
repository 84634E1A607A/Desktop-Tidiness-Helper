#pragma once

#include "framework.h"

// Structs
struct FILEINFO 
{
	TCHAR fullpath[MAX_PATH];
	LPTSTR name;
	DWORD size = 0;
	FILEINFO(LPWSTR p, DWORD s) :size(s) { lstrcpy(fullpath, p); initname(); }
	FILEINFO() :fullpath(TEXT("")), name(fullpath) {}
	bool operator< (const FILEINFO r) const 
	{
		return name < r.name;
	}
	bool operator== (const FILEINFO r) const
	{
		return name == r.name && size == r.size;
	}

	void initname()
	{
		int l = static_cast<int>(lstrlen(fullpath)), i;
		if (l == 0)
		{
			name = &fullpath[0];
			return;
		}
		for (i = l - 1; fullpath[i] != TEXT('\\'); i--)
		{
			if (i < 0) break;
		}
		name = &fullpath[i + 1];
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

vector<wstring> FindInUDisk(LPCTSTR fname);

void CopyToClipBoard(LPCTSTR fname);

void OpenIn(LPCTSTR exename, LPCTSTR cmdline);

void GetFileInfo(LPCTSTR fname, LPTSTR info);

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

ATOM MyRegisterClass(HINSTANCE hInstance);
