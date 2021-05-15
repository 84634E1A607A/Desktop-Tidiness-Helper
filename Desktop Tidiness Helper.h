#pragma once

#include "framework.h"

// Structs
struct FILEINFO 
{
	TCHAR szFullPath[MAX_PATH];
	int nNameIndex;
	DWORD dwSize = 0;

	inline const TCHAR* Name() const
	{
		return &(szFullPath[nNameIndex]);
	}

	FILEINFO(LPTSTR _szPath, DWORD _dwSize) :dwSize(_dwSize)
	{ 
		lstrcpy(szFullPath, _szPath);
		InitName(); 
	}
	FILEINFO() :szFullPath(TEXT("")), nNameIndex(0), dwSize(0){}
	bool operator< (const FILEINFO FileInfo) const 
	{
		return lstrcmp(Name(), FileInfo.Name()) < 0;
	}
	bool operator== (const FILEINFO FileInfo) const
	{
		return (!lstrcmp(Name(), FileInfo.Name())) && dwSize == FileInfo.dwSize;
	}
	FILEINFO operator=(const FILEINFO FileInfo)
	{
		lstrcpy(szFullPath, FileInfo.szFullPath);
		nNameIndex = FileInfo.nNameIndex;
		dwSize = FileInfo.dwSize;
		return *this;
	}

	void InitName()
	{
		int nLength = static_cast<int>(lstrlen(szFullPath)), nIndex;
		if (nLength == 0)
		{
			nNameIndex = 0;
			return;
		}
		for (nIndex = nLength - 1; szFullPath[nIndex] != TEXT('\\'); nIndex--)
		{
			if (nIndex < 0) break;
		}
		nNameIndex = nIndex + 1;
	}
};

struct DRIVE 
{
	DWORD dwUUID = 0;
	TCHAR szPath[MAX_PATH] = TEXT("");
	TCHAR szLetter[3] = TEXT("");
	vector<FILEINFO> vFiles;
	volatile bool bIsAvailable = false;
	DRIVE* pNext = NULL;
} driveHead, curDriveHead;


void WriteLog();

const LPWSTR CurTime();

void MoveQueue();

void Trim(LPTSTR szString);

bool CheckSingleInstance();

template<typename _Ptr>
void DeleteList(_Ptr pListHead)
{
	_Ptr pThis = pListHead->pNext;
	while (pThis)
	{
		pListHead->pNext = pThis->pNext;
		delete pThis;
		pThis = pListHead->pNext;
	}
}

void LoadExempts(LPTSTR szExempt);

void ReadConfig();

vector<FILEINFO> ListDir(LPTSTR szDir);

//DWORD WINAPI Monitor(LPVOID lpParameter);

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);

void ExitInstance();

vector<FILEINFO> IndexerWorker(LPTSTR szDir, DRIVE* pDrive);

DWORD WINAPI Indexer(LPVOID lpParameter);

DWORD WINAPI ConfigEditHandler(LPVOID lpParameter);

DWORD WINAPI CopyUDisk(LPVOID pDrive);

void DeviceArrivalHandler(int nVolumeIndex);

DWORD WINAPI FindFileDlg(LPVOID lpUnused);

INT_PTR CALLBACK FindDlgProc(HWND hDialog, UINT message, WPARAM wParam, LPARAM lParam);

bool ProcessRegex(wstring szRegex, LPCTSTR szTarget);

vector<wstring> FindInUDisk(LPCTSTR szFileName);

void CopyToClipBoard(LPCTSTR szFileName);

void OpenIn(LPCTSTR szExeName, LPCTSTR szCmdLine);

void GetFileInfo(LPCTSTR szFileName, LPTSTR szInfo);

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

ATOM MyRegisterClass(HINSTANCE hInstance);
