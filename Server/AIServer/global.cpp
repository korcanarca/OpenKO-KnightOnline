﻿#include "stdafx.h"
#include "global.h"

#include <shared/StringConversion.h>

BOOL CheckGetVarString(int nLength, char* tBuf, const char* sBuf, int nSize, int& index)
{
	int nRet = GetVarString(tBuf, sBuf, nSize, index);
	if (nRet <= 0
		|| nRet > nLength)
		return FALSE;

	return TRUE;
}

int GetVarString(char* tBuf, const char* sBuf, int nSize, int& index)
{
	int nLen = 0;

	if (nSize == sizeof(BYTE))
		nLen = GetByte(sBuf, index);
	else
		nLen = GetShort(sBuf, index);

	GetString(tBuf, sBuf, nLen, index);
	*(tBuf + nLen) = 0;

	return nLen;
}

void GetString(char* tBuf, const char* sBuf, int len, int& index)
{
	memcpy(tBuf, sBuf + index, len);
	index += len;
}

BYTE GetByte(const char* sBuf, int& index)
{
	int t_index = index;
	index++;
	return (BYTE) (*(sBuf + t_index));
}

int GetShort(const char* sBuf, int& index)
{
	index += 2;
	return *(short*) (sBuf + index - 2);
}

int GetInt(const char* sBuf, int& index)
{
	index += 4;
	return *(int*) (sBuf + index - 4);
}

DWORD GetDWORD(const char* sBuf, int& index)
{
	index += 4;
	return *(DWORD*) (sBuf + index - 4);
}

float Getfloat(const char* sBuf, int& index)
{
	index += 4;
	return *(float*) (sBuf + index - 4);
}

void SetString(char* tBuf, const char* sBuf, int len, int& index)
{
	CopyMemory(tBuf + index, sBuf, len);
	index += len;
}

void SetVarString(char* tBuf, const char* sBuf, int len, int& index)
{
	*(tBuf + index) = (BYTE) len;
	index ++;

	CopyMemory(tBuf + index, sBuf, len);
	index += len;
}

void SetByte(char* tBuf, BYTE sByte, int& index)
{
	*(tBuf + index) = (char) sByte;
	index++;
}

void SetShort(char* tBuf, int sShort, int& index)
{
	short temp = (short) sShort;

	CopyMemory(tBuf + index, &temp, 2);
	index += 2;
}

void SetInt(char* tBuf, int sInt, int& index)
{
	CopyMemory(tBuf + index, &sInt, 4);
	index += 4;
}

void SetDWORD(char* tBuf, DWORD sDword, int& index)
{
	CopyMemory(tBuf + index, &sDword, 4);
	index += 4;
}

void Setfloat(char* tBuf, float sFloat, int& index)
{
	CopyMemory(tBuf + index, &sFloat, 4);
	index += 4;
}

void SetString1(char* tBuf, const char* sBuf, BYTE len, int& index)
{
	SetByte(tBuf, len, index);
	SetString(tBuf, sBuf, len, index);
}

void SetString2(char* tBuf, const char* sBuf, short len, int& index)
{
	SetShort(tBuf, len, index);
	SetString(tBuf, sBuf, len, index);
}

int ParseSpace(char* tBuf, const char* sBuf)
{
	int i = 0, index = 0;
	BOOL flag = FALSE;

	while (sBuf[index] == ' '
		|| sBuf[index] == '\t')
		index++;

	while (sBuf[index] != ' '
		&& sBuf[index] != '\t'
		&& sBuf[index] != (BYTE) 0)
	{
		tBuf[i++] = sBuf[index++];
		flag = TRUE;
	}
	tBuf[i] = 0;

	while (sBuf[index] == ' '
		|| sBuf[index] == '\t')
		index++;

	if (!flag)
		return 0;

	return index;
}

CString GetProgPath()
{
	TCHAR Buf[256], Path[256];
	TCHAR drive[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];

	::GetModuleFileName(AfxGetApp()->m_hInstance, Buf, 256);
	_tsplitpath(Buf, drive, dir, fname, ext);
	_tcscpy(Path, drive);
	_tcscat(Path, dir);
	return Path;
}

int myrand(int min, int max, BOOL bSame)
{
	static int nOld = 0;
	int nRet = 0;
	int nLoop = 10;

	if (min == max)
		return min;

	while (nLoop--)
	{
		nRet = (rand() % (max - min + 1)) + min;
		if (bSame)
			return nRet;

		if (nRet != nOld)
		{
			nOld = nRet;
			return nRet;
		}
	}

	return nRet;
}

///////////////////////////////////////////////////////////////////////////
//	XdY 형식의 주사위 굴리기
//
int XdY(int x, int y)
{
	int temp = 0;
	if (x <= 0)
		return myrand(x, y, TRUE);

	for (int i = 0; i < x; i++)
		temp += myrand(1, y, TRUE);

	return temp;
}

///////////////////////////////////////////////////////////////////////////
//	DWORD 의 Max 값을 채크하면서 증가시킨다.
//
void CheckMaxValue(DWORD& dest, DWORD add)
{
	DWORD Diff = _MAX_DWORD - dest;

	if (add <= Diff)
		dest += add;
	else
		dest = _MAX_DWORD;
}

///////////////////////////////////////////////////////////////////////////
//	int 의 Max 값을 채크하면서 증가시킨다.
//
void CheckMaxValue(int& dest, int add)
{
	int Diff = _MAX_INT - dest;

	if (add <= Diff)
		dest += add;
	else
		dest = _MAX_INT;
}

///////////////////////////////////////////////////////////////////////////
//	short 의 Max 값을 채크하면서 증가시킨다.
//
void CheckMaxValue(short& dest, short add)
{
	short Diff = _MAX_SHORT - dest;

	if (add <= Diff)
		dest += add;
	else
		dest = _MAX_SHORT;
}

BOOL CheckMaxValueReturn(DWORD& dest, DWORD add)
{
	DWORD Diff = _MAX_DWORD - dest;

	if (add <= Diff)
		return TRUE;//dest += add;
	else
		return FALSE;//dest = _MAX_DWORD;
}

void LogFileWrite(CString logstr)
{
	CString LogFileName;
	LogFileName.Format(_T("%s\\AIServer.log"), GetProgPath().GetString());

	CFile file;
	if (!file.Open(LogFileName, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite))
		return;

	file.SeekToEnd();

#if defined(_UNICODE)
	const std::string utf8 = WideToUtf8(logstr.GetString(), static_cast<size_t>(logstr.GetLength()));
	file.Write(utf8.c_str(), static_cast<int>(utf8.size()));
#else
	file.Write(logstr, logstr.GetLength());
#endif

	file.Close();
}

void TimeTrace(const TCHAR* pMsg)
{
	CString szMsg;
	CTime time = CTime::GetCurrentTime();
	szMsg.Format(_T("%s,,  time : %d-%d-%d, %d:%d]\n"), pMsg, time.GetYear(), time.GetMonth(), time.GetDay(), time.GetHour(), time.GetMinute());
	TRACE(szMsg);
}
