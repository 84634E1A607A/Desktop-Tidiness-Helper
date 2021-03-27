#pragma once

#include <stdio.h>
#include <Dbt.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <time.h>
#include <vector>
#include <algorithm>

// Structs
struct FILEINFO {
	TCHAR name[MAX_PATH] = TEXT("");
	DWORD size = 0;
	FILEINFO(LPWSTR n, DWORD s) { lstrcpy(name, n); size = s; }
	FILEINFO() {}
	bool operator< (const FILEINFO r) const {
		return lstrcmp(this->name, r.name) < 0;
	}
	bool operator== (const FILEINFO r) const {
		return !lstrcmp(this->name, r.name) && this->size == r.size;
	}
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


void WriteLog();

const LPWSTR CurTime();

void MoveQueue();

void trim(LPWSTR str);

void ReadConfig();

DWORD __stdcall Monitor(LPVOID lpParameter);

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);

void ExitInstance();

vector<FILEINFO> IndexerWorker(LPWSTR dir, bool docopy, DRIVE* pDrive);

DWORD __stdcall Indexer(LPVOID lpParameter);

DWORD __stdcall ConfigEditHandler(LPVOID lpParameter);

DWORD __stdcall CopyUDisk(LPVOID pDrive);

void DeviceArrivalHandler(int volumeIndex);

LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

ATOM MyRegisterClass(HINSTANCE hInstance);
