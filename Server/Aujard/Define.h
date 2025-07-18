﻿#ifndef _DEFINE_H
#define _DEFINE_H

#if defined(_DEBUG)
#include <iostream>
#endif

#include <shared/globals.h>
#include <shared/StringConversion.h>
#include <shared/_USER_DATA.h>

constexpr int MAX_USER			= 3000;

/// \brief AUTOSAVE_DELTA is the amount of time required since last save to trigger a UserDataSave()
constexpr int AUTOSAVE_DELTA = 360000;

constexpr long DB_PROCESS_TIMEOUT = 10;

#define MAX_ITEM			28

////////////////////////////////////////////////////////////
// Socket Define
////////////////////////////////////////////////////////////
#define SOCKET_BUFF_SIZE	(1024*8)
#define MAX_PACKET_SIZE		(1024*2)

#define PACKET_START1				0XAA
#define PACKET_START2				0X55
#define PACKET_END1					0X55
#define PACKET_END2					0XAA
//#define PROTOCOL_VER				0X01

// status
#define STATE_CONNECTED			0X01
#define STATE_DISCONNECTED		0X02
#define STATE_GAMESTART			0x03

/////////////////////////////////////////////////////
// ITEM_SLOT DEFINE
#define RIGHTEAR			0
#define HEAD				1
#define LEFTEAR				2
#define NECK				3
#define BREAST				4
#define SHOULDER			5
#define RIGHTHAND			6
#define WAIST				7
#define LEFTHAND			8
#define RIGHTRING			9
#define LEG					10
#define LEFTRING			11
#define GLOVE				12
#define FOOT				13
/////////////////////////////////////////////////////

////////////////////////////////////////////////////////////

typedef union {
	short int	i;
	BYTE		b[2];
} MYSHORT;

typedef union {
	int			i;
	BYTE		b[4];
} MYINT;

typedef union {
	DWORD		w;
	BYTE		b[4];
} MYDWORD;

// DEFINE Shared Memory Queue Flag

#define E	0x00
#define R	0x01
#define W	0x02
#define WR	0x03

// DEFINE Shared Memory Queue Return VALUE

#define SMQ_BROKEN		10000
#define SMQ_FULL		10001
#define SMQ_EMPTY		10002
#define SMQ_PKTSIZEOVER	10003
#define SMQ_WRITING		10004
#define SMQ_READING		10005

// DEFINE Shared Memory Costumizing

#define MAX_PKTSIZE		512
#define MAX_COUNT		4096
#define SMQ_LOGGERSEND	"KNIGHT_SEND"
#define SMQ_LOGGERRECV	"KNIGHT_RECV"

// Packet Define...
#define WIZ_LOGIN				0x01	// Account Login
#define WIZ_NEW_CHAR			0x02	// Create Character DB
#define WIZ_DEL_CHAR			0x03	// Delete Character DB
#define WIZ_SEL_CHAR			0x04	// Select Character
#define WIZ_SEL_NATION			0x05	// Select Nation
#define WIZ_ALLCHAR_INFO_REQ	0x0C	// Account All Character Info Request
#define WIZ_LOGOUT				0x0F	// Request Logout
#define WIZ_DATASAVE			0x37	// User GameData DB Save Request
#define WIZ_KNIGHTS_PROCESS		0x3C	// Knights Related Packet..
#define WIZ_CLAN_PROCESS		0x4E	// Clans Related Packet..
#define WIZ_LOGIN_INFO			0x50	// define for _dbAgent Communication
#define WIZ_KICKOUT				0x51	// Account ID forbid duplicate connection
#define WIZ_BATTLE_EVENT		0x57	// Battle Event Result

#define DB_COUPON_EVENT			0x10	// coupon event
		#define CHECK_COUPON_EVENT		0x01
		#define UPDATE_COUPON_EVENT		0x02

////////////////////////////////////////////////////////////////
// Knights Packet sub define 
////////////////////////////////////////////////////////////////
#define KNIGHTS_CREATE			0x11		// 생성
#define KNIGHTS_JOIN			0x12		// 가입
#define KNIGHTS_WITHDRAW		0x13		// 탈퇴
#define KNIGHTS_REMOVE			0x14		// 멤버 삭제
#define KNIGHTS_DESTROY			0x15		// 뽀개기
#define KNIGHTS_ADMIT			0x16		// 멤버 가입 허가
#define KNIGHTS_REJECT			0x17		// 멤버 가입 거절
#define KNIGHTS_PUNISH			0x18		// 멤버 징계
#define KNIGHTS_CHIEF			0x19		// 단장 임명
#define KNIGHTS_VICECHIEF		0x1A		// 부단장 임명
#define KNIGHTS_OFFICER			0x1B		// 장교임명
#define KNIGHTS_ALLLIST_REQ		0x1C		// 리스트를 10개 단위로 Page 요청
#define KNIGHTS_MEMBER_REQ		0x1D		// 모든 멤버 요청
#define KNIGHTS_CURRENT_REQ		0x1E		// 현재 접속 리스트
#define KNIGHTS_STASH			0x1F		// 기사단 창고
#define KNIGHTS_MODIFY_FAME		0x20		// 멤버의 직위 변경.. 해당 멤버에게 간다
#define KNIGHTS_JOIN_REQ		0x21		// 해당멤버에게 가입요청을 한다
#define KNIGHTS_LIST_REQ		0x22		// 기사단 리스트를  요청 ( index 검색 )

////////////////////////////////////////////////////////////////
// Clan Packet sub define
////////////////////////////////////////////////////////////////
#define CLAN_CREATE				0x01
#define CLAN_JOIN				0x02

////////////////////////////////////////////////////////////////
// Update User Data type define
////////////////////////////////////////////////////////////////
#define UPDATE_LOGOUT			0x01
#define UPDATE_ALL_SAVE			0x02
#define UPDATE_PACKET_SAVE		0x03

////////////////////////////////////////////////////////////////
// WIZ_NEW_CHAR Results
////////////////////////////////////////////////////////////////
#define NEW_CHAR_ERROR			-1
#define NEW_CHAR_SUCCESS		0
#define NEW_CHAR_NO_FREE_SLOT	1
#define NEW_CHAR_INVALID_RACE	2
#define NEW_CHAR_NAME_IN_USE	3
#define NEW_CHAR_SYNC_ERROR		4


// Reply packet define...

#define SEND_ME					0x01
#define SEND_REGION				0x02
#define SEND_ALL				0x03

#define ITEMCOUNT_MAX		9999	// 소모 아이템 소유 한계값

/////////////////////////////////////////////////////////////////////////////////
// Structure Define
/////////////////////////////////////////////////////////////////////////////////

import AujardModel;
namespace model = aujard_model;

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
//
//	Global Function Define
//

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

inline int64_t GetInt64(char* sBuf, int& index)
{
	index += 8;
	return *(int64_t*) (sBuf + index - 8);
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

inline void SetInt64(char* tBuf, int64_t nInt64, int& index)
{
	CopyMemory(tBuf + index, &nInt64, 8);
	index += 8;
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

inline void SetVarString(char* tBuf, TCHAR* sBuf, int len, int& index)
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

inline void LogFileWrite(LPCTSTR logstr)
{
	CString LogFileName;
	LogFileName.Format(_T("%s\\Aujard.log"), GetProgPath().GetString());

	CFile file;
	if (!file.Open(LogFileName, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite))
		return;

	file.SeekToEnd();

#if defined(_UNICODE)
	const std::string utf8 = WideToUtf8(logstr, wcslen(logstr));
	file.Write(utf8.c_str(), static_cast<int>(utf8.size()));
#if defined(_DEBUG)
	std::cout << "LogFileWrite: " << utf8 << std::endl;
#endif
#else
	file.Write(logstr, strlen(logstr));
#if defined(_DEBUG)
	std::cout << "LogFileWrite: " << logstr << std::endl;
#endif
#endif

	file.Close();
}

inline void LogFileWrite(const std::string& logStr)
{
	CString clog(CA2T(logStr.c_str()));
	LogFileWrite(clog);
}

inline int DisplayErrorMsg(SQLHANDLE hstmt)
{
	SQLTCHAR      SqlState[6], Msg[4096];
	SQLINTEGER    NativeError;
	SQLSMALLINT   i, MsgLen;
	SQLRETURN     rc2;
	TCHAR		  logstr[4096] = {};

	i = 1;
	while ((rc2 = SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, i, SqlState, &NativeError, Msg, _countof(Msg), &MsgLen)) != SQL_NO_DATA)
	{
		_stprintf(logstr, _T("*** %s, %d, %s, %d ***\r\n"), SqlState, NativeError, Msg, MsgLen);
		LogFileWrite(logstr);

		i++;
	}

	if (_tcscmp((TCHAR*) SqlState, _T("08S01")) == 0)
		return -1;
	else
		return 0;
}

namespace ini
{
	// ODBC Config Section
	static constexpr char ODBC[] = "ODBC";
	static constexpr char GAME_DSN[] = "GAME_DSN";
	static constexpr char GAME_UID[] = "GAME_UID";
	static constexpr char GAME_PWD[] = "GAME_PWD";
	static constexpr char ACCOUNT_DSN[] = "ACCOUNT_DSN";
	static constexpr char ACCOUNT_UID[] = "ACCOUNT_UID";
	static constexpr char ACCOUNT_PWD[] = "ACCOUNT_PWD";

	// ZONE_INFO section
	static constexpr char ZONE_INFO[] = "ZONE_INFO";
	static constexpr char GROUP_INFO[] = "GROUP_INFO";
}

#endif
