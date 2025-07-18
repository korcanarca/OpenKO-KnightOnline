﻿#ifndef _DEFINE_H
#define _DEFINE_H

#if defined(_DEBUG)
#include <iostream>
#endif
#include <string>
#include <mmsystem.h>

#include <shared/globals.h>
#include <shared/StringConversion.h>
#include <shared/STLMap.h>

constexpr int MAX_USER			= 3000;

#define _LISTEN_PORT			15100
#define CLIENT_SOCKSIZE			10
#define DB_PROCESS_TIMEOUT		100

////////////////////////////////////////////////////////////
// Socket Define
////////////////////////////////////////////////////////////
#define SOCKET_BUFF_SIZE		(1024*16)
#define MAX_PACKET_SIZE			(1024*8)

#define PACKET_START1			0XAA
#define PACKET_START2			0X55
#define PACKET_END1				0X55
#define PACKET_END2				0XAA

// status
#define STATE_CONNECTED			0X01
#define STATE_DISCONNECTED		0X02
#define STATE_GAMESTART			0x03

// Socket type
#define TYPE_ACCEPT				0x01
#define TYPE_CONNECT			0x02

// Overlapped flag
#define OVL_RECEIVE				0X01
#define OVL_SEND				0X02
#define OVL_CLOSE				0X03
////////////////////////////////////////////////////////////

typedef union
{
	short int	i;
	BYTE		b[2];
} MYSHORT;

typedef union
{
	int			i;
	BYTE		b[4];
} MYINT;

typedef union
{
	DWORD		w;
	BYTE		b[4];
} MYDWORD;

import VersionManagerModel;
namespace model = versionmanager_model; 

struct _NEWS
{
	char Content[4096]	= {};
	short Size			= 0;
};

struct _SERVER_INFO
{
	char	strServerIP[20]		= {};
	char	strServerName[20]	= {};
	short	sUserCount			= 0;
	short	sUserLimit			= 0;
	short	sServerID			= 1;
};

typedef CSTLMap <model::Version>	VersionInfoList;

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
//
//	Global Function Define
//
// sungyong 2001.11.06

inline void GetString(char* tBuf, char* sBuf, int len, int& index)
{
	memcpy(tBuf, sBuf + index, len);
	index += len;
}

inline BYTE GetByte(char* sBuf, int& index)
{
	int t_index = index;
	index++;
	return (BYTE) (*(sBuf + t_index));
}

inline int GetShort(char* sBuf, int& index)
{
	index += 2;
	return *(short*) (sBuf + index - 2);
}

inline DWORD GetDWORD(char* sBuf, int& index)
{
	index += 4;
	return *(DWORD*) (sBuf + index - 4);
}

inline float Getfloat(char* sBuf, int& index)
{
	index += 4;
	return *(float*) (sBuf + index - 4);
}

inline void SetString(char* tBuf, const char* sBuf, int len, int& index)
{
	memcpy(tBuf + index, sBuf, len);
	index += len;
}

inline void SetByte(char* tBuf, BYTE sByte, int& index)
{
	*(tBuf + index) = (char) sByte;
	index++;
}

inline void SetShort(char* tBuf, int sShort, int& index)
{
	short temp = (short) sShort;

	CopyMemory(tBuf + index, &temp, 2);
	index += 2;
}

inline void SetDWORD(char* tBuf, DWORD sDWORD, int& index)
{
	CopyMemory(tBuf + index, &sDWORD, 4);
	index += 4;
}

inline void Setfloat(char* tBuf, float sFloat, int& index)
{
	CopyMemory(tBuf + index, &sFloat, 4);
	index += 4;
}

inline void SetString1(char* tBuf, const char* sBuf, BYTE len, int& index)
{
	SetByte(tBuf, len, index);
	SetString(tBuf, sBuf, len, index);
}

inline void SetString2(char* tBuf, const char* sBuf, short len, int& index)
{
	SetShort(tBuf, len, index);
	SetString(tBuf, sBuf, len, index);
}

// sungyong 2001.11.06
inline int GetVarString(char* tBuf, char* sBuf, int nSize, int& index)
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

inline void SetVarString(char* tBuf, char* sBuf, int len, int& index)
{
	*(tBuf + index) = (BYTE) len;
	index ++;

	CopyMemory(tBuf + index, sBuf, len);
	index += len;
}

inline CString GetProgPath()
{
	TCHAR Buf[256], Path[256];
	TCHAR drive[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];

	::GetModuleFileName(AfxGetApp()->m_hInstance, Buf, 256);
	_tsplitpath(Buf, drive, dir, fname, ext);
	_tcscpy(Path, drive);
	_tcscat(Path, dir);
	return Path;
}

inline void LogFileWrite(const TCHAR* logstr)
{
	CString LogFileName;
	LogFileName.Format(_T("%s\\Login.log"), GetProgPath().GetString());
	
	
	CFile file;
	if (!file.Open(LogFileName, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite))
		return;

	file.SeekToEnd();

#if defined(_UNICODE)
	const std::string utf8 = WideToUtf8(logstr, wcslen(logstr));
#if defined(_DEBUG)
	std::cout << "using query: " << utf8 << '\n';
#endif
	file.Write(utf8.c_str(), static_cast<int>(utf8.size()));
#else
#if defined(_DEBUG)
	std::cout << "using query: " << logstr << '\n';
#endif
	file.Write(logstr, strlen(logstr));
#endif

	file.Close();
}

inline void LogFileWrite(const std::string& logStr)
{
	CString clog = logStr.c_str();
	LogFileWrite(clog);
}

inline int DisplayErrorMsg(SQLHANDLE hstmt)
{
	SQLTCHAR      SqlState[6], Msg[1024];
	SQLINTEGER    NativeError;
	SQLSMALLINT   i, MsgLen;
	SQLRETURN     rc2;
	TCHAR		  logstr[512] = {};

	i = 1;
	while ((rc2 = SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, i, SqlState, &NativeError, Msg, _countof(Msg), &MsgLen)) != SQL_NO_DATA)
	{
		_sntprintf(logstr, _countof(logstr) - 1, _T("*** %s, %d, %s, %d ***\r\n"), SqlState, NativeError, Msg, MsgLen);
		LogFileWrite(logstr);

		i++;
	}

	if (_tcscmp((TCHAR*) SqlState, _T("08S01")) == 0)
		return -1;

	return 0;
}

// ini config variable names
namespace ini
{
	// ODBC Config Section
	static constexpr char ODBC[] = "ODBC";
	static constexpr char DSN[] = "DSN";
	static constexpr char UID[] = "UID";
	static constexpr char PWD[] = "PWD";
	static constexpr char TABLE[] = "TABLE";

	// CONFIGURATION section
	static constexpr char CONFIGURATION[] = "CONFIGURATION";
	static constexpr char DEFAULT_PATH[] = "DEFAULT_PATH";

	// SERVER_LIST section
	static constexpr char SERVER_LIST[] = "SERVER_LIST";
	static constexpr char COUNT[] = "COUNT";
}

#endif
