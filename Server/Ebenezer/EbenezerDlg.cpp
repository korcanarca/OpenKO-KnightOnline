﻿// EbenezerDlg.cpp : implementation file
//

#include "stdafx.h"
#include "EbenezerDlg.h"
#include "User.h"

#include "ZoneInfoSet.h"
#include "LevelUpTableSet.h"
#include "KnightsSet.h"
#include "KnightsUserSet.h"
#include "KnightsRankSet.h"
#include "BattleSet.h"

#include <shared/crc32.h>
#include <shared/lzf.h>
#include <shared/packets.h>

#include <db-library/DatabaseConnManager.h>
#include <db-library/RecordSetLoader_STLMap.h>

constexpr int GAME_TIME       	= 100;
constexpr int SEND_TIME			= 200;
constexpr int PACKET_CHECK		= 300;
constexpr int ALIVE_TIME		= 400;
constexpr int MARKET_BBS_TIME	= 1000;

constexpr int NUM_FLAG_VICTORY    = 4;
constexpr int AWARD_GOLD          = 5000;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

import EbenezerBinder;

using namespace db;

CRITICAL_SECTION g_serial_critical;
CRITICAL_SECTION g_region_critical;
CRITICAL_SECTION g_LogFile_critical;

CEbenezerDlg* CEbenezerDlg::s_pInstance = nullptr;
CIOCPort CEbenezerDlg::m_Iocport;

WORD g_increase_serial = 1;
BYTE g_serverdown_flag = FALSE;

DWORD WINAPI ReadQueueThread(LPVOID lp)
{
	CEbenezerDlg* pMain = (CEbenezerDlg*) lp;
	int recvlen = 0, index = 0, uid = -1, send_index = 0, buff_length = 0;
	BYTE command, result;
	char pBuf[1024] = {}, send_buff[1024] = {};
	CUser* pUser = nullptr;
	int currenttime = 0;

	while (TRUE)
	{
		if (pMain->m_LoggerRecvQueue.GetFrontMode() == R)
			continue;

		index = 0;
		recvlen = pMain->m_LoggerRecvQueue.GetData(pBuf);

		if (recvlen > MAX_PKTSIZE
			|| recvlen == 0)
		{
			Sleep(1);
			continue;
		}

		command = GetByte(pBuf, index);
		uid = GetShort(pBuf, index);

		if (command == (KNIGHTS_ALLLIST_REQ + 0x10)
			&& uid == -1)
		{
			pMain->m_KnightsManager.RecvKnightsAllList(pBuf + index);
			continue;
		}

		if (uid < 0
			|| uid >= MAX_USER)
			goto loop_pass;

		pUser = (CUser*) pMain->m_Iocport.m_SockArray[uid];
		if (pUser == nullptr)
			goto loop_pass;

		switch (command)
		{
			case WIZ_LOGIN:
				result = GetByte(pBuf, index);
				if (result == 0xFF)
					memset(pUser->m_strAccountID, 0, sizeof(pUser->m_strAccountID));
				SetByte(send_buff, WIZ_LOGIN, send_index);
				SetByte(send_buff, result, send_index);					// 성공시 국가 정보
				pUser->Send(send_buff, send_index);
				break;

			case WIZ_SEL_NATION:
				SetByte(send_buff, WIZ_SEL_NATION, send_index);
				SetByte(send_buff, GetByte(pBuf, index), send_index);	// 국가 정보
				pUser->Send(send_buff, send_index);
				break;

			case WIZ_NEW_CHAR:
				result = GetByte(pBuf, index);
				SetByte(send_buff, WIZ_NEW_CHAR, send_index);
				SetByte(send_buff, result, send_index);					// 성공시 국가 정보
				pUser->Send(send_buff, send_index);
				break;

			case WIZ_DEL_CHAR:
				pUser->RecvDeleteChar(pBuf + index);
			/*	result = GetByte( pBuf, index );
				SetByte( send_buff, WIZ_DEL_CHAR, send_index );
				SetByte( send_buff, result, send_index );					// 성공시 국가 정보
				SetByte( send_buff, GetByte( pBuf, index ), send_index );
				pUser->Send( send_buff, send_index );	*/
				break;

			case WIZ_SEL_CHAR:
				pUser->SelectCharacter(pBuf + index);
				break;

			case WIZ_ALLCHAR_INFO_REQ:
				buff_length = GetShort(pBuf, index);
				if (buff_length > recvlen)
					break;

				SetByte(send_buff, WIZ_ALLCHAR_INFO_REQ, send_index);
				SetString(send_buff, pBuf + index, buff_length, send_index);
				pUser->Send(send_buff, send_index);
				break;

			case WIZ_LOGOUT:
				if (pUser != nullptr
					&& strlen(pUser->m_pUserData->m_id) != 0)
				{
					TRACE(_T("Logout Strange...%hs\n"), pUser->m_pUserData->m_id);
					pUser->Close();
				}
				break;

			case KNIGHTS_CREATE + 0x10:
			case KNIGHTS_JOIN + 0x10:
			case KNIGHTS_WITHDRAW + 0x10:
			case KNIGHTS_REMOVE + 0x10:
			case KNIGHTS_ADMIT + 0x10:
			case KNIGHTS_REJECT + 0x10:
			case KNIGHTS_CHIEF + 0x10:
			case KNIGHTS_VICECHIEF + 0x10:
			case KNIGHTS_OFFICER + 0x10:
			case KNIGHTS_PUNISH + 0x10:
			case KNIGHTS_DESTROY + 0x10:
			case KNIGHTS_MEMBER_REQ + 0x10:
			case KNIGHTS_STASH + 0x10:
			case KNIGHTS_LIST_REQ + 0x10:
			case KNIGHTS_ALLLIST_REQ + 0x10:
				pMain->m_KnightsManager.ReceiveKnightsProcess(pUser, pBuf + index, command);
				break;

			case WIZ_LOGIN_INFO:
				result = GetByte(pBuf, index);
				if (result == 0x00)
					pUser->Close();
				break;

			case DB_COUPON_EVENT:
				if (pUser != nullptr)
					pUser->CouponEvent(pBuf + index);
				break;
		}

	loop_pass:
		recvlen = 0;
		memset(pBuf, 0, sizeof(pBuf));
		send_index = 0;
		memset(send_buff, 0, sizeof(send_buff));
	}
}

/////////////////////////////////////////////////////////////////////////////
// CEbenezerDlg dialog

CEbenezerDlg::CEbenezerDlg(CWnd* pParent /*=nullptr*/)
	: CDialog(CEbenezerDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CEbenezerDlg)
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	m_bMMFCreate = FALSE;
	m_hReadQueueThread = nullptr;

	m_nYear = 0;
	m_nMonth = 0;
	m_nDate = 0;
	m_nHour = 0;
	m_nMin = 0;
	m_nWeather = 0;
	m_nAmount = 0;
	m_sPartyIndex = 0;

	m_nCastleCapture = 0;

	m_bKarusFlag = 0;
	m_bElmoradFlag = 0;

	m_byKarusOpenFlag = m_byElmoradOpenFlag = 0;
	m_byBanishFlag = 0;
	m_sBanishDelay = 0;

	m_sKarusDead = 0;
	m_sElmoradDead = 0;

	m_bVictory = 0;
	m_byOldVictory = 0;
	m_bBanishDelayStart = 0;
	m_byBattleSave = 0;
	m_sKarusCount = 0;
	m_sElmoradCount = 0;

	m_nBattleZoneOpenWeek = m_nBattleZoneOpenHourStart = m_nBattleZoneOpenHourEnd = 0;

	m_byBattleOpen = NO_BATTLE;
	m_byOldBattleOpen = NO_BATTLE;
	m_bFirstServerFlag = FALSE;
	m_bPointCheckFlag = FALSE;

	m_nServerNo = 0;
	m_nServerGroupNo = 0;
	m_nServerGroup = 0;
	m_iPacketCount = 0;
	m_iSendPacketCount = 0;
	m_iRecvPacketCount = 0;
	m_sDiscount = 0;

	m_pUdpSocket = nullptr;

	for (int h = 0; h < MAX_BBS_POST; h++)
	{
		m_sBuyID[h] = -1;
		memset(m_strBuyTitle[h], 0, sizeof(m_strBuyTitle[h]));
		memset(m_strBuyMessage[h], 0, sizeof(m_strBuyMessage[h]));
		m_iBuyPrice[h] = 0;
		m_fBuyStartTime[h] = 0.0f;

		m_sSellID[h] = -1;
		memset(m_strSellTitle[h], 0, sizeof(m_strSellTitle[h]));
		memset(m_strSellMessage[h], 0, sizeof(m_strSellMessage[h]));
		m_iSellPrice[h] = 0;
		m_fSellStartTime[h] = 0.0f;
	}

	memset(m_ppNotice, 0, sizeof(m_ppNotice));
	memset(m_AIServerIP, 0, sizeof(m_AIServerIP));

	m_bPermanentChatMode = FALSE;			// 비러머글 남는 공지 --;
	m_bPermanentChatFlag = FALSE;
	memset(m_strPermanentChat, 0, sizeof(m_strPermanentChat));

	memset(m_strKarusCaptain, 0, sizeof(m_strKarusCaptain));
	memset(m_strElmoradCaptain, 0, sizeof(m_strElmoradCaptain));

	memset(m_strGameDSN, 0, sizeof(m_strGameDSN));
	memset(m_strGameUID, 0, sizeof(m_strGameUID));
	memset(m_strGamePWD, 0, sizeof(m_strGamePWD));

	m_bSanta = FALSE;		// 갓댐 산타!!! >.<

	DatabaseConnManager::Create();
}

CEbenezerDlg::~CEbenezerDlg()
{
	DatabaseConnManager::Destroy();
}

void CEbenezerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CEbenezerDlg)
	DDX_Control(pDX, IDC_GONGJI_EDIT, m_AnnounceEdit);
	DDX_Control(pDX, IDC_LIST1, m_StatusList);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CEbenezerDlg, CDialog)
	//{{AFX_MSG_MAP(CEbenezerDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_TIMER()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CEbenezerDlg message handlers

BOOL CEbenezerDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	s_pInstance = this;

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	srand(time(nullptr));

	// Compress Init
	memset(m_CompBuf, 0, sizeof(m_CompBuf));	// 압축할 데이터를 모으는 버퍼
	m_iCompIndex = 0;							// 압축할 데이터의 길이
	m_CompCount = 0;							// 압축할 데이터의 개수

	m_sZoneCount = 0;
	m_sSocketCount = 0;
	m_sErrorSocketCount = 0;
	m_KnightsManager.m_pMain = this;
	// sungyong 2002.05.23
	m_sSendSocket = 0;
	m_bFirstServerFlag = FALSE;
	m_bServerCheckFlag = FALSE;
	m_sReSocketCount = 0;
	m_fReConnectStart = 0.0f;
	// sungyong~ 2002.05.23

	//----------------------------------------------------------------------
	//	Logfile initialize
	//----------------------------------------------------------------------
	CTime cur = CTime::GetCurrentTime();
	TCHAR strLogFile[50] = {};
	wsprintf(strLogFile, _T("RegionLog-%d-%d-%d.txt"), cur.GetYear(), cur.GetMonth(), cur.GetDay());
	m_RegionLogFile.Open(strLogFile, CFile::modeWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone);
	m_RegionLogFile.SeekToEnd();

	wsprintf(strLogFile, _T("PacketLog-%d-%d-%d.txt"), cur.GetYear(), cur.GetMonth(), cur.GetDay());
	m_LogFile.Open(strLogFile, CFile::modeWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone);
	m_LogFile.SeekToEnd();

	InitializeCriticalSection(&g_LogFile_critical);
	InitializeCriticalSection(&g_serial_critical);
	InitializeCriticalSection(&g_region_critical);

	GetTimeFromIni();

	m_Iocport.Init(MAX_USER, CLIENT_SOCKSIZE, 4);

	for (int i = 0; i < MAX_USER; i++)
		m_Iocport.m_SockArrayInActive[i] = new CUser();

	_ZONE_SERVERINFO* pInfo = m_ServerArray.GetData(m_nServerNo);
	if (pInfo == nullptr)
	{
		AfxMessageBox(_T("No Listen Port!!"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!m_Iocport.Listen(pInfo->sPort))
		AfxMessageBox(_T("FAIL TO CREATE LISTEN STATE"), MB_OK);

	if (!InitializeMMF())
	{
		AfxMessageBox(_T("Main Shared Memory Initialize Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!m_LoggerSendQueue.InitailizeMMF(MAX_PKTSIZE, MAX_COUNT, _T(SMQ_LOGGERSEND)))
	{
		AfxMessageBox(_T("SMQ Send Shared Memory Initialize Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!m_LoggerRecvQueue.InitailizeMMF(MAX_PKTSIZE, MAX_COUNT, _T(SMQ_LOGGERRECV)))
	{
		AfxMessageBox(_T("SMQ Recv Shared Memory Initialize Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!m_ItemLoggerSendQ.InitailizeMMF(MAX_PKTSIZE, MAX_COUNT, _T(SMQ_ITEMLOGGER)))
	{
		AfxMessageBox(_T("SMQ ItemLog Shared Memory Initialize Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before item\r\n"));
	if (!LoadItemTable())
	{
		AfxMessageBox(_T("ItemTable Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before main\r\n"));
	if (!LoadMagicTable())
	{
		AfxMessageBox(_T("MagicTable Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before 1\r\n"));
	if (!LoadMagicType1())
	{
		AfxMessageBox(_T("MagicType1 Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before 2\r\n"));
	if (!LoadMagicType2())
	{
		AfxMessageBox(_T("MagicType2 Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before 3\r\n"));
	if (!LoadMagicType3())
	{
		AfxMessageBox(_T("MagicType3 Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before 4\r\n"));
	if (!LoadMagicType4())
	{
		AfxMessageBox(_T("MagicType4 Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before 5\r\n"));
	if (!LoadMagicType5())
	{
		AfxMessageBox(_T("MagicType5 Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before 8\r\n"));
	if (!LoadMagicType8())
	{
		AfxMessageBox(_T("MagicType8 Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before Coeffi\r\n"));
	if (!LoadCoefficientTable())
	{
		AfxMessageBox(_T("CharaterDataTable Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before Level\r\n"));
	if (!LoadLevelUpTable())
	{
		AfxMessageBox(_T("LevelUpTable Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before All Kinghts\r\n"));
	if (!LoadAllKnights())
	{
		AfxMessageBox(_T("KnightsData Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before All Knights User\r\n"));
	if (!LoadAllKnightsUserData())
	{
		AfxMessageBox(_T("LoadAllKnightsUserData Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before home\r\n"));
	if (!LoadHomeTable())
	{
		AfxMessageBox(_T("LoadHomeTable Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before battle\r\n"));
	if (!LoadBattleTable())
	{
		AfxMessageBox(_T("LoadBattleTable Load Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	LogFileWrite(_T("before map file\r\n"));
	if (!MapFileLoad())
		AfxPostQuitMessage(0);

	LogFileWrite(_T("after map file\r\n"));

	LoadNoticeData();

	DWORD id;
	m_hReadQueueThread = ::CreateThread(nullptr, 0, ReadQueueThread, this, 0, &id);

	m_pUdpSocket = new CUdpSocket(this);
	if (!m_pUdpSocket->CreateSocket())
	{
		AfxMessageBox(_T("Udp Socket Create Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!AIServerConnect())
	{
#ifndef _DEBUG
		AfxPostQuitMessage(0);
#endif
	}

	LogFileWrite(_T("success\r\n"));
	UserAcceptThread();

	//CTime cur = CTime::GetCurrentTime();
	CString starttime;
	starttime.Format(
		_T("Game Server Start : %d월 %d일 %d시 %d분\r\n"), cur.GetMonth(), cur.GetDay(), cur.GetHour(), cur.GetMinute());
	LogFileWrite(starttime);
	m_StatusList.AddString(starttime);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CEbenezerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	CDialog::OnSysCommand(nID, lParam);
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CEbenezerDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CEbenezerDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

BOOL CEbenezerDlg::DestroyWindow()
{
	KillTimer(GAME_TIME);
	KillTimer(SEND_TIME);
	KillTimer(ALIVE_TIME);
	KillTimer(MARKET_BBS_TIME);
	KillTimer(PACKET_CHECK);

	if (m_hReadQueueThread != nullptr)
		::TerminateThread(m_hReadQueueThread, 0);

	if (m_bMMFCreate)
	{
		UnmapViewOfFile(m_lpMMFile);
		CloseHandle(m_hMMFile);
	}

	if (m_RegionLogFile.m_hFile != CFile::hFileNull)
		m_RegionLogFile.Close();

	if (m_LogFile.m_hFile != CFile::hFileNull)
		m_LogFile.Close();

	if (!m_ItemtableArray.IsEmpty())
		m_ItemtableArray.DeleteAllData();

	if (!m_MagictableArray.IsEmpty())
		m_MagictableArray.DeleteAllData();

	if (!m_Magictype1Array.IsEmpty())
		m_Magictype1Array.DeleteAllData();

	if (!m_Magictype2Array.IsEmpty())
		m_Magictype2Array.DeleteAllData();

	if (!m_Magictype3Array.IsEmpty())
		m_Magictype3Array.DeleteAllData();

	if (!m_Magictype4Array.IsEmpty())
		m_Magictype4Array.DeleteAllData();

	if (!m_Magictype5Array.IsEmpty())
		m_Magictype5Array.DeleteAllData();

	if (!m_Magictype8Array.IsEmpty())
		m_Magictype8Array.DeleteAllData();

	if (!m_arNpcArray.IsEmpty())
		m_arNpcArray.DeleteAllData();

	if (!m_AISocketArray.IsEmpty())
		m_AISocketArray.DeleteAllData();

	if (!m_PartyArray.IsEmpty())
		m_PartyArray.DeleteAllData();

	if (!m_CoefficientArray.IsEmpty())
		m_CoefficientArray.DeleteAllData();

	if (!m_KnightsArray.IsEmpty())
		m_KnightsArray.DeleteAllData();

	if (!m_ServerArray.IsEmpty())
		m_ServerArray.DeleteAllData();

	if (!m_ServerGroupArray.IsEmpty())
		m_ServerGroupArray.DeleteAllData();

	if (!m_HomeArray.IsEmpty())
		m_HomeArray.DeleteAllData();

	for (C3DMap* pMap : m_ZoneArray)
		delete pMap;
	m_ZoneArray.clear();

	for (_LEVELUP* pLevelUp : m_LevelUpArray)
		delete pLevelUp;
	m_LevelUpArray.clear();

	if (!m_Event.IsEmpty())
		m_Event.DeleteAllData();

	delete m_pUdpSocket;
	m_pUdpSocket = nullptr;

	s_pInstance = nullptr;

	DeleteCriticalSection(&g_LogFile_critical);
	DeleteCriticalSection(&g_serial_critical);
	DeleteCriticalSection(&g_region_critical);

	return CDialog::DestroyWindow();
}

void CEbenezerDlg::UserAcceptThread()
{
	// User Socket Accept
	::ResumeThread(m_Iocport.m_hAcceptThread);
}

CUser* CEbenezerDlg::GetUserPtr(const char* userid, NameType type)
{
	// Account id check....
	if (type == NameType::Account)
	{
		for (int i = 0; i < MAX_USER; i++)
		{
			CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
			if (pUser != nullptr
				&& _strnicmp(pUser->m_strAccountID, userid, MAX_ID_SIZE) == 0)
				return pUser;
		}
	}
	// character id check...
	else // if (type == NameType::Character)
	{
		for (int i = 0; i < MAX_USER; i++)
		{
			CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
			if (pUser != nullptr
				&& _strnicmp(pUser->m_pUserData->m_id, userid, MAX_ID_SIZE) == 0)
				return pUser;
		}
	}

	return nullptr;
}

void CEbenezerDlg::OnTimer(UINT nIDEvent)
{
	// sungyong 2002.05.23
	int retval = 0;

	switch (nIDEvent)
	{
		case GAME_TIME:
			UpdateGameTime();

			// AIServer Socket Alive Check Routine
			if (m_bFirstServerFlag)
			{
				int count = 0;
				for (int i = 0; i < MAX_AI_SOCKET; i++)
				{
					CAISocket* pAISock = m_AISocketArray.GetData(i);
					if (pAISock != nullptr
						&& pAISock->GetState() == STATE_DISCONNECTED)
						AISocketConnect(i, 1);
					else if (pAISock == nullptr)
						AISocketConnect(i, 1);
					else
						count++;
				}

				if (count <= 0)
					DeleteAllNpcList();
			}
			// sungyong~ 2002.05.23
			break;

		case SEND_TIME:
			m_Iocport.m_PostOverlapped.Offset = OVL_SEND;
			retval = PostQueuedCompletionStatus(m_Iocport.m_hSendIOCPort, 0, 0, &m_Iocport.m_PostOverlapped);		
			if (!retval)
			{
				int errValue = GetLastError();
				TRACE(_T("Send PostQueued Error : %d\n"), errValue);
			}
			break;

		case ALIVE_TIME:
			CheckAliveUser();
			break;

		case MARKET_BBS_TIME:
			MarketBBSTimeCheck();
			break;

		case PACKET_CHECK:
			WritePacketLog();
			break;
	}

	CDialog::OnTimer(nIDEvent);
}

// sungyong 2002.05.22
BOOL CEbenezerDlg::AIServerConnect()
{
	for (int i = 0; i < MAX_AI_SOCKET; i++)
	{
		if (!AISocketConnect(i))
		{
			AfxMessageBox(_T("AI Server Connect Fail!!"));
			return FALSE;
		}
	}

	return TRUE;
}

BOOL CEbenezerDlg::AISocketConnect(int zone, int flag)
{
	CAISocket* pAISock = nullptr;
	int send_index = 0;
	char pBuf[128] = {};

	//if( m_nServerNo == 3 ) return FALSE;

	pAISock = m_AISocketArray.GetData(zone);
	if (pAISock != nullptr)
	{
		if (pAISock->GetState() != STATE_DISCONNECTED)
			return TRUE;

		m_AISocketArray.DeleteData(zone);
	}

	pAISock = new CAISocket(zone);

	if (!pAISock->Create())
	{
		delete pAISock;
		return FALSE;
	}

	if (m_nServerNo == KARUS)
	{
		if (!pAISock->Connect(&m_Iocport, m_AIServerIP, AI_KARUS_SOCKET_PORT))
		{
			delete pAISock;
			return FALSE;
		}
	}
	else if (m_nServerNo == ELMORAD)
	{
		if (!pAISock->Connect(&m_Iocport, m_AIServerIP, AI_ELMO_SOCKET_PORT))
		{
			delete pAISock;
			return FALSE;
		}
	}
	else if (m_nServerNo == BATTLE)
	{
		if (!pAISock->Connect(&m_Iocport, m_AIServerIP, AI_BATTLE_SOCKET_PORT))
		{
			delete pAISock;
			return FALSE;
		}
	}

	SetByte(pBuf, AI_SERVER_CONNECT, send_index);
	SetByte(pBuf, zone, send_index);

	// 재접속
	if (flag == 1)
		SetByte(pBuf, 1, send_index);
	// 처음 접속..
	else
		SetByte(pBuf, 0, send_index);

	pAISock->Send(pBuf, send_index);

	// 해야할일 :이 부분 처리.....
	//SendAllUserInfo();
	//m_sSocketCount = zone;
	m_AISocketArray.PutData(zone, pAISock);

	TRACE(_T("**** AISocket Connect Success!! ,, zone = %d ****\n"), zone);
	return TRUE;
}
// ~sungyong 2002.05.22

void CEbenezerDlg::Send_All(char* pBuf, int len, CUser* pExceptUser, int nation)
{
	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		if (pUser == pExceptUser)
			continue;

		if (pUser->GetState() == STATE_GAMESTART)
		{
			if (nation == 0)
				pUser->Send(pBuf, len);
			else if (nation == pUser->m_pUserData->m_bNation)
				pUser->Send(pBuf, len);
		}
	}
}

void CEbenezerDlg::Send_Region(char* pBuf, int len, int zone, int x, int z, CUser* pExceptUser, bool bDirect)
{
	C3DMap* pMap = GetMapByID(zone);
	if (pMap == nullptr)
		return;

	Send_UnitRegion(pMap, pBuf, len, x, z, pExceptUser, bDirect);
	Send_UnitRegion(pMap, pBuf, len, x - 1, z - 1, pExceptUser, bDirect);	// NW
	Send_UnitRegion(pMap, pBuf, len, x, z - 1, pExceptUser, bDirect);		// N
	Send_UnitRegion(pMap, pBuf, len, x + 1, z - 1, pExceptUser, bDirect);	// NE
	Send_UnitRegion(pMap, pBuf, len, x - 1, z, pExceptUser, bDirect);		// W
	Send_UnitRegion(pMap, pBuf, len, x + 1, z, pExceptUser, bDirect);		// E
	Send_UnitRegion(pMap, pBuf, len, x - 1, z + 1, pExceptUser, bDirect);	// SW
	Send_UnitRegion(pMap, pBuf, len, x, z + 1, pExceptUser, bDirect);		// S
	Send_UnitRegion(pMap, pBuf, len, x + 1, z + 1, pExceptUser, bDirect);	// SE
}

void CEbenezerDlg::Send_UnitRegion(C3DMap* pMap, char* pBuf, int len, int x, int z, CUser* pExceptUser, bool bDirect)
{
	if (pMap == nullptr)
		return;

	if (x < 0
		|| z < 0
		|| x > pMap->GetXRegionMax()
		|| z > pMap->GetZRegionMax())
		return;

	EnterCriticalSection(&g_region_critical);

	for (const auto& [_, pUid] : pMap->m_ppRegion[x][z].m_RegionUserArray)
	{
		int uid = *pUid;
		if (uid < 0)
			continue;

		CUser* pUser = (CUser*) m_Iocport.m_SockArray[uid];
		if (pUser == pExceptUser)
			continue;

		if (pUser != nullptr
			&& (pUser->GetState() == STATE_GAMESTART))
		{
			if (bDirect)
				pUser->Send(pBuf, len);
			else
				pUser->RegionPacketAdd(pBuf, len);
		}
	}

	LeaveCriticalSection(&g_region_critical);
}

void CEbenezerDlg::Send_NearRegion(char* pBuf, int len, int zone, int region_x, int region_z, float curx, float curz, CUser* pExceptUser)
{
	C3DMap* pMap = GetMapByID(zone);
	if (pMap == nullptr)
		return;

	int left_border = region_x * VIEW_DISTANCE, top_border = region_z * VIEW_DISTANCE;

	Send_FilterUnitRegion(pMap, pBuf, len, region_x, region_z, curx, curz, pExceptUser);

	// RIGHT
	if (((curx - left_border) > (VIEW_DISTANCE / 2.0f)))
	{
		// BOTTOM
		if (((curz - top_border) > (VIEW_DISTANCE / 2.0f)))
		{
			Send_FilterUnitRegion(pMap, pBuf, len, region_x + 1, region_z, curx, curz, pExceptUser);
			Send_FilterUnitRegion(pMap, pBuf, len, region_x, region_z + 1, curx, curz, pExceptUser);
			Send_FilterUnitRegion(pMap, pBuf, len, region_x + 1, region_z + 1, curx, curz, pExceptUser);
		}
		// TOP
		else
		{
			Send_FilterUnitRegion(pMap, pBuf, len, region_x + 1, region_z, curx, curz, pExceptUser);
			Send_FilterUnitRegion(pMap, pBuf, len, region_x, region_z - 1, curx, curz, pExceptUser);
			Send_FilterUnitRegion(pMap, pBuf, len, region_x + 1, region_z - 1, curx, curz, pExceptUser);
		}
	}
	// LEFT
	else
	{
		// BOTTOM
		if (((curz - top_border) > (VIEW_DISTANCE / 2.0f)))
		{
			Send_FilterUnitRegion(pMap, pBuf, len, region_x - 1, region_z, curx, curz, pExceptUser);
			Send_FilterUnitRegion(pMap, pBuf, len, region_x, region_z + 1, curx, curz, pExceptUser);
			Send_FilterUnitRegion(pMap, pBuf, len, region_x - 1, region_z + 1, curx, curz, pExceptUser);
		}
		// TOP
		else
		{
			Send_FilterUnitRegion(pMap, pBuf, len, region_x - 1, region_z, curx, curz, pExceptUser);
			Send_FilterUnitRegion(pMap, pBuf, len, region_x, region_z - 1, curx, curz, pExceptUser);
			Send_FilterUnitRegion(pMap, pBuf, len, region_x - 1, region_z - 1, curx, curz, pExceptUser);
		}
	}
}

void CEbenezerDlg::Send_FilterUnitRegion(C3DMap* pMap, char* pBuf, int len, int x, int z, float ref_x, float ref_z, CUser* pExceptUser)
{
	if (pMap == nullptr)
		return;

	if (x < 0
		|| z < 0
		|| x > pMap->GetXRegionMax()
		|| z > pMap->GetZRegionMax())
		return;

	EnterCriticalSection(&g_region_critical);

	auto Iter1 = pMap->m_ppRegion[x][z].m_RegionUserArray.begin();
	auto Iter2 = pMap->m_ppRegion[x][z].m_RegionUserArray.end();

	for (const auto& [_, pUid] : pMap->m_ppRegion[x][z].m_RegionUserArray)
	{
		int uid = *pUid;
		if (uid < 0)
			continue;

		CUser* pUser = (CUser*) m_Iocport.m_SockArray[uid];
		if (pUser == pExceptUser)
			continue;

		if (pUser != nullptr
			&& pUser->GetState() == STATE_GAMESTART)
		{
			double fDist = sqrt(
				pow((pUser->m_pUserData->m_curx - ref_x), 2)
				+ pow((pUser->m_pUserData->m_curz - ref_z), 2));
			if (fDist < 32)
				pUser->RegionPacketAdd(pBuf, len);
		}
	}

	LeaveCriticalSection(&g_region_critical);
}

void CEbenezerDlg::Send_PartyMember(int party, char* pBuf, int len)
{
	if (party < 0)
		return;

	_PARTY_GROUP* pParty = m_PartyArray.GetData(party);
	if (pParty == nullptr)
		return;

	for (int i = 0; i < 8; i++)
	{
		if (pParty->uid[i] == -1
			|| pParty->uid[i] >= MAX_USER)
			continue;

		CUser* pUser = (CUser*) m_Iocport.m_SockArray[pParty->uid[i]];
		if (pUser != nullptr)
			pUser->Send(pBuf, len);
	}
}

void CEbenezerDlg::Send_KnightsMember(int index, char* pBuf, int len, int zone)
{
	if (index <= 0)
		return;

	CKnights* pKnights = m_KnightsArray.GetData(index);
	if (pKnights == nullptr)
		return;

	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		if (pUser->m_pUserData->m_bKnights != index)
			continue;

		if (zone != 100
			&& pUser->m_pUserData->m_bZone != zone)
			continue;

		pUser->Send(pBuf, len);
	}
}

// sungyong 2002.05.22
void CEbenezerDlg::Send_AIServer(int zone, char* pBuf, int len)
{
	for (int i = 0; i < MAX_AI_SOCKET; i++)
	{
		CAISocket* pSocket = m_AISocketArray.GetData(i);
		if (pSocket == nullptr)
		{
			m_sSendSocket++;
			if (m_sSendSocket >= MAX_AI_SOCKET)
				m_sSendSocket = 0;

			continue;
		}

		if (i == m_sSendSocket)
		{
			int send_size = pSocket->Send(pBuf, len);
			int old_send_socket = m_sSendSocket;
			m_sSendSocket++;
			if (m_sSendSocket >= MAX_AI_SOCKET)
				m_sSendSocket = 0;
			if (send_size == 0)
				continue;

			//TRACE(_T(" <--- Send_AIServer : length = %d, socket = %d \n"), send_size, old_send_socket);
			return;
		}
	}
}
// ~sungyong 2002.05.22

BOOL CEbenezerDlg::InitializeMMF()
{
	BOOL bCreate = TRUE;
	CString logstr;

	DWORD filesize = MAX_USER * ALLOCATED_USER_DATA_BLOCK;
	m_hMMFile = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, filesize, _T("KNIGHT_DB"));

	if (m_hMMFile != nullptr
		&& GetLastError() == ERROR_ALREADY_EXISTS)
	{
		m_hMMFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, TRUE, _T("KNIGHT_DB"));
		if (m_hMMFile == nullptr)
		{
			logstr = _T("Shared Memory Load Fail!!");
			m_hMMFile = INVALID_HANDLE_VALUE;
			return FALSE;
		}

		bCreate = FALSE;
	}

	logstr = _T("Shared Memory Create Success!!");
	m_StatusList.AddString(logstr);

	m_lpMMFile = (char*) MapViewOfFile(m_hMMFile, FILE_MAP_WRITE, 0, 0, 0);
	if (m_lpMMFile == nullptr)
		return FALSE;

	memset(m_lpMMFile, 0, filesize);

	m_bMMFCreate = bCreate;

	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArrayInActive[i];
		if (pUser != nullptr)
			pUser->m_pUserData = (_USER_DATA*) (m_lpMMFile + i * ALLOCATED_USER_DATA_BLOCK);
	}

	return TRUE;
}

BOOL CEbenezerDlg::MapFileLoad()
{
	CFile file;
	CString szFullPath, errormsg;

	CZoneInfoSet ZoneInfoSet;

	if (!ZoneInfoSet.Open())
	{
		AfxMessageBox(_T("ZoneInfoTable Open Fail!"));
		return FALSE;
	}

	if (ZoneInfoSet.IsBOF()
		|| ZoneInfoSet.IsEOF())
	{
		AfxMessageBox(_T("ZoneInfoTable Empty!"));
		return FALSE;
	}

	m_ZoneArray.reserve(10);

	// Build the base MAP directory
	std::filesystem::path mapDir(GetProgPath().GetString());
	mapDir /= MAP_DIR;

	// Resolve it to strip the relative references to be nice.
	if (std::filesystem::exists(mapDir))
		mapDir = std::filesystem::canonical(mapDir);

	ZoneInfoSet.MoveFirst();

	while (!ZoneInfoSet.IsEOF())
	{
		std::filesystem::path mapPath
			= mapDir / ZoneInfoSet.m_strZoneName.GetString();

		szFullPath.Format(_T("%ls"), mapPath.c_str());

		if (!file.Open(szFullPath, CFile::modeRead))
		{
			errormsg.Format(_T("File Open Fail - %s\n"), szFullPath);
			AfxMessageBox(errormsg);
			return FALSE;
		}

		C3DMap* pMap = new C3DMap;

		pMap->m_nServerNo = ZoneInfoSet.m_ServerNo;
		pMap->m_nZoneNumber = ZoneInfoSet.m_ZoneNo;
		strcpy(pMap->m_MapName, CT2A(ZoneInfoSet.m_strZoneName));
		pMap->m_fInitX = (float) (ZoneInfoSet.m_InitX / 100.0);
		pMap->m_fInitZ = (float) (ZoneInfoSet.m_InitZ / 100.0);
		pMap->m_fInitY = (float) (ZoneInfoSet.m_InitY / 100.0);
		pMap->m_bType = ZoneInfoSet.m_Type;

		if (!pMap->LoadMap(file.m_hFile))
		{
			errormsg.Format(_T("Map Load Fail - %s\n"), szFullPath);
			AfxMessageBox(errormsg);
			delete pMap;
			return FALSE;
		}

		m_ZoneArray.push_back(pMap);

		// 스트립트를 읽어 들인다.
		EVENT* pEvent = new EVENT;
		if (!pEvent->LoadEvent(ZoneInfoSet.m_ZoneNo))
		{
			delete pEvent;
			pEvent = nullptr;
		}

		if (pEvent != nullptr)
		{
			if (!m_Event.PutData(pEvent->m_Zone, pEvent))
			{
				delete pEvent;
				pEvent = nullptr;
			}
		}

		ZoneInfoSet.MoveNext();

		file.Close();
	}

	return TRUE;
}

void CEbenezerDlg::ReportTableLoadError(const recordset_loader::Error& err, const char* source)
{
	CString msg;
	msg.Format(_T("%hs failed: %hs"), source, err.Message.c_str());
	AfxMessageBox(msg);
}

BOOL CEbenezerDlg::LoadItemTable()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_ItemtableArray, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadMagicTable()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_MagictableArray, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadMagicType1()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_Magictype1Array, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadMagicType2()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_Magictype2Array, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadMagicType3()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_Magictype3Array, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;

}

BOOL CEbenezerDlg::LoadMagicType4()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_Magictype4Array, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadMagicType5()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_Magictype5Array, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadMagicType8()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_Magictype8Array, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadCoefficientTable()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_CoefficientArray, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadLevelUpTable()
{
	CLevelUpTableSet LevelUpTableSet;

	if (!LevelUpTableSet.Open())
	{
		AfxMessageBox(_T("LevelUpTable Open Fail!"));
		return FALSE;
	}

	if (LevelUpTableSet.IsBOF()
		|| LevelUpTableSet.IsEOF())
	{
		AfxMessageBox(_T("LevelUpTable Empty!"));
		return FALSE;
	}

	m_LevelUpArray.reserve(MAX_LEVEL);

	LevelUpTableSet.MoveFirst();

	while (!LevelUpTableSet.IsEOF())
	{
		_LEVELUP* pTableLevelUp = new _LEVELUP;

		pTableLevelUp->m_sLevel = LevelUpTableSet.m_level;
		pTableLevelUp->m_iExp = LevelUpTableSet.m_Exp;

		m_LevelUpArray.push_back(pTableLevelUp);

		LevelUpTableSet.MoveNext();
	}

	return TRUE;
}

void CEbenezerDlg::GetTimeFromIni()
{
	int year = 0, month = 0, date = 0, hour = 0, server_count = 0, sgroup_count = 0;
	char ipkey[20] = {};

	std::filesystem::path iniPath(GetProgPath().GetString());
	iniPath /= L"gameserver.ini";

	m_Ini.Load(iniPath);
	m_nYear = m_Ini.GetInt("TIMER", "YEAR", 1);
	m_nMonth = m_Ini.GetInt("TIMER", "MONTH", 1);
	m_nDate = m_Ini.GetInt("TIMER", "DATE", 1);
	m_nHour = m_Ini.GetInt("TIMER", "HOUR", 1);
	m_nWeather = m_Ini.GetInt("TIMER", "WEATHER", 1);

//	m_nBattleZoneOpenWeek  = m_Ini.GetInt("BATTLE", "WEEK", 3);
	m_nBattleZoneOpenWeek = m_Ini.GetInt("BATTLE", "WEEK", 5);
	m_nBattleZoneOpenHourStart = m_Ini.GetInt("BATTLE", "START_TIME", 20);
	m_nBattleZoneOpenHourEnd = m_Ini.GetInt("BATTLE", "END_TIME", 0);

	// TODO: This should be removed
	m_Ini.GetString(_T("ODBC"), _T("GAME_DSN"), _T("KN_online"), m_strGameDSN, _countof(m_strGameDSN));
	m_Ini.GetString(_T("ODBC"), _T("GAME_UID"), _T("knight"), m_strGameUID, _countof(m_strGameUID));
	m_Ini.GetString(_T("ODBC"), _T("GAME_PWD"), _T("knight"), m_strGamePWD, _countof(m_strGamePWD));

	std::string datasourceName = m_Ini.GetString("ODBC", "GAME_DSN", "KN_online");
	std::string datasourceUser = m_Ini.GetString("ODBC", "GAME_UID", "knight");
	std::string datasourcePass = m_Ini.GetString("ODBC", "GAME_PWD", "knight");

	DatabaseConnManager::SetDatasourceConfig(
		modelUtil::DbType::GAME,
		datasourceName, datasourceUser, datasourcePass);

	m_Ini.GetString("AI_SERVER", "IP", "127.0.0.1", m_AIServerIP, _countof(m_AIServerIP));

	m_nCastleCapture = m_Ini.GetInt("CASTLE", "NATION", 1);
	m_nServerNo = m_Ini.GetInt("ZONE_INFO", "MY_INFO", 1);
	m_nServerGroup = m_Ini.GetInt("ZONE_INFO", "SERVER_NUM", 0);
	server_count = m_Ini.GetInt("ZONE_INFO", "SERVER_COUNT", 1);
	if (server_count < 1)
	{
		AfxMessageBox(_T("ServerCount Error!!"));
		return;
	}

	for (int i = 0; i < server_count; i++)
	{
		_ZONE_SERVERINFO* pInfo = new _ZONE_SERVERINFO;

		sprintf(ipkey, "SERVER_%02d", i);
		pInfo->sServerNo = m_Ini.GetInt("ZONE_INFO", ipkey, 1);

		sprintf(ipkey, "SERVER_IP_%02d", i);
		m_Ini.GetString("ZONE_INFO", ipkey, "127.0.0.1", pInfo->strServerIP, _countof(pInfo->strServerIP));

		pInfo->sPort = _LISTEN_PORT + pInfo->sServerNo;

		m_ServerArray.PutData(pInfo->sServerNo, pInfo);
	}

	if (m_nServerGroup != 0)
	{
		m_nServerGroupNo = m_Ini.GetInt("SG_INFO", "GMY_INFO", 1);
		sgroup_count = m_Ini.GetInt("SG_INFO", "GSERVER_COUNT", 1);
		if (sgroup_count < 1)
		{
			AfxMessageBox(_T("Server group count error!!"));
			return;
		}

		for (int i = 0; i < sgroup_count; i++)
		{
			_ZONE_SERVERINFO* pInfo = new _ZONE_SERVERINFO;

			sprintf(ipkey, "GSERVER_%02d", i);
			pInfo->sServerNo = m_Ini.GetInt("SG_INFO", ipkey, 1);

			sprintf(ipkey, "GSERVER_IP_%02d", i);
			m_Ini.GetString("SG_INFO", ipkey, "127.0.0.1", pInfo->strServerIP, _countof(pInfo->strServerIP));

			pInfo->sPort = _LISTEN_PORT + pInfo->sServerNo;

			m_ServerGroupArray.PutData(pInfo->sServerNo, pInfo);
		}
	}

	// Trigger a save to flush defaults to file.
	m_Ini.Save();

	SetTimer(GAME_TIME, 6000, nullptr);
	SetTimer(SEND_TIME, 200, nullptr);
	SetTimer(ALIVE_TIME, 34000, nullptr);
	SetTimer(MARKET_BBS_TIME, 300000, nullptr);
	SetTimer(PACKET_CHECK, 360000, nullptr);
}

void CEbenezerDlg::UpdateGameTime()
{
	CUser* pUser = nullptr;
	BOOL bKnights = FALSE;

	m_nMin++;

	BattleZoneOpenTimer();	// Check if it's time for the BattleZone to open or end.

	if (m_nMin == 60)
	{
		m_nHour++;
		m_nMin = 0;

		UpdateWeather();
		SetGameTime();

		//  갓댐 산타!! >.<
		if (m_bSanta)
			FlySanta();
		//
	}

	if (m_nHour == 24)
	{
		m_nDate++;
		m_nHour = 0;
		bKnights = TRUE;
	}

	if (m_nDate == 31)
	{
		m_nMonth++;
		m_nDate = 1;
	}

	if (m_nMonth == 13)
	{
		m_nYear++;
		m_nMonth = 1;
	}

	// ai status check packet...
	m_sErrorSocketCount++;

	int send_index = 0;
	char pSendBuf[256] = {};
	//SetByte(pSendBuf, AG_CHECK_ALIVE_REQ, send_index);
	//Send_AIServer(1000, pSendBuf, send_index);

	// 시간과 날씨 정보를 보낸다..
	memset(pSendBuf, 0, sizeof(pSendBuf));
	send_index = 0;
	SetByte(pSendBuf, AG_TIME_WEATHER, send_index);
	SetShort(pSendBuf, m_nYear, send_index);				// time info
	SetShort(pSendBuf, m_nMonth, send_index);
	SetShort(pSendBuf, m_nDate, send_index);
	SetShort(pSendBuf, m_nHour, send_index);
	SetShort(pSendBuf, m_nMin, send_index);
	SetByte(pSendBuf, (BYTE) m_nWeather, send_index);		// weather info
	SetShort(pSendBuf, m_nAmount, send_index);
	Send_AIServer(1000, pSendBuf, send_index);

	if (bKnights)
	{
		memset(pSendBuf, 0, sizeof(pSendBuf));
		send_index = 0;
		SetByte(pSendBuf, WIZ_KNIGHTS_PROCESS, send_index);
		SetByte(pSendBuf, KNIGHTS_ALLLIST_REQ + 0x10, send_index);
		SetByte(pSendBuf, m_nServerNo, send_index);
		m_LoggerSendQueue.PutData(pSendBuf, send_index);
	}
}

void CEbenezerDlg::UpdateWeather()
{
	int weather = 0, result = 0, send_index = 0;
	char send_buff[256] = {};

	result = myrand(0, 100);

//	if (result < 5)
	if (result < 2)
		weather = WEATHER_SNOW;
//	else if (result < 15)
	else if (result < 7)
		weather = WEATHER_RAIN;
	else
		weather = WEATHER_FINE;

	m_nAmount = myrand(0, 100);

	// WEATHER_FINE 일때 m_nAmount 는 안개 정도 표시
	if (weather == WEATHER_FINE)
	{
		if (m_nAmount > 70)
			m_nAmount = m_nAmount / 2;
		else
			m_nAmount = 0;
	}

	m_nWeather = weather;

	SetByte(send_buff, WIZ_WEATHER, send_index);
	SetByte(send_buff, (BYTE) m_nWeather, send_index);
	SetShort(send_buff, m_nAmount, send_index);
	Send_All(send_buff, send_index);
}

void CEbenezerDlg::SetGameTime()
{
	m_Ini.SetInt("TIMER", "YEAR", m_nYear);
	m_Ini.SetInt("TIMER", "MONTH", m_nMonth);
	m_Ini.SetInt("TIMER", "DATE", m_nDate);
	m_Ini.SetInt("TIMER", "HOUR", m_nHour);
	m_Ini.SetInt("TIMER", "WEATHER", m_nWeather);
	m_Ini.Save();
}

void CEbenezerDlg::UserInOutForMe(CUser* pSendUser)
{
	int send_index = 0, buff_index = 0, i = 0, j = 0, t_count = 0, prev_index = 0;
	C3DMap* pMap = nullptr;
	int region_x = -1, region_z = -1, user_count = 0, uid = -1;
	char buff[16384] = {}, send_buff[49152] = {};

	if (pSendUser == nullptr)
		return;

	pMap = GetMapByIndex(pSendUser->m_iZoneIndex);
	if (pMap == nullptr)
		return;

	send_index = 3;		// packet command 와 user_count 를 나중에 셋팅한다...
	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ;			// CENTER
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ - 1;	// NORTH WEST
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ - 1;		// NORTH
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	prev_index = buff_index + send_index;

	if (prev_index >= 49152)
	{
		TRACE(_T("#### UserInOutForMe - buffer overflow = %d ####\n"), prev_index);
		return;
	}

	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ - 1;	// NORTH EAST
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	prev_index = buff_index + send_index;

	if (prev_index >= 49152)
	{
		TRACE(_T("#### UserInOutForMe - buffer overflow = %d ####\n"), prev_index);
		return;
	}

	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ;		// WEST
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	prev_index = buff_index + send_index;

	if (prev_index >= 49152)
	{
		TRACE(_T("#### UserInOutForMe - buffer overflow = %d ####\n"), prev_index);
		return;
	}

	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ;		// EAST
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	prev_index = buff_index + send_index;
	if (prev_index >= 49152)
	{
		TRACE(_T("#### UserInOutForMe - buffer overflow = %d ####\n"), prev_index);
		return;
	}

	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ + 1;	// SOUTH WEST
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	prev_index = buff_index + send_index;
	if (prev_index >= 49152)
	{
		TRACE(_T("#### UserInOutForMe - buffer overflow = %d ####\n"), prev_index);
		return;
	}

	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ + 1;		// SOUTH
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	prev_index = buff_index + send_index;

	if (prev_index >= 49152)
	{
		TRACE(_T("#### UserInOutForMe - buffer overflow = %d ####\n"), prev_index);
		return;
	}

	SetString(send_buff, buff, buff_index, send_index);
	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ + 1;	// SOUTH EAST
	buff_index = GetRegionUserIn(pMap, region_x, region_z, buff, t_count);
	prev_index = buff_index + send_index;
	if (prev_index >= 49152)
	{
		TRACE(_T("#### UserInOutForMe - buffer overflow = %d ####\n"), prev_index);
		return;
	}

	SetString(send_buff, buff, buff_index, send_index);

	int temp_index = 0;
	SetByte(send_buff, WIZ_REQ_USERIN, temp_index);
	SetShort(send_buff, t_count, temp_index);

	pSendUser->SendCompressingPacket(send_buff, send_index);
}

void CEbenezerDlg::RegionUserInOutForMe(CUser* pSendUser)
{
	int send_index = 0, buff_index = 0, i = 0, j = 0, t_count = 0;
	C3DMap* pMap = nullptr;
	int region_x = -1, region_z = -1, user_count = 0, uid_sendindex = 0;
	char uid_buff[2048] = {}, send_buff[16384] = {};

	if (pSendUser == nullptr)
		return;

	pMap = GetMapByIndex(pSendUser->m_iZoneIndex);
	if (pMap == nullptr)
		return;

	uid_sendindex = 3;	// packet command 와 user_count 는 나중에 셋팅한다...

	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ;			// CENTER
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);
	memset(uid_buff, 0, sizeof(uid_buff));

	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ - 1;	// NORTH WEST
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);
	memset(uid_buff, 0, sizeof(uid_buff));

	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ - 1;		// NORTH
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);
	memset(uid_buff, 0, sizeof(uid_buff));

	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ - 1;	// NORTH EAST
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);
	memset(uid_buff, 0, sizeof(uid_buff));

	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ;		// WEST
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);
	memset(uid_buff, 0, sizeof(uid_buff));

	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ;		// EAST
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);
	memset(uid_buff, 0, sizeof(uid_buff));

	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ + 1;	// SOUTH WEST
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);
	memset(uid_buff, 0, sizeof(uid_buff));

	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ + 1;		// SOUTH
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);
	memset(uid_buff, 0, sizeof(uid_buff));

	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ + 1;	// SOUTH EAST
	buff_index = GetRegionUserList(pMap, region_x, region_z, uid_buff, user_count);
	SetString(send_buff, uid_buff, buff_index, uid_sendindex);

	int temp_index = 0;
	SetByte(send_buff, WIZ_REGIONCHANGE, temp_index);
	SetShort(send_buff, user_count, temp_index);

	pSendUser->Send(send_buff, uid_sendindex);

	if (user_count > 500)
		TRACE(_T("Req UserIn: %d \n"), user_count);
}

int CEbenezerDlg::GetRegionUserIn(C3DMap* pMap, int region_x, int region_z, char* buff, int& t_count)
{
	if (pMap == nullptr)
		return 0;

	if (region_x < 0
		|| region_z < 0
		|| region_x > pMap->GetXRegionMax()
		|| region_z > pMap->GetZRegionMax())
		return 0;

	int buff_index = 0;

	EnterCriticalSection(&g_region_critical);

	for (const auto& [_, pUid] : pMap->m_ppRegion[region_x][region_z].m_RegionUserArray)
	{
		int uid = *pUid;
		if (uid < 0)
			continue;

		CUser* pUser = (CUser*) m_Iocport.m_SockArray[uid];
		if (pUser == nullptr)
			continue;

		if (pUser->m_RegionX != region_x
			|| pUser->m_RegionZ != region_z)
			continue;

		if (pUser->GetState() != STATE_GAMESTART)
			continue;

		SetShort(buff, pUser->GetSocketID(), buff_index);
		pUser->GetUserInfo(buff, buff_index);

		++t_count;
	}

	LeaveCriticalSection(&g_region_critical);

	return buff_index;
}

int CEbenezerDlg::GetRegionUserList(C3DMap* pMap, int region_x, int region_z, char* buff, int& t_count)
{
	if (pMap == nullptr)
		return 0;

	if (region_x < 0
		|| region_z < 0
		|| region_x > pMap->GetXRegionMax()
		|| region_z > pMap->GetZRegionMax())
		return 0;

	int buff_index = 0;

	EnterCriticalSection(&g_region_critical);

	for (const auto& [_, pUid] : pMap->m_ppRegion[region_x][region_z].m_RegionUserArray)
	{
		int uid = *pUid;
		if (uid < 0)
			continue;

		CUser* pUser = (CUser*) m_Iocport.m_SockArray[uid];
		if (pUser != nullptr
			&& pUser->GetState() == STATE_GAMESTART)
		{
			SetShort(buff, pUser->GetSocketID(), buff_index);
			t_count++;
		}
	}

	LeaveCriticalSection(&g_region_critical);

	return buff_index;
}

void CEbenezerDlg::NpcInOutForMe(CUser* pSendUser)
{
	int send_index = 0, buff_index = 0, t_count = 0;
	C3DMap* pMap = nullptr;
	int region_x = -1, region_z = -1;
	char buff[8192] = {}, send_buff[32768] = {};

	if (pSendUser == nullptr)
		return;

	pMap = GetMapByIndex(pSendUser->m_iZoneIndex);
	if (pMap == nullptr)
		return;

	send_index = 3;		// packet command 와 user_count 를 나중에 셋팅한다...
	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ;			// CENTER
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ - 1;	// NORTH WEST
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ - 1;		// NORTH
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ - 1;	// NORTH EAST
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ;		// WEST
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ;		// EAST
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ + 1;	// SOUTH WEST
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ + 1;		// SOUTH
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	memset(buff, 0, sizeof(buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ + 1;	// SOUTH EAST
	buff_index = GetRegionNpcIn(pMap, region_x, region_z, buff, t_count);
	SetString(send_buff, buff, buff_index, send_index);

	int temp_index = 0;
	SetByte(send_buff, WIZ_REQ_NPCIN, temp_index);
	SetShort(send_buff, t_count, temp_index);

	pSendUser->SendCompressingPacket(send_buff, send_index);
}

int CEbenezerDlg::GetRegionNpcIn(C3DMap* pMap, int region_x, int region_z, char* buff, int& t_count)
{
	// 포인터 참조하면 안됨
	if (!m_bPointCheckFlag)
		return 0;

	if (pMap == nullptr)
		return 0;

	if (region_x < 0
		|| region_z < 0
		|| region_x > pMap->GetXRegionMax()
		|| region_z > pMap->GetZRegionMax())
		return 0;

	int buff_index = 0;

	EnterCriticalSection(&g_region_critical);

	//string.Format("---- GetRegionNpcIn , x=%d, z=%d ----\r\n", region_x, region_z);
	//EnterCriticalSection( &g_LogFile_critical );
	//m_RegionLogFile.Write( string, string.GetLength() );
	//LeaveCriticalSection( &g_LogFile_critical );

	for (const auto& [_, pNid] : pMap->m_ppRegion[region_x][region_z].m_RegionNpcArray)
	{
		int nid = *pNid;
		if (nid < 0)
			continue;

		CNpc* pNpc = m_arNpcArray.GetData(nid);
		if (pNpc == nullptr)
			continue;

		if (pNpc->m_sRegion_X != region_x
			|| pNpc->m_sRegion_Z != region_z)
			continue;

		SetShort(buff, pNpc->m_sNid, buff_index);
		pNpc->GetNpcInfo(buff, buff_index);

		t_count++;

		//string.Format("nid=%d, name=%s, count=%d \r\n", pNpc->m_sNid, pNpc->m_strName, t_count);
		//EnterCriticalSection( &g_LogFile_critical );
		//m_RegionLogFile.Write( string, string.GetLength() );
		//LeaveCriticalSection( &g_LogFile_critical );

		//if( pNpc->m_sCurZone > 100 ) 
		//	TRACE(_T("GetRegionNpcIn rx=%d, rz=%d, nid=%d, name=%hs, count=%d\n"), region_x, region_z, pNpc->m_sNid, pNpc->m_strName, t_count);
	}

	LeaveCriticalSection(&g_region_critical);

	return buff_index;
}

void CEbenezerDlg::RegionNpcInfoForMe(CUser* pSendUser, int nType)
{
	int send_index = 0, buff_index = 0, i = 0, j = 0, t_count = 0;
	C3DMap* pMap = nullptr;
	int region_x = -1, region_z = -1, npc_count = 0, nid_sendindex = 0;
	char nid_buff[1024] = {}, send_buff[8192] = {};
	CString string;

	if (pSendUser == nullptr)
		return;

	pMap = GetMapByIndex(pSendUser->m_iZoneIndex);
	if (pMap == nullptr)
		return;

	nid_sendindex = 3;	// packet command 와 user_count 는 나중에 셋팅한다...

	// test
	if (nType == 1)
	{
		char strLog[256] = {};
		CTime t = CTime::GetCurrentTime();
		sprintf(strLog, "**** RegionNpcInfoForMe start(%d:%d-%d) : name=%s, x=%d, z=%d **** \r\n", t.GetHour(), t.GetMinute(), t.GetSecond(), pSendUser->m_pUserData->m_id, pSendUser->m_RegionX, pSendUser->m_RegionZ);
		EnterCriticalSection(&g_LogFile_critical);
		m_RegionLogFile.Write(strLog, strlen(strLog));
		LeaveCriticalSection(&g_LogFile_critical);
		//TRACE(strLog);
	}

	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ;			// CENTER
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	memset(nid_buff, 0, sizeof(nid_buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ - 1;	// NORTH WEST
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	memset(nid_buff, 0, sizeof(nid_buff));
	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ - 1;		// NORTH
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	memset(nid_buff, 0, sizeof(nid_buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ - 1;	// NORTH EAST
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	memset(nid_buff, 0, sizeof(nid_buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ;		// WEST
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	memset(nid_buff, 0, sizeof(nid_buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ;		// EAST
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	memset(nid_buff, 0, sizeof(nid_buff));
	region_x = pSendUser->m_RegionX - 1;	region_z = pSendUser->m_RegionZ + 1;	// SOUTH WEST
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	memset(nid_buff, 0, sizeof(nid_buff));
	region_x = pSendUser->m_RegionX;	region_z = pSendUser->m_RegionZ + 1;		// SOUTH
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	memset(nid_buff, 0, sizeof(nid_buff));
	region_x = pSendUser->m_RegionX + 1;	region_z = pSendUser->m_RegionZ + 1;	// SOUTH EAST
	buff_index = GetRegionNpcList(pMap, region_x, region_z, nid_buff, npc_count, nType);
	SetString(send_buff, nid_buff, buff_index, nid_sendindex);

	int temp_index = 0;

	// test 
	if (nType == 1)
	{
		char strLog[256] = {};
		SetByte(send_buff, WIZ_TEST_PACKET, temp_index);
		sprintf(strLog, "**** RegionNpcInfoForMe end : name=%s, x=%d, z=%d, count=%d **** \r\n", pSendUser->m_pUserData->m_id, pSendUser->m_RegionX, pSendUser->m_RegionZ, npc_count);
		EnterCriticalSection(&g_LogFile_critical);
		m_RegionLogFile.Write(strLog, strlen(strLog));
		LeaveCriticalSection(&g_LogFile_critical);
		//TRACE(strLog);
	}
	else
	{
		SetByte(send_buff, WIZ_NPC_REGION, temp_index);
	}

	SetShort(send_buff, npc_count, temp_index);

	pSendUser->Send(send_buff, nid_sendindex);

	if (npc_count > 500)
		TRACE(_T("Req Npc In: %d \n"), npc_count);
}

int CEbenezerDlg::GetRegionNpcList(C3DMap* pMap, int region_x, int region_z, char* nid_buff, int& t_count, int nType)
{
	// 포인터 참조하면 안됨
	if (!m_bPointCheckFlag)
		return 0;

	if (pMap == nullptr)
		return 0;

	if (region_x < 0
		|| region_z < 0
		|| region_x > pMap->GetXRegionMax()
		|| region_z > pMap->GetZRegionMax())
		return 0;

	char strLog[1024] = {};
	int buff_index = 0;

	EnterCriticalSection(&g_region_critical);

	if (nType == 1)
	{
		sprintf(strLog, "++++ GetRegionNpcList , x=%d, z=%d ++++\r\n", region_x, region_z);
		EnterCriticalSection(&g_LogFile_critical);
		m_RegionLogFile.Write(strLog, strlen(strLog));
		LeaveCriticalSection(&g_LogFile_critical);
		memset(strLog, 0, sizeof(strLog));
	}

	for (const auto& [_, pNid] : pMap->m_ppRegion[region_x][region_z].m_RegionNpcArray)
	{
		int nid = *pNid;
		if (nid < 0)
			continue;

		CNpc* pNpc = m_arNpcArray.GetData(nid);
		//if( pNpc && (pNpc->m_NpcState == NPC_LIVE ) ) {  // 수정할 것,,
		if (pNpc != nullptr)
		{
			SetShort(nid_buff, pNpc->m_sNid, buff_index);
			t_count++;
			if (nType == 1)
			{
				sprintf(strLog, "%d   ", pNpc->m_sNid);
				EnterCriticalSection(&g_LogFile_critical);
				m_RegionLogFile.Write(strLog, strlen(strLog));
				LeaveCriticalSection(&g_LogFile_critical);
			}
		}
		else
		{
			if (nType == 1)
			{
				sprintf(strLog, "%d(Err)   ", nid);
				EnterCriticalSection(&g_LogFile_critical);
				m_RegionLogFile.Write(strLog, strlen(strLog));
				LeaveCriticalSection(&g_LogFile_critical);
			}
		}
	}

	if (nType == 1)
	{
		sprintf(strLog, "\r\n");
		EnterCriticalSection(&g_LogFile_critical);
		m_RegionLogFile.Write(strLog, strlen(strLog));
		LeaveCriticalSection(&g_LogFile_critical);
	}

	LeaveCriticalSection(&g_region_critical);

	return buff_index;
}

int CEbenezerDlg::GetZoneIndex(int zonenumber)
{
	for (size_t i = 0; i < m_ZoneArray.size(); i++)
	{
		C3DMap* pMap = m_ZoneArray[i];
		if (pMap != nullptr
			&& zonenumber == pMap->m_nZoneNumber)
			return static_cast<int>(i);
	}

	return -1;
}

BOOL CEbenezerDlg::PreTranslateMessage(MSG* pMsg)
{
	char buff[1024] = {};
	char chatstr[256] = {}, killstr[256] = {};
	int chatlen = 0, buffindex = 0;

	std::string buff2;
//
	BOOL permanent_off = FALSE;
//
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_RETURN)
		{
			::GetWindowTextA(m_AnnounceEdit.GetSafeHwnd(), chatstr, 256);

			UpdateData(TRUE);
			chatlen = strlen(chatstr);
			if (chatlen == 0)
				return TRUE;

			m_AnnounceEdit.SetWindowText(_T(""));
			UpdateData(FALSE);

			if (_strnicmp("/kill", chatstr, 5) == 0)
			{
				strcpy(killstr, chatstr + 6);
				KillUser(killstr);
				return TRUE;
			}

			if (_strnicmp("/Open", chatstr, 5) == 0)
			{
				BattleZoneOpen(BATTLEZONE_OPEN);
				return TRUE;
			}

			if (_strnicmp("/snowopen", chatstr, 9) == 0)
			{
				BattleZoneOpen(SNOW_BATTLEZONE_OPEN);
				return TRUE;
			}

			if (_strnicmp("/Close", chatstr, 6) == 0)
			{
				m_byBanishFlag = 1;
				//WithdrawUserOut();
				return TRUE;
			}

			if (_strnicmp("/down", chatstr, 5) == 0)
			{
				g_serverdown_flag = TRUE;
				::SuspendThread(m_Iocport.m_hAcceptThread);
				KickOutAllUsers();
				return TRUE;
			}

			if (_strnicmp("/discount", chatstr, 9) == 0)
			{
				m_sDiscount = 1;
				return TRUE;
			}

			if (_strnicmp("/alldiscount", chatstr, 12) == 0)
			{
				m_sDiscount = 2;
				return TRUE;
			}

			if (_strnicmp("/undiscount", chatstr, 11) == 0)
			{
				m_sDiscount = 0;
				return TRUE;
			}

			// 비러머글 남는 공지 --;
			if (_strnicmp("/permanent", chatstr, 10) == 0)
			{
				m_bPermanentChatMode = TRUE;
				m_bPermanentChatFlag = TRUE;
				return TRUE;
			}

			if (_strnicmp("/captain", chatstr, 8) == 0)
			{
				LoadKnightsRankTable();				// captain 
				return TRUE;
			}

			if (_strnicmp("/offpermanent", chatstr, 13) == 0)
			{
				m_bPermanentChatMode = FALSE;
				m_bPermanentChatFlag = FALSE;
				permanent_off = TRUE;
//				return TRUE;	//이것은 고의적으로 TRUE를 뺐었음
			}
//

			// 갓댐 산타!!! >.<
			if (_strnicmp("/santa", chatstr, 6) == 0)
			{
				m_bSanta = TRUE;			// Make Motherfucking Santa Claus FLY!!!
				return TRUE;
			}

			if (_strnicmp("/offsanta", chatstr, 9) == 0)
			{
				m_bSanta = FALSE;			// SHOOT DOWN Motherfucking Santa Claus!!!
				return TRUE;
			}
//

			char finalstr[256] = {};
//			sprintf( finalstr, "#### 공지 : %s ####", chatstr );

			// 비러머글 남는 공지		
			if (m_bPermanentChatFlag)
			{
				sprintf(finalstr, "- %s -", chatstr);
			}
			else
			{
				//sprintf( finalstr, "#### 공지 : %s ####", chatstr );
				::_LoadStringFromResource(IDP_ANNOUNCEMENT, buff2);
				sprintf(finalstr, buff2.c_str(), chatstr);
			}

			//
			SetByte(buff, WIZ_CHAT, buffindex);
			// SetByte( buff, PUBLIC_CHAT, buffindex );

			// 비러머글 남는 공지
			if (permanent_off)
			{
				SetByte(buff, END_PERMANENT_CHAT, buffindex);
			}
			else if (!m_bPermanentChatFlag)
			{
				SetByte(buff, PUBLIC_CHAT, buffindex);
			}
			else
			{
				SetByte(buff, PERMANENT_CHAT, buffindex);
				strcpy(m_strPermanentChat, finalstr);
				m_bPermanentChatFlag = FALSE;
			}
//
			SetByte(buff, 0x01, buffindex);		// nation
			SetShort(buff, -1, buffindex);		// sid
			SetByte(buff, 0, buffindex);		// sender name length
			SetString2(buff, finalstr, static_cast<short>(strlen(finalstr)), buffindex);
			Send_All(buff, buffindex);

			buffindex = 0;
			memset(buff, 0x00, 1024);
			SetByte(buff, STS_CHAT, buffindex);
			SetShort(buff, strlen(finalstr), buffindex);
			SetString(buff, finalstr, strlen(finalstr), buffindex);

			for (const auto& [_, pInfo] : m_ServerArray)
			{
				if (pInfo != nullptr
					&& pInfo->sServerNo != m_nServerNo)
					m_pUdpSocket->SendUDPPacket(pInfo->strServerIP, buff, buffindex);
			}

			return TRUE;
		}

		if (pMsg->wParam == VK_ESCAPE)
			return TRUE;
	}

	if (pMsg->wParam == VK_F8)
		SyncTest(1);
	else if (pMsg->wParam == VK_F9)
		SyncTest(2);

	return CDialog::PreTranslateMessage(pMsg);
}

BOOL CEbenezerDlg::LoadNoticeData()
{
	CString ProgPath = GetProgPath();
	CString NoticePath = ProgPath + "Notice.txt";
	CString buff;
	CStdioFile txt_file;
	int count = 0;

	if (!txt_file.Open(NoticePath, CFile::modeRead))
	{
#if !defined(_DEBUG)
		AfxMessageBox(_T("cannot open Notice.txt!!"));
#endif
		return FALSE;
	}

	while (txt_file.ReadString(buff))
	{
		if (count > 19)
			AfxMessageBox(_T("Notice Count Overflow!!"));

		strcpy(m_ppNotice[count], CT2A(buff));
		count++;
	}

	txt_file.Close();

	return TRUE;
}

void CEbenezerDlg::SyncTest(int nType)
{
	char strPath[100] = {};
	if (nType == 1)
		strcpy(strPath, "c:\\userlist.txt");
	else if (nType == 2)
		strcpy(strPath, "c:\\npclist.txt");

	FILE* stream = fopen(strPath, "w");
	if (stream == nullptr)
		return;

	int len = 0;
	char pBuf[256] = {};

	SetByte(pBuf, AG_CHECK_ALIVE_REQ, len);

	for (C3DMap* pMap : m_ZoneArray)
	{
		if (pMap == nullptr)
			continue;

		CAISocket* pSocket = m_AISocketArray.GetData(pMap->m_nZoneNumber);
		if (pSocket == nullptr)
			continue;

		int size = pSocket->Send(pBuf, len);
		fprintf(stream, "size=%d, zone=%d, number=%d\n", size, pSocket->m_iZoneNum, pMap->m_nZoneNumber);

		//return;
	}

	fprintf(stream, "*****   Region List  *****\n");

	for (C3DMap* pMap : m_ZoneArray)
	{
		//if (k != 2) continue;		// 201 존만 체크..
		if (pMap == nullptr)
			continue;

		for (int i = 0; i < pMap->GetXRegionMax(); i++)
		{
			for (int j = 0; j < pMap->GetZRegionMax(); j++)
			{
				EnterCriticalSection(&g_region_critical);
				int total_user = pMap->m_ppRegion[i][j].m_RegionUserArray.GetSize();
				int total_mon = pMap->m_ppRegion[i][j].m_RegionNpcArray.GetSize();
				LeaveCriticalSection(&g_region_critical);

				if (total_user > 0
					|| total_mon > 0)
				{
					fprintf(stream, "rx=%d, rz=%d, user=%d, monster=%d\n", i, j, total_user, total_mon);
					SyncRegionTest(pMap, i, j, stream, nType);
				}
			}
		}
	}

	fclose(stream);
}

void CEbenezerDlg::SyncRegionTest(C3DMap* pMap, int rx, int rz, FILE* pfile, int nType)
{
	fprintf(pfile, "ZONE=%d, [%d,%d] : ", pMap->m_nZoneNumber, rx, rz);

	std::map<int, int*>::iterator		Iter1;
	std::map<int, int*>::iterator		Iter2;

	EnterCriticalSection(&g_region_critical);

	if (nType == 2)
	{
		Iter1 = pMap->m_ppRegion[rx][rz].m_RegionNpcArray.begin();
		Iter2 = pMap->m_ppRegion[rx][rz].m_RegionNpcArray.end();
	}
	else if (nType == 1)
	{
		Iter1 = pMap->m_ppRegion[rx][rz].m_RegionUserArray.begin();
		Iter2 = pMap->m_ppRegion[rx][rz].m_RegionUserArray.end();
	}

	for (; Iter1 != Iter2; Iter1++)
	{
		int nid = *Iter1->second;
		if (nType == 1)
		{
			CUser* pUser = (CUser*) m_Iocport.m_SockArray[nid];
			if (pUser == nullptr)
			{
				TRACE(_T("SyncRegionTest : nid fail = %d\n"), nid);
				fprintf(pfile, "%d(fail)	", nid);
				continue;
			}

			fprintf(pfile, "%d(%d,%d)	", nid, (int) pUser->m_pUserData->m_curx, (int) pUser->m_pUserData->m_curz);
		}
		else if (nType == 2)
		{
			CNpc* pNpc = m_arNpcArray.GetData(nid);
			if (pNpc== nullptr)
			{
				TRACE(_T("SyncRegionTest : nid fail = %d\n"), nid);
				fprintf(pfile, "%d(fail)	", nid);
				continue;
			}

			fprintf(pfile, "%d(%d,%d)	", nid, (int) pNpc->m_fCurX, (int) pNpc->m_fCurZ);
		}
	}

	fprintf(pfile, "\n");

	LeaveCriticalSection(&g_region_critical);
}

void CEbenezerDlg::SendAllUserInfo()
{
	int send_index = 0;
	char send_buff[2048] = {};

	SetByte(send_buff, AG_SERVER_INFO, send_index);
	SetByte(send_buff, SERVER_INFO_START, send_index);
	Send_AIServer(1000, send_buff, send_index);

	int count = 0;
	send_index = 2;
	memset(send_buff, 0, sizeof(send_buff));
	int send_count = 0;
	int send_tot = 0;
	int tot = 20;

	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser != nullptr)
		{
			pUser->SendUserInfo(send_buff, send_index);
			count++;
			if (count == tot)
			{
				SetByte(send_buff, AG_USER_INFO_ALL, send_count);
				SetByte(send_buff, (BYTE) count, send_count);
				m_CompCount++;
				memset(m_CompBuf, 0, sizeof(m_CompBuf));
				memcpy(m_CompBuf, send_buff, send_index);
				m_iCompIndex = send_index;
				SendCompressedData();
				send_index = 2;
				send_count = 0;
				count = 0;
				send_tot++;
				//TRACE(_T("AllNpcInfo - send_count=%d, count=%d\n"), send_tot, count);
				memset(send_buff, 0, sizeof(send_buff));
				//Sleep(320);
			}
		}
	}

	if (count != 0
		&& count < (tot - 1))
	{
		send_count = 0;
		SetByte(send_buff, AG_USER_INFO_ALL, send_count);
		SetByte(send_buff, (BYTE) count, send_count);
		Send_AIServer(1000, send_buff, send_index);
		send_tot++;
		//TRACE(_T("AllNpcInfo - send_count=%d, count=%d\n"), send_tot, count);
		//Sleep(1);
	}

	// 파티에 대한 정보도 보내도록 한다....
	EnterCriticalSection(&g_region_critical);

	for (int i = 0; i < m_PartyArray.GetSize(); i++)
	{
		_PARTY_GROUP* pParty = m_PartyArray.GetData(i);
		if (pParty == nullptr)
			continue;

		send_index = 0;
		::ZeroMemory(send_buff, sizeof(send_buff));
		SetByte(send_buff, AG_PARTY_INFO_ALL, send_index);
		SetShort(send_buff, i, send_index);					// 파티 번호
		//if( i == pParty->wIndex )
		for (int j = 0; j < 8; j++)
		{
			SetShort(send_buff, pParty->uid[j], send_index);				// 유저 번호
			//SetShort(send_buff, pParty->sHp[j], send_index );				// HP
			//SetByte(send_buff, pParty->bLevel[j], send_index );				// Level
			//SetShort(send_buff, pParty->sClass[j], send_index );			// Class
		}

		Send_AIServer(1000, send_buff, send_index);
	}

	LeaveCriticalSection(&g_region_critical);

	send_index = 0;
	::ZeroMemory(send_buff, sizeof(send_buff));
	SetByte(send_buff, AG_SERVER_INFO, send_index);
	SetByte(send_buff, SERVER_INFO_END, send_index);
	Send_AIServer(1000, send_buff, send_index);

	TRACE(_T("** SendAllUserInfo() **\n"));
}

void CEbenezerDlg::SendCompressedData()
{
	if (m_CompCount <= 0
		|| m_iCompIndex <= 0)
	{
		m_CompCount = 0;
		m_iCompIndex = 0;
		return;
	}

	int send_index = 0;
	char send_buff[32000] = {};
	uint8_t comp_buff[32000] = {};
	unsigned int comp_data_len = 0;
	uint32_t crc_value = 0;

	comp_data_len = lzf_compress(m_CompBuf, m_iCompIndex, comp_buff, sizeof(comp_buff));

	_ASSERT(comp_data_len != 0 && comp_data_len <= sizeof(comp_buff));

	if (comp_data_len == 0
		|| comp_data_len > sizeof(comp_buff))
	{
		TRACE(_T("Failed to compress AI packet\n"));
		return;
	}

	crc_value = crc32(reinterpret_cast<uint8_t*>(m_CompBuf), m_iCompIndex);

	SetByte(send_buff, AG_COMPRESSED_DATA, send_index);
	SetShort(send_buff, (short) comp_data_len, send_index);
	SetShort(send_buff, (short) m_iCompIndex, send_index);
	SetDWORD(send_buff, crc_value, send_index);
	SetShort(send_buff, (short) m_CompCount, send_index);
	SetString(send_buff, reinterpret_cast<const char*>(comp_buff), comp_data_len, send_index);

	Send_AIServer(1000, send_buff, send_index);

	m_CompCount = 0;
	m_iCompIndex = 0;
}

// sungyong 2002. 05. 23
void CEbenezerDlg::DeleteAllNpcList(int flag)
{
	if (!m_bServerCheckFlag)
		return;

	if (m_bPointCheckFlag)
	{
		m_bPointCheckFlag = FALSE;
		TRACE(_T("*** Point 참조 하면 안되여 *** \n"));
		return;
	}

	CString logstr;
	logstr.Format(_T("[Monster Point Delete]"));
	m_StatusList.AddString(logstr);

	TRACE(_T("*** DeleteAllNpcList - Start *** \n"));

	CUser* pUser = nullptr;

	// region Npc Array Delete
	for (C3DMap* pMap : m_ZoneArray)
	{
		if (pMap == nullptr)
			continue;

		for (int i = 0; i < pMap->GetXRegionMax(); i++)
		{
			for (int j = 0; j < pMap->GetZRegionMax(); j++)
			{
				if (!pMap->m_ppRegion[i][j].m_RegionNpcArray.IsEmpty())
					pMap->m_ppRegion[i][j].m_RegionNpcArray.DeleteAllData();
			}
		}
	}

	// Npc Array Delete
	if (!m_arNpcArray.IsEmpty())
		m_arNpcArray.DeleteAllData();

	m_bServerCheckFlag = FALSE;

	TRACE(_T("*** DeleteAllNpcList - End *** \n"));
}
// ~sungyong 2002. 05. 23

void CEbenezerDlg::KillUser(const char* strbuff)
{
	if (strlen(strbuff) <= 0
		|| strlen(strbuff) > MAX_ID_SIZE)
		return;

	CUser* pUser = GetUserPtr(strbuff, NameType::Character);
	if (pUser != nullptr)
		pUser->Close();
}

CNpc* CEbenezerDlg::GetNpcPtr(int sid, int cur_zone)
{
	// 포인터 참조하면 안됨
	if (!m_bPointCheckFlag)
		return nullptr;

	for (const auto& [_, pNpc] : m_arNpcArray)
	{
		if (pNpc == nullptr)
			continue;

		if (pNpc->m_sCurZone != cur_zone)
			continue;

		if (pNpc->m_sPid == sid)
			return pNpc;
	}

	return nullptr;
}

void CEbenezerDlg::WithdrawUserOut()
{
	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser != nullptr
			&& pUser->m_pUserData->m_bZone == pUser->m_pUserData->m_bNation)
		{
			C3DMap* pMap = GetMapByID(pUser->m_pUserData->m_bNation);
			if (pMap != nullptr)
				pUser->ZoneChange(pMap->m_nZoneNumber, pMap->m_fInitX, pMap->m_fInitZ);
		}
	}
}

void CEbenezerDlg::AliveUserCheck()
{
	float currenttime = TimeGet();

	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		if (pUser->GetState() != STATE_GAMESTART)
			continue;

/*
		if ((currenttime - pUser->m_fHPLastTime) > 300)
			pUser->Close();
*/
		for (int k = 0; k < MAX_TYPE3_REPEAT; k++)
		{
			if ((currenttime - pUser->m_fHPLastTime[k]) > 300)
			{
				pUser->Close();
				break;
			}
		}
	}
}

/////// BATTLEZONE RELATED by Yookozuna 2002.6.18 /////////////////
void CEbenezerDlg::BattleZoneOpenTimer()
{
	CTime cur = CTime::GetCurrentTime();

	// sungyong modify
	int nWeek = cur.GetDayOfWeek();
	int nTime = cur.GetHour();
	char send_buff[128] = {};
	int send_index = 0, loser_nation = 0, snow_battle = 0;
	CUser* pKarusUser = nullptr;
	CUser* pElmoUser = nullptr;

/*	if( m_byBattleOpen == NO_BATTLE )	{	// When Battlezone is closed, open it!
		if( nWeek == m_nBattleZoneOpenWeek && nTime == m_nBattleZoneOpenHourStart )	{	// 수요일, 20시에 전쟁존 open
			TRACE(_T("전쟁 자동 시작 - week=%d, time=%d\n"), nWeek, nTime);
			BattleZoneOpen(BATTLEZONE_OPEN);
//			KickOutZoneUsers(ZONE_FRONTIER);	// Kick out users in frontier zone.
		}
	}
	else {	  // When Battlezone is open, close it!
		if( nWeek == (m_nBattleZoneOpenWeek+1) && nTime == m_nBattleZoneOpenHourEnd )	{	// 목요일, 0시에 전쟁존 close
			TRACE(_T("전쟁 자동 종료 - week=%d, time=%d\n"), nWeek, nTime);
			m_byBanishFlag = 1;
		}
	}	*/

	if (m_byBattleOpen == NATION_BATTLE)
		BattleZoneCurrentUsers();

	if (m_byBanishFlag == 1)
	{
		if (m_sBanishDelay == 0)
		{
			m_byBattleOpen = NO_BATTLE;
			m_byKarusOpenFlag = 0;		// 카루스 땅으로 넘어갈 수 없도록
			m_byElmoradOpenFlag = 0;	// 엘모 땅으로 넘어갈 수 없도록

			memset(m_strKarusCaptain, 0, sizeof(m_strKarusCaptain));
			memset(m_strElmoradCaptain, 0, sizeof(m_strElmoradCaptain));

			TRACE(_T("전쟁 종료 0단계\n"));

			if (m_nServerNo == KARUS)
			{
				memset(send_buff, 0, sizeof(send_buff));
				send_index = 0;
				SetByte(send_buff, UDP_BATTLE_EVENT_PACKET, send_index);
				SetByte(send_buff, BATTLE_EVENT_KILL_USER, send_index);
				SetByte(send_buff, 1, send_index);						// karus의 정보 전송
				SetShort(send_buff, m_sKarusDead, send_index);
				SetShort(send_buff, m_sElmoradDead, send_index);
				Send_UDP_All(send_buff, send_index);
			}
		}

		m_sBanishDelay++;

		if (m_sBanishDelay == 3)
		{
			// 눈싸움 전쟁
			if (m_byOldBattleOpen == SNOW_BATTLE)
			{
				if (m_sKarusDead > m_sElmoradDead)
				{
					m_bVictory = ELMORAD;
					loser_nation = KARUS;
				}
				else if (m_sKarusDead < m_sElmoradDead)
				{
					m_bVictory = KARUS;
					loser_nation = ELMORAD;
				}
				else if (m_sKarusDead == m_sElmoradDead)
				{
					m_bVictory = 0;
				}
			}

			if (m_bVictory == 0)
			{
				BattleZoneOpen(BATTLEZONE_CLOSE);
			}
			else if (m_bVictory)
			{
				if (m_bVictory == KARUS)
					loser_nation = ELMORAD;
				else if (m_bVictory == ELMORAD)
					loser_nation = KARUS;

				Announcement(DECLARE_WINNER, m_bVictory);
				Announcement(DECLARE_LOSER, loser_nation);
			}
			TRACE(_T("전쟁 종료 1단계, m_bVictory=%d\n"), m_bVictory);
		}
		else if (m_sBanishDelay == 8)
		{
			Announcement(DECLARE_BAN);
		}
		else if (m_sBanishDelay == 10)
		{
			TRACE(_T("전쟁 종료 2단계 - 모든 유저 자기 국가로 가 \n"));
			BanishLosers();
		}
		else if (m_sBanishDelay == 20)
		{
			TRACE(_T("전쟁 종료 3단계 - 초기화 해주세여 \n"));
			SetByte(send_buff, AG_BATTLE_EVENT, send_index);
			SetByte(send_buff, BATTLE_EVENT_OPEN, send_index);
			SetByte(send_buff, BATTLEZONE_CLOSE, send_index);
			Send_AIServer(1000, send_buff, send_index);
			ResetBattleZone();
		}
	}

	// ~
}

void CEbenezerDlg::BattleZoneOpen(int nType)
{
	int send_index = 0;
	char send_buff[1024] = {};
	CTime time = CTime::GetCurrentTime();

	// Open battlezone.
	if (nType == BATTLEZONE_OPEN)
	{
		m_byBattleOpen = NATION_BATTLE;
		m_byOldBattleOpen = NATION_BATTLE;
	}
	// Open snow battlezone.
	else if (nType == SNOW_BATTLEZONE_OPEN)
	{
		m_byBattleOpen = SNOW_BATTLE;
		m_byOldBattleOpen = SNOW_BATTLE;

		TCHAR strLogFile[100] = {};
		_stprintf(strLogFile, _T("EventLog-%d-%d-%d.txt"), time.GetYear(), time.GetMonth(), time.GetDay());
		if (m_EvnetLogFile.Open(strLogFile, CFile::modeWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone))
			m_EvnetLogFile.SeekToEnd();
	}
	// battle close
	else if (nType == BATTLEZONE_CLOSE)
	{
		m_byBattleOpen = NO_BATTLE;
		Announcement(BATTLEZONE_CLOSE);
	}
	else
	{
		return;
	}

	Announcement(nType);	// Send an announcement out that the battlezone is open/closed.
//
	KickOutZoneUsers(ZONE_FRONTIER);
//
	memset(send_buff, 0, sizeof(send_buff));
	SetByte(send_buff, AG_BATTLE_EVENT, send_index);		// Send packet to AI server.
	SetByte(send_buff, BATTLE_EVENT_OPEN, send_index);
	SetByte(send_buff, nType, send_index);
	Send_AIServer(1000, send_buff, send_index);
}

void CEbenezerDlg::BattleZoneVictoryCheck()
{
	// WINNER DECLARATION PROCEDURE !!!
	if (m_bKarusFlag >= NUM_FLAG_VICTORY)
		m_bVictory = KARUS;
	else if (m_bElmoradFlag >= NUM_FLAG_VICTORY)
		m_bVictory = ELMORAD;
	else
		return;

	m_bBanishDelayStart = TimeGet();

	Announcement(DECLARE_WINNER);

	// GOLD DISTRIBUTION PROCEDURE FOR WINNERS !!!
	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pTUser = (CUser*) m_Iocport.m_SockArray[i];     // Get target info.
		if (pTUser == nullptr)
			continue;

		if (pTUser->m_pUserData->m_bNation == m_bVictory
			// Zone Check!
			&& pTUser->m_pUserData->m_bZone == pTUser->m_pUserData->m_bNation)
			pTUser->m_pUserData->m_iGold += AWARD_GOLD;	// Target is in the area.
	}
}

void CEbenezerDlg::BanishLosers()
{
	// EVACUATION PROCEDURE FOR LOSERS !!!		
	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pTUser = (CUser*) m_Iocport.m_SockArray[i];     // Get target info.
		if (pTUser == nullptr)
			continue;

		if (pTUser->m_pUserData->m_bFame == COMMAND_CAPTAIN)
		{
			pTUser->m_pUserData->m_bFame = CHIEF;

			char send_buff[256] = {};
			int send_index = 0;
			SetByte(send_buff, WIZ_AUTHORITY_CHANGE, send_index);
			SetByte(send_buff, COMMAND_AUTHORITY, send_index);
			SetShort(send_buff, pTUser->GetSocketID(), send_index);
			SetByte(send_buff, pTUser->m_pUserData->m_bFame, send_index);
			pTUser->Send(send_buff, send_index);
		}

		if (pTUser->m_pUserData->m_bZone != pTUser->m_pUserData->m_bNation)
			pTUser->KickOutZoneUser(TRUE);
	}
}

void CEbenezerDlg::ResetBattleZone()
{
	if (m_byOldBattleOpen == SNOW_BATTLE)
	{
		if (m_EvnetLogFile.m_hFile != CFile::hFileNull)
			m_EvnetLogFile.Close();

		TRACE(_T("Event Log close\n"));
	}

	m_bVictory = 0;
	m_bBanishDelayStart = 0;
	m_byBanishFlag = 0;
	m_sBanishDelay = 0;
	m_bKarusFlag = 0;
	m_bElmoradFlag = 0;
	m_byKarusOpenFlag = m_byElmoradOpenFlag = 0;
	m_byBattleOpen = NO_BATTLE;
	m_byOldBattleOpen = NO_BATTLE;
	m_sKarusDead = m_sElmoradDead = 0;
	m_byBattleSave = 0;
	m_sKarusCount = 0;
	m_sElmoradCount = 0;
	// REMEMBER TO MAKE ALL FLAGS AND LEVERS NEUTRAL AGAIN!!!!!!!!!!
}

void CEbenezerDlg::Announcement(BYTE type, int nation, int chat_type)
{
	int send_index = 0;

	char chatstr[1024] = {},
		finalstr[1024] = {},
		send_buff[1024] = {};

	std::string buff;
	std::string buff2;

	switch (type)
	{
		case BATTLEZONE_OPEN:
			::_LoadStringFromResource(IDP_BATTLEZONE_OPEN, buff);
			sprintf(chatstr, buff.c_str());
			break;

		case SNOW_BATTLEZONE_OPEN:
			::_LoadStringFromResource(IDP_BATTLEZONE_OPEN, buff);
			sprintf(chatstr, buff.c_str());
			break;

		case DECLARE_WINNER:
			if (m_bVictory == KARUS)
			{
				::_LoadStringFromResource(IDP_KARUS_VICTORY, buff);
				sprintf(chatstr, buff.c_str(), m_sElmoradDead, m_sKarusDead);
			}
			else if (m_bVictory == ELMORAD)
			{
				::_LoadStringFromResource(IDP_ELMORAD_VICTORY, buff);
				sprintf(chatstr, buff.c_str(), m_sKarusDead, m_sElmoradDead);
			}
			else
			{
				return;
			}
			break;

		case DECLARE_LOSER:
			if (m_bVictory == KARUS)
			{
				::_LoadStringFromResource(IDS_ELMORAD_LOSER, buff);
				sprintf(chatstr, buff.c_str(), m_sKarusDead, m_sElmoradDead);
			}
			else if (m_bVictory == ELMORAD)
			{
				::_LoadStringFromResource(IDS_KARUS_LOSER, buff);
				sprintf(chatstr, buff.c_str(), m_sElmoradDead, m_sKarusDead);
			}
			else
			{
				return;
			}
			break;

		case DECLARE_BAN:
			::_LoadStringFromResource(IDS_BANISH_USER, buff);
			sprintf(chatstr, buff.c_str());
			break;

		case BATTLEZONE_CLOSE:
			::_LoadStringFromResource(IDS_BATTLE_CLOSE, buff);
			sprintf(chatstr, buff.c_str());
			break;

		case KARUS_CAPTAIN_NOTIFY:
			::_LoadStringFromResource(IDS_KARUS_CAPTAIN, buff);
			sprintf(chatstr, buff.c_str(), m_strKarusCaptain);
			break;

		case ELMORAD_CAPTAIN_NOTIFY:
			::_LoadStringFromResource(IDS_ELMO_CAPTAIN, buff);
			sprintf(chatstr, buff.c_str(), m_strElmoradCaptain);
			break;

		case KARUS_CAPTAIN_DEPRIVE_NOTIFY:
			::_LoadStringFromResource(IDS_KARUS_CAPTAIN_DEPRIVE, buff);
			sprintf(chatstr, buff.c_str(), m_strKarusCaptain);
			break;

		case ELMORAD_CAPTAIN_DEPRIVE_NOTIFY:
			::_LoadStringFromResource(IDS_ELMO_CAPTAIN_DEPRIVE, buff);
			sprintf(chatstr, buff.c_str(), m_strElmoradCaptain);
			break;
	}

	::_LoadStringFromResource(IDP_ANNOUNCEMENT, buff2);
	sprintf(finalstr, buff2.c_str(), chatstr);
	//sprintf( finalstr, "## 공지 : %s ##", chatstr );
	SetByte(send_buff, WIZ_CHAT, send_index);
	SetByte(send_buff, chat_type, send_index);
	SetByte(send_buff, 1, send_index);
	SetShort(send_buff, -1, send_index);
	SetByte(send_buff, 0, send_index);			// sender name length
	SetString2(send_buff, finalstr, static_cast<short>(strlen(finalstr)), send_index);

	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		if (pUser->GetState() == STATE_GAMESTART)
		{
			if (nation == 0)
				pUser->Send(send_buff, send_index);
			else if (nation == pUser->m_pUserData->m_bNation)
				pUser->Send(send_buff, send_index);
		}
	}
}

BOOL CEbenezerDlg::LoadHomeTable()
{
	recordset_loader::Error err = {};
	if (!recordset_loader::STLMap_ForbidEmpty(m_HomeArray, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadAllKnights()
{
	CKnightsSet	KnightsSet;
	CString strKnightsName, strChief, strViceChief_1, strViceChief_2, strViceChief_3;

	if (!KnightsSet.Open())
	{
		AfxMessageBox(_T("Knights Open Fail!"));
		return FALSE;
	}

	if (KnightsSet.IsBOF()
		|| KnightsSet.IsEOF())
	{
		// AfxMessageBox(_T("Knights Data Empty!"));
		return TRUE;
	}

	KnightsSet.MoveFirst();

	while (!KnightsSet.IsEOF())
	{
		CKnights* pKnights = new CKnights;
		pKnights->InitializeValue();

		pKnights->m_sIndex = KnightsSet.m_IDNum;
		pKnights->m_byFlag = KnightsSet.m_Flag;
		pKnights->m_byNation = KnightsSet.m_Nation;
		strKnightsName = KnightsSet.m_IDName;
		strKnightsName.TrimRight();
		strChief = KnightsSet.m_Chief;
		strChief.TrimRight();
		strViceChief_1 = KnightsSet.m_ViceChief_1;
		strViceChief_1.TrimRight();
		strViceChief_2 = KnightsSet.m_ViceChief_2;
		strViceChief_2.TrimRight();
		strViceChief_3 = KnightsSet.m_ViceChief_3;
		strViceChief_3.TrimRight();

		strcpy(pKnights->m_strName, CT2A(strKnightsName));
		pKnights->m_sMembers = KnightsSet.m_Members;
		strcpy(pKnights->m_strChief, CT2A(strChief));
		strcpy(pKnights->m_strViceChief_1, CT2A(strViceChief_1));
		strcpy(pKnights->m_strViceChief_2, CT2A(strViceChief_2));
		strcpy(pKnights->m_strViceChief_3, CT2A(strViceChief_3));
		pKnights->m_nMoney = atoi(CT2A(KnightsSet.m_Gold));
		pKnights->m_sAllianceKnights = KnightsSet.m_AllianceKnights;
		pKnights->m_sMarkVersion = KnightsSet.m_MarkVersion;
		pKnights->m_sCape = KnightsSet.m_Cape;
		pKnights->m_sDomination = KnightsSet.m_Domination;
		pKnights->m_nPoints = KnightsSet.m_Points;
		pKnights->m_byGrade = GetKnightsGrade(KnightsSet.m_Points);
		pKnights->m_byRanking = KnightsSet.m_Ranking;

		for (int i = 0; i < MAX_CLAN; i++)
		{
			pKnights->m_arKnightsUser[i].byUsed = 0;
			strcpy(pKnights->m_arKnightsUser[i].strUserName, "");
		}

		if (!m_KnightsArray.PutData(pKnights->m_sIndex, pKnights))
		{
			TRACE(_T("Knights PutData Fail - %d\n"), pKnights->m_sIndex);
			delete pKnights;
			pKnights = nullptr;
		}

		KnightsSet.MoveNext();
	}

	return TRUE;
}

BOOL CEbenezerDlg::LoadAllKnightsUserData()
{
	CKnightsUserSet	KnightsSet;
	CString strUserName;
	int iFame = 0, iLevel = 0, iClass = 0;

	if (!KnightsSet.Open())
	{
		AfxMessageBox(_T("KnightsUser Open Fail!"));
		return FALSE;
	}

	if (KnightsSet.IsBOF()
		|| KnightsSet.IsEOF())
	{
		// AfxMessageBox(_T("KnightsUser Data Empty!"));
		return TRUE;
	}

	KnightsSet.MoveFirst();

	while (!KnightsSet.IsEOF())
	{
		// sungyong ,, zone server : 카루스와 전쟁존을 합치므로 인해서,,
	/*	if( m_nServerNo == KARUS )	{
			if( KnightsSet.m_sIDNum < 15000 )	{
				strUserName = KnightsSet.m_strUserID;
				strUserName.TrimRight();
				m_KnightsManager.AddKnightsUser( KnightsSet.m_sIDNum, (char*)(LPCTSTR) strUserName );
			}
		}
		else if( m_nServerNo == ELMORAD )	{	*/
	/*	if( m_nServerNo == ELMORAD )	{
			if( KnightsSet.m_sIDNum >= 15000 && KnightsSet.m_sIDNum < 30000 )	{
				strUserName = KnightsSet.m_strUserID;
				strUserName.TrimRight();
				m_KnightsManager.AddKnightsUser( KnightsSet.m_sIDNum, (char*)(LPCTSTR) strUserName );
			}
		}
		else	*/
		{
			strUserName = KnightsSet.m_strUserID;
			strUserName.TrimRight();
			m_KnightsManager.AddKnightsUser(KnightsSet.m_sIDNum, CT2A(strUserName));
		}

		KnightsSet.MoveNext();
	}

	return TRUE;
}

int CEbenezerDlg::GetKnightsAllMembers(int knightsindex, char* temp_buff, int& buff_index, int type)
{
	if (knightsindex <= 0)
		return 0;

	int count = 0;
	if (type == 0)
	{
		for (int i = 0; i < MAX_USER; i++)
		{
			CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
			if (pUser == nullptr)
				continue;

			// 같은 소속의 클랜..
			if (pUser->m_pUserData->m_bKnights == knightsindex)
			{
				SetShort(temp_buff, strlen(pUser->m_pUserData->m_id), buff_index);
				SetString(temp_buff, pUser->m_pUserData->m_id, strlen(pUser->m_pUserData->m_id), buff_index);
				SetByte(temp_buff, pUser->m_pUserData->m_bFame, buff_index);
				SetByte(temp_buff, pUser->m_pUserData->m_bLevel, buff_index);
				SetShort(temp_buff, pUser->m_pUserData->m_sClass, buff_index);
				SetByte(temp_buff, 1, buff_index);
				count++;
			}
		}
	}
	else if (type == 1)
	{
		CKnights* pKnights = m_KnightsArray.GetData(knightsindex);
		if (pKnights == nullptr)
			return 0;

		for (int i = 0; i < MAX_CLAN; i++)
		{
			if (pKnights->m_arKnightsUser[i].byUsed == 1)
			{
				// 접속중인 회원
				CUser* pUser = GetUserPtr(pKnights->m_arKnightsUser[i].strUserName, NameType::Character);
				if (pUser != nullptr)
				{
					if (pUser->m_pUserData->m_bKnights == knightsindex)
					{
						SetShort(temp_buff, strlen(pUser->m_pUserData->m_id), buff_index);
						SetString(temp_buff, pUser->m_pUserData->m_id, strlen(pUser->m_pUserData->m_id), buff_index);
						SetByte(temp_buff, pUser->m_pUserData->m_bFame, buff_index);
						SetByte(temp_buff, pUser->m_pUserData->m_bLevel, buff_index);
						SetShort(temp_buff, pUser->m_pUserData->m_sClass, buff_index);
						SetByte(temp_buff, 1, buff_index);
						count++;
					}
					// 다른존에서 탈퇴나 추방된 유저이므로 메모리에서 삭제
					else
					{
						m_KnightsManager.RemoveKnightsUser(knightsindex, pUser->m_pUserData->m_id);
					}
				}
				// 비접속중인 회원
				else
				{
					SetShort(temp_buff, strlen(pKnights->m_arKnightsUser[i].strUserName), buff_index);
					SetString(temp_buff, pKnights->m_arKnightsUser[i].strUserName, strlen(pKnights->m_arKnightsUser[i].strUserName), buff_index);
					SetByte(temp_buff, 0, buff_index);
					SetByte(temp_buff, 0, buff_index);
					SetShort(temp_buff, 0, buff_index);
					SetByte(temp_buff, 0, buff_index);
					count++;
				}

			}
		}
	}

	return count;
}

void CEbenezerDlg::MarketBBSTimeCheck()
{
	int send_index = 0;
	char send_buff[256] = {};
	float currenttime = 0.0f;

	currenttime = TimeGet();

	for (int i = 0; i < MAX_BBS_POST; i++)
	{
		// BUY!!!
		if (m_sBuyID[i] != -1)
		{
			CUser* pUser = (CUser*) m_Iocport.m_SockArray[m_sBuyID[i]];
			if (pUser == nullptr)
			{
				MarketBBSBuyDelete(i);
				continue;
			}

			if (m_fBuyStartTime[i] + BBS_CHECK_TIME < currenttime)
			{
//				if (pUser->m_pUserData->m_iGold >= BUY_POST_PRICE) {
//					pUser->m_pUserData->m_iGold -= BUY_POST_PRICE ;
//					m_fBuyStartTime[i] = TimeGet();

//					memset(send_buff, 0, sizeof(send_buff));
//					send_index = 0;	
//					SetByte( send_buff, WIZ_GOLD_CHANGE, send_index );	// Now the target
//					SetByte( send_buff, 0x02, send_index );
//					SetDWORD( send_buff, BUY_POST_PRICE, send_index );
//					SetDWORD( send_buff, pUser->m_pUserData->m_iGold, send_index );
//					pUser->Send( send_buff, send_index );	
//				}
//				else {
				MarketBBSBuyDelete(i);
//				}
			}
		}

		// SELL!!!
		if (m_sSellID[i] != -1)
		{
			CUser* pUser = (CUser*) m_Iocport.m_SockArray[m_sSellID[i]];
			if (pUser == nullptr)
			{
				MarketBBSSellDelete(i);
				continue;
			}

			if (m_fSellStartTime[i] + BBS_CHECK_TIME < currenttime)
			{
//				if (pUser->m_pUserData->m_iGold >= SELL_POST_PRICE) {
//					pUser->m_pUserData->m_iGold -= SELL_POST_PRICE ;
//					m_fSellStartTime[i] = TimeGet();

//					memset(send_buff, 0, sizeof(send_buff));
//					send_index = 0;
//					SetByte( send_buff, WIZ_GOLD_CHANGE, send_index );	// Now the target
//					SetByte( send_buff, 0x02, send_index );
//					SetDWORD( send_buff, SELL_POST_PRICE, send_index );
//					SetDWORD( send_buff, pUser->m_pUserData->m_iGold, send_index );
//					pUser->Send( send_buff, send_index );	
//				}
//				else {
				MarketBBSSellDelete(i);
//				}
			}
		}
	}
}

void CEbenezerDlg::MarketBBSBuyDelete(short index)
{
	m_sBuyID[index] = -1;
	memset(m_strBuyTitle[index], 0, sizeof(m_strBuyTitle[index]));
	memset(m_strBuyMessage[index], 0, sizeof(m_strBuyMessage[index]));
	m_iBuyPrice[index] = 0;
	m_fBuyStartTime[index] = 0.0f;
}

void CEbenezerDlg::MarketBBSSellDelete(short index)
{
	m_sSellID[index] = -1;
	memset(m_strSellTitle[index], 0, sizeof(m_strSellTitle[index]));
	memset(m_strSellMessage[index], 0, sizeof(m_strSellMessage[index]));
	m_iSellPrice[index] = 0;
	m_fSellStartTime[index] = 0.0f;
}

void CEbenezerDlg::WritePacketLog()
{
	CTime cur = CTime::GetCurrentTime();
	CString starttime;
	starttime.Format(_T("* Packet Check : send=%d, realsend=%d, recv=%d, time %d:%d분\r\n"), m_iPacketCount, m_iSendPacketCount, m_iRecvPacketCount, cur.GetHour(), cur.GetMinute());
	LogFileWrite(starttime);
}

int CEbenezerDlg::GetKnightsGrade(int nPoints)
{
	// 클랜등급 = 클랜원 국가 기여도의 총합 / 24
	int nClanPoints = nPoints / 24;

	int nGrade = 5;
	if (nClanPoints >= 0
		&& nClanPoints < 2000)
		nGrade = 5;
	else if (nClanPoints >= 2000
		&& nClanPoints < 5000)
		nGrade = 4;
	else if (nClanPoints >= 5000
		&& nClanPoints < 10000)
		nGrade = 3;
	else if (nClanPoints >= 10000
		&& nClanPoints < 20000)
		nGrade = 2;
	else if (nClanPoints >= 20000)
		nGrade = 1;

	return nGrade;
}

void CEbenezerDlg::CheckAliveUser()
{
	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		if (pUser->GetState() == STATE_GAMESTART)
		{
			if (pUser->m_sAliveCount > 3)
			{
				pUser->Close();

				TCHAR logstr[1024] = {};
				_stprintf(logstr, _T("User Alive Close - (%d) %hs\r\n"), pUser->GetSocketID(), pUser->m_pUserData->m_id);
				LogFileWrite(logstr);
			}

			pUser->m_sAliveCount++;
		}
	}
}

void CEbenezerDlg::KickOutAllUsers()
{
	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		pUser->Close();
		Sleep(1000);
	}
}

int64_t CEbenezerDlg::GenerateItemSerial()
{
	MYINT64 serial;
	MYSHORT	increase;
	serial.i = 0;
	increase.w = 0;

	CTime t = CTime::GetCurrentTime();

	EnterCriticalSection(&g_serial_critical);

	increase.w = g_increase_serial++;

	serial.b[7] = (BYTE) m_nServerNo;
	serial.b[6] = (BYTE) (t.GetYear() % 100);
	serial.b[5] = (BYTE) t.GetMonth();
	serial.b[4] = (BYTE) t.GetDay();
	serial.b[3] = (BYTE) t.GetHour();
	serial.b[2] = (BYTE) t.GetMinute();
	serial.b[1] = increase.b[1];
	serial.b[0] = increase.b[0];

	LeaveCriticalSection(&g_serial_critical);

//	TRACE(_T("Generate Item Serial : %I64d\n"), serial.i);
	return serial.i;
}

void CEbenezerDlg::KickOutZoneUsers(short zone)
{
	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pTUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pTUser == nullptr)
			continue;

		// Only kick out users in requested zone.
		if (pTUser->m_pUserData->m_bZone == zone)
		{
			C3DMap* pMap = GetMapByID(pTUser->m_pUserData->m_bNation);
			if (pMap != nullptr)
				pTUser->ZoneChange(pMap->m_nZoneNumber, pMap->m_fInitX, pMap->m_fInitZ);	// Move user to native zone.
		}
	}
}

void CEbenezerDlg::Send_UDP_All(char* pBuf, int len, int group_type)
{
	std::map<int, _ZONE_SERVERINFO*>::iterator Iter1;
	std::map<int, _ZONE_SERVERINFO*>::iterator Iter2;

	int server_number = 0;

	if (group_type == 0)
	{
		Iter1 = m_ServerArray.begin();
		Iter2 = m_ServerArray.end();
		server_number = m_nServerNo;
	}
	else
	{
		Iter1 = m_ServerGroupArray.begin();
		Iter2 = m_ServerGroupArray.end();
		server_number = m_nServerGroupNo;
	}

	for (; Iter1 != Iter2; Iter1++)
	{
		_ZONE_SERVERINFO* pInfo = (*Iter1).second;
		if (pInfo != nullptr
			&& pInfo->sServerNo != server_number)
			m_pUdpSocket->SendUDPPacket(pInfo->strServerIP, pBuf, len);
	}
}

BOOL CEbenezerDlg::LoadBattleTable()
{
	CBattleSet BattleSet;

	if (!BattleSet.Open())
	{
		AfxMessageBox(_T("BattleSet Data Open Fail!"));
		return FALSE;
	}

	if (BattleSet.IsBOF()
		|| BattleSet.IsEOF())
	{
		AfxMessageBox(_T("BattleSet Data Empty!"));
		return FALSE;
	}

	BattleSet.MoveFirst();

	while (!BattleSet.IsEOF())
	{
		m_byOldVictory = BattleSet.m_byNation;
		BattleSet.MoveNext();
	}

	return TRUE;
}

void CEbenezerDlg::Send_CommandChat(char* pBuf, int len, int nation, CUser* pExceptUser)
{
	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		// if (pUser == pExceptUser)
		//	continue;

		if (pUser->GetState() == STATE_GAMESTART
			&& pUser->m_pUserData->m_bNation == nation)
			pUser->Send(pBuf, len);
	}
}

BOOL CEbenezerDlg::LoadKnightsRankTable()
{
	CKnightsRankSet	KRankSet;
	int nRank = 0, nKnightsIndex = 0, nKaursRank = 0, nElmoRank = 0, nFindKarus = 0, nFindElmo = 0, send_index = 0, temp_index = 0;
	CKnights* pKnights = nullptr;
	CUser* pUser = nullptr;
	CString strKnightsName;

	std::string buff;

	char send_buff[1024] = {},
		temp_buff[1024] = {},
		strKarusCaptainName[1024] = {},
		strElmoCaptainName[1024] = {},
		strKarusCaptain[5][50] = {},
		strElmoCaptain[5][50] = {};
	for (int i = 0; i < 5; i++)
	{
		memset(strKarusCaptain[i], 0, sizeof(strKarusCaptain[i]));
		memset(strElmoCaptain[i], 0, sizeof(strElmoCaptain[i]));
	}

	if (!KRankSet.Open())
	{
		TRACE(_T("### KnightsRankTable Open Fail! ###\n"));
		return TRUE;
	}

	if (KRankSet.IsBOF()
		|| KRankSet.IsEOF())
	{
		TRACE(_T("### KnightsRankTable Empty! ###\n"));
		return TRUE;
	}

	KRankSet.MoveFirst();

	while (!KRankSet.IsEOF())
	{
		nRank = KRankSet.m_nRank;
		nKnightsIndex = KRankSet.m_shIndex;
		pKnights = m_KnightsArray.GetData(nKnightsIndex);
		strKnightsName = KRankSet.m_strName;
		strKnightsName.TrimRight();

		if (pKnights == nullptr)
		{
			KRankSet.MoveNext();
			continue;
		}

		if (pKnights->m_byNation == KARUS)
		{
			//if (nKaursRank == 5 || nFindKarus == 1)
			if (nKaursRank == 5)
			{
				KRankSet.MoveNext();
				continue;			// 5위까지 클랜장이 없으면 대장은 없음			
			}

			//nKaursRank++;

			pUser = GetUserPtr(pKnights->m_strChief, NameType::Character);
			if (pUser == nullptr)
			{
				KRankSet.MoveNext();
				continue;
			}

			if (pUser->m_pUserData->m_bZone != ZONE_BATTLE)
			{
				KRankSet.MoveNext();
				continue;
			}

			if (pUser->m_pUserData->m_bKnights == nKnightsIndex)
			{
				pUser->m_pUserData->m_bFame = COMMAND_CAPTAIN;
				sprintf(strKarusCaptain[nKaursRank], "[%ls][%s]", strKnightsName.GetString(), pUser->m_pUserData->m_id);
				nKaursRank++;

				nFindKarus = 1;
				memset(send_buff, 0, sizeof(send_buff));
				send_index = 0;
				SetByte(send_buff, WIZ_AUTHORITY_CHANGE, send_index);
				SetByte(send_buff, COMMAND_AUTHORITY, send_index);
				SetShort(send_buff, pUser->GetSocketID(), send_index);
				SetByte(send_buff, pUser->m_pUserData->m_bFame, send_index);
				//pUser->Send( send_buff, send_index );
				Send_Region(send_buff, send_index, pUser->m_pUserData->m_bZone, pUser->m_RegionX, pUser->m_RegionZ);

				//strcpy( m_strKarusCaptain, pUser->m_pUserData->m_id );
				//Announcement( KARUS_CAPTAIN_NOTIFY, KARUS );
				//TRACE(_T("Karus Captain - %hs, rank=%d, index=%d\n"), pUser->m_pUserData->m_id, nRank, nKnightsIndex);
			}
		}
		else if (pKnights->m_byNation == ELMORAD)
		{
			//if (nElmoRank == 5 || nFindElmo == 1)
			if (nElmoRank == 5)
			{
				KRankSet.MoveNext();
				continue;			// 5위까지 클랜장이 없으면 대장은 없음			
			}

			//nElmoRank++;

			pUser = GetUserPtr(pKnights->m_strChief, NameType::Character);
			if (pUser == nullptr)
			{
				KRankSet.MoveNext();
				continue;
			}

			if (pUser->m_pUserData->m_bZone != ZONE_BATTLE)
			{
				KRankSet.MoveNext();
				continue;
			}

			if (pUser->m_pUserData->m_bKnights == nKnightsIndex)
			{
				pUser->m_pUserData->m_bFame = COMMAND_CAPTAIN;
				sprintf(strElmoCaptain[nElmoRank], "[%ls][%s]", strKnightsName.GetString(), pUser->m_pUserData->m_id);
				nFindElmo = 1;
				nElmoRank++;

				memset(send_buff, 0, sizeof(send_buff));
				send_index = 0;
				SetByte(send_buff, WIZ_AUTHORITY_CHANGE, send_index);
				SetByte(send_buff, COMMAND_AUTHORITY, send_index);
				SetShort(send_buff, pUser->GetSocketID(), send_index);
				SetByte(send_buff, pUser->m_pUserData->m_bFame, send_index);
				//pUser->Send( send_buff, send_index );
				Send_Region(send_buff, send_index, pUser->m_pUserData->m_bZone, pUser->m_RegionX, pUser->m_RegionZ);

				//strcpy( m_strElmoradCaptain, pUser->m_pUserData->m_id );
				//Announcement( ELMORAD_CAPTAIN_NOTIFY, ELMORAD );
				//TRACE(_T("Elmo Captain - %hs, rank=%d, index=%d\n"), pUser->m_pUserData->m_id, nRank, nKnightsIndex);
			}
		}

		KRankSet.MoveNext();
	}

	::_LoadStringFromResource(IDS_KARUS_CAPTAIN, buff);
	sprintf(strKarusCaptainName, buff.c_str(), strKarusCaptain[0], strKarusCaptain[1], strKarusCaptain[2], strKarusCaptain[3], strKarusCaptain[4]);

	::_LoadStringFromResource(IDS_ELMO_CAPTAIN, buff);
	sprintf(strElmoCaptainName, buff.c_str(), strElmoCaptain[0], strElmoCaptain[1], strElmoCaptain[2], strElmoCaptain[3], strElmoCaptain[4]);

	//sprintf( strKarusCaptainName, "카루스의 지휘관은 %s, %s, %s, %s, %s 입니다", strKarusCaptain[0], strKarusCaptain[1], strKarusCaptain[2], strKarusCaptain[3], strKarusCaptain[4]);
	//sprintf( strElmoCaptainName, "엘모라드의 지휘관은 %s, %s, %s, %s, %s 입니다", strKarusCaptain[0], strKarusCaptain[1], strKarusCaptain[2], strKarusCaptain[3], strKarusCaptain[4]);
	TRACE(_T("LoadKnightsRankTable Success\n"));

	memset(send_buff, 0, sizeof(send_buff));
	send_index = 0;
	SetByte(send_buff, WIZ_CHAT, send_index);
	SetByte(send_buff, WAR_SYSTEM_CHAT, send_index);
	SetByte(send_buff, 1, send_index);
	SetShort(send_buff, -1, send_index);
	SetByte(send_buff, 0, send_index);			// sender name length
	SetString2(send_buff, strKarusCaptainName, static_cast<short>(strlen(strKarusCaptainName)), send_index);

	SetByte(temp_buff, WIZ_CHAT, temp_index);
	SetByte(temp_buff, WAR_SYSTEM_CHAT, temp_index);
	SetByte(temp_buff, 1, temp_index);
	SetShort(temp_buff, -1, temp_index);
	SetByte(temp_buff, 0, send_index);			// sender name length
	SetString2(temp_buff, strElmoCaptainName, static_cast<short>(strlen(strElmoCaptainName)), temp_index);

	for (int i = 0; i < MAX_USER; i++)
	{
		pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		if (pUser->GetState() == STATE_GAMESTART)
		{
			if (pUser->m_pUserData->m_bNation == KARUS)
				pUser->Send(send_buff, send_index);
			else if (pUser->m_pUserData->m_bNation == ELMORAD)
				pUser->Send(temp_buff, temp_index);
		}
	}

	return TRUE;
}

void CEbenezerDlg::BattleZoneCurrentUsers()
{
	C3DMap* pMap = GetMapByID(ZONE_BATTLE);
	if (pMap == nullptr)
		return;

	// 현재의 서버가 배틀존 서버가 아니라면 리턴
	if (m_nServerNo != pMap->m_nServerNo)
		return;

	char send_buff[128] = {};
	int nKarusMan = 0, nElmoradMan = 0, send_index = 0;

	for (int i = 0; i < MAX_USER; i++)
	{
		CUser* pUser = (CUser*) m_Iocport.m_SockArray[i];
		if (pUser == nullptr)
			continue;

		if (pUser->m_pUserData->m_bZone == ZONE_BATTLE)
		{
			if (pUser->m_pUserData->m_bNation == KARUS)
				nKarusMan++;
			else if (pUser->m_pUserData->m_bNation == ELMORAD)
				nElmoradMan++;
		}
	}

	m_sKarusCount = nKarusMan;
	m_sElmoradCount = nElmoradMan;

	//TRACE(_T("---> BattleZoneCurrentUsers - karus=%d, elmorad=%d\n"), m_sKarusCount, m_sElmoradCount);

	SetByte(send_buff, UDP_BATTLEZONE_CURRENT_USERS, send_index);
	SetShort(send_buff, m_sKarusCount, send_index);
	SetShort(send_buff, m_sElmoradCount, send_index);
	Send_UDP_All(send_buff, send_index);
}

void CEbenezerDlg::FlySanta()
{
	int send_index = 0;
	char send_buff[128] = {};

	SetByte(send_buff, WIZ_SANTA, send_index);
	Send_All(send_buff, send_index);
}

void CEbenezerDlg::WriteEventLog(char* pBuf)
{
	TCHAR strLog[256] = {};
	CTime t = CTime::GetCurrentTime();
	_stprintf(strLog, _T("%d:%d-%d : %hs \r\n"), t.GetHour(), t.GetMinute(), t.GetSecond(), pBuf);
	EnterCriticalSection(&g_LogFile_critical);
	m_EvnetLogFile.Write(strLog, _tcslen(strLog));
	LeaveCriticalSection(&g_LogFile_critical);
}

C3DMap* CEbenezerDlg::GetMapByIndex(int iZoneIndex) const
{
	if (iZoneIndex < 0
		|| iZoneIndex >= static_cast<int>(m_ZoneArray.size()))
		return nullptr;

	return m_ZoneArray[iZoneIndex];
}

C3DMap* CEbenezerDlg::GetMapByID(int iZoneID) const
{
	for (C3DMap* pMap : m_ZoneArray)
	{
		if (pMap != nullptr
			&& pMap->m_nZoneNumber == iZoneID)
			return pMap;
	}

	return nullptr;
}

CString CEbenezerDlg::GetGameDBConnectionString()
{
	CString strConnection;
	strConnection.Format(
		_T("ODBC;DSN=%s;UID=%s;PWD=%s"),
		m_strGameDSN,
		m_strGameUID,
		m_strGamePWD);
	return strConnection;
}
