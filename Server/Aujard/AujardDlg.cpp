﻿// AujardDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Aujard.h"
#include "AujardDlg.h"

#include <process.h>

#include <shared/Ini.h>

#include <db-library/DatabaseConnManager.h>
#include <db-library/STLMapRecordSetLoader.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define PROCESS_CHECK		100
#define CONCURRENT_CHECK	200
#define SERIAL_TIME			300
#define PACKET_CHECK		400

import AujardBinder;

using namespace db;

WORD g_increase_serial = 50001;

CRITICAL_SECTION g_LogFileWrite;

CAujardDlg* CAujardDlg::s_pInstance = nullptr;

DWORD WINAPI ReadQueueThread(LPVOID lp)
{
	CAujardDlg* pMain = (CAujardDlg*) lp;
	int recvlen = 0, index = 0;
	BYTE command;
	char recv_buff[1024] = {};

	while (TRUE)
	{
		if (pMain->m_LoggerRecvQueue.GetFrontMode() != R)
		{
			index = 0;
			recvlen = pMain->m_LoggerRecvQueue.GetData(recv_buff);
			if (recvlen > MAX_PKTSIZE)
			{
				Sleep(1);
				continue;
			}

			command = GetByte(recv_buff, index);
			switch (command)
			{
				case WIZ_LOGIN:
					pMain->AccountLogIn(recv_buff + index);
					break;

				case WIZ_NEW_CHAR:
					pMain->CreateNewChar(recv_buff + index);
					break;

				case WIZ_DEL_CHAR:
					pMain->DeleteChar(recv_buff + index);
					break;

				case WIZ_SEL_CHAR:
					pMain->SelectCharacter(recv_buff + index);
					break;

				case WIZ_SEL_NATION:
					pMain->SelectNation(recv_buff + index);
					break;

				case WIZ_ALLCHAR_INFO_REQ:
					pMain->AllCharInfoReq(recv_buff + index);
					break;

				case WIZ_LOGOUT:
					pMain->UserLogOut(recv_buff + index);
					break;

				case WIZ_DATASAVE:
					pMain->UserDataSave(recv_buff + index);
					break;

				case WIZ_KNIGHTS_PROCESS:
					pMain->KnightsPacket(recv_buff + index);
					break;

				case WIZ_CLAN_PROCESS:
					pMain->KnightsPacket(recv_buff + index);
					break;

				case WIZ_LOGIN_INFO:
					pMain->SetLogInInfo(recv_buff + index);
					break;

				case WIZ_KICKOUT:
					pMain->UserKickOut(recv_buff + index);
					break;

				case WIZ_BATTLE_EVENT:
					pMain->BattleEventResult(recv_buff + index);
					break;

				case DB_COUPON_EVENT:
					pMain->CouponEvent(recv_buff + index);
					break;
			}

			recvlen = 0;
			memset(recv_buff, 0, sizeof(recv_buff));
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
// CAujardDlg dialog

CAujardDlg::CAujardDlg(CWnd* pParent /*=nullptr*/)
	: CDialog(CAujardDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAujardDlg)
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	memset(m_strGameDSN, 0, sizeof(m_strGameDSN));
	memset(m_strGameUID, 0, sizeof(m_strGameUID));
	memset(m_strGamePWD, 0, sizeof(m_strGamePWD));
	memset(m_strAccountDSN, 0, sizeof(m_strAccountDSN));
	memset(m_strAccountUID, 0, sizeof(m_strAccountUID));
	memset(m_strAccountPWD, 0, sizeof(m_strAccountPWD));

	m_iSendPacketCount = 0;
	m_iPacketCount = 0;
	m_iRecvPacketCount = 0;

	DatabaseConnManager::Create();
}

CAujardDlg::~CAujardDlg()
{
	DatabaseConnManager::Destroy();
}

void CAujardDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAujardDlg)
	DDX_Control(pDX, IDC_OUT_LIST, m_OutputList);
	DDX_Text(pDX, IDC_DB_PROCESS, m_DBProcessNum);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAujardDlg, CDialog)
	//{{AFX_MSG_MAP(CAujardDlg)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_TIMER()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CAujardDlg message handlers

BOOL CAujardDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	s_pInstance = this;

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	//----------------------------------------------------------------------
	//	Logfile initialize
	//----------------------------------------------------------------------
	CTime time = CTime::GetCurrentTime();
	TCHAR strLogFile[50] = {};
	wsprintf(strLogFile, _T("AujardLog-%d-%d-%d.txt"), time.GetYear(), time.GetMonth(), time.GetDay());
	m_LogFile.Open(strLogFile, CFile::modeWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone);
	m_LogFile.SeekToEnd();

	m_iLogFileDay = time.GetDay();

	InitializeCriticalSection(&g_LogFileWrite);

	m_LoggerRecvQueue.InitailizeMMF(MAX_PKTSIZE, MAX_COUNT, _T(SMQ_LOGGERSEND), FALSE);	// Dispatcher 의 Send Queue
	m_LoggerSendQueue.InitailizeMMF(MAX_PKTSIZE, MAX_COUNT, _T(SMQ_LOGGERRECV), FALSE);	// Dispatcher 의 Read Queue

	if (!InitializeMMF())
	{
		AfxMessageBox(_T("Main Shared Memory Initialize Fail"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	std::filesystem::path iniPath(GetProgPath().GetString());
	iniPath /= L"Aujard.ini";

	CIni ini(iniPath);

	// TODO: This should be Knight_Account
	ini.GetString(_T("ODBC"), _T("ACCOUNT_DSN"), _T("KN_online"), m_strAccountDSN, _countof(m_strAccountDSN));
	ini.GetString(_T("ODBC"), _T("ACCOUNT_UID"), _T("knight"), m_strAccountUID, _countof(m_strAccountUID));
	ini.GetString(_T("ODBC"), _T("ACCOUNT_PWD"), _T("knight"), m_strAccountPWD, _countof(m_strAccountPWD));

	ini.GetString(_T("ODBC"), _T("GAME_DSN"), _T("KN_online"), m_strGameDSN, _countof(m_strGameDSN));
	ini.GetString(_T("ODBC"), _T("GAME_UID"), _T("knight"), m_strGameUID, _countof(m_strGameUID));
	ini.GetString(_T("ODBC"), _T("GAME_PWD"), _T("knight"), m_strGamePWD, _countof(m_strGamePWD));

	// TODO: This should only fetch the once.
	// The above won't be necessary after stored procedures are replaced, so it can be replaced then.
	std::string datasourceName, datasourceUser, datasourcePass;

	// TODO: This should be Knight_Account
	datasourceName = ini.GetString("ODBC", "ACCOUNT_DSN", "KN_online");
	datasourceUser = ini.GetString("ODBC", "ACCOUNT_UID", "knight");
	datasourcePass = ini.GetString("ODBC", "ACCOUNT_PWD", "knight");

	DatabaseConnManager::SetDatasourceConfig(
		modelUtil::ACCOUNT,
		datasourceName, datasourceUser, datasourcePass);

	datasourceName = ini.GetString("ODBC", "GAME_DSN", "KN_online");
	datasourceUser = ini.GetString("ODBC", "GAME_UID", "knight");
	datasourcePass = ini.GetString("ODBC", "GAME_PWD", "knight");

	DatabaseConnManager::SetDatasourceConfig(
		modelUtil::GAME,
		datasourceName, datasourceUser, datasourcePass);

	m_nServerNo = ini.GetInt(_T("ZONE_INFO"), _T("GROUP_INFO"), 1);
	m_nZoneNo = ini.GetInt(_T("ZONE_INFO"), _T("ZONE_INFO"), 1);

	// Trigger a save to flush defaults to file.
	ini.Save();

	if (!m_DBAgent.DatabaseInit())
	{
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!LoadItemTable())
	{
		AfxMessageBox(_T("Load ItemTable Fail!!"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	SetTimer(PROCESS_CHECK, 40000, nullptr);
	SetTimer(CONCURRENT_CHECK, 300000, nullptr);
//	SetTimer( SERIAL_TIME, 60000, nullptr );
	SetTimer(PACKET_CHECK, 120000, nullptr);

	DWORD id;
	m_hReadQueueThread = ::CreateThread(nullptr, 0, ReadQueueThread, this, 0, &id);

	CTime cur = CTime::GetCurrentTime();
	CString starttime;
	starttime.Format(_T("Aujard Start : %d월 %d일 %d시 %d분\r\n"), cur.GetMonth(), cur.GetDay(), cur.GetHour(), cur.GetMinute());
	m_LogFile.Write(starttime, starttime.GetLength());

	return TRUE;  // return TRUE  unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CAujardDlg::OnPaint()
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
HCURSOR CAujardDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

BOOL CAujardDlg::DestroyWindow()
{
	KillTimer(PROCESS_CHECK);
	KillTimer(CONCURRENT_CHECK);
//	KillTimer(SERIAL_TIME);
	KillTimer(PACKET_CHECK);

	if (m_hReadQueueThread != nullptr)
		::TerminateThread(m_hReadQueueThread, 0);

	if (!m_ItemtableArray.IsEmpty())
		m_ItemtableArray.DeleteAllData();

	if (m_LogFile.m_hFile != CFile::hFileNull)
		m_LogFile.Close();

	DeleteCriticalSection(&g_LogFileWrite);

	s_pInstance = nullptr;

	return CDialog::DestroyWindow();
}

BOOL CAujardDlg::InitializeMMF()
{
	CString logstr;

	DWORD filesize = MAX_USER * ALLOCATED_USER_DATA_BLOCK;

	m_hMMFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, TRUE, _T("KNIGHT_DB"));
	if (m_hMMFile == nullptr)
	{
		logstr = _T("Shared Memory Load Fail!!");
		m_hMMFile = INVALID_HANDLE_VALUE;
		return FALSE;
	}

	logstr = _T("Shared Memory Load Success!!");
	m_OutputList.AddString(logstr);

	m_lpMMFile = (char*) MapViewOfFile(m_hMMFile, FILE_MAP_WRITE, 0, 0, 0);
	if (m_lpMMFile == nullptr)
		return FALSE;

	m_DBAgent.m_UserDataArray.reserve(MAX_USER);

	for (int i = 0; i < MAX_USER; i++)
	{
		_USER_DATA* pUser = (_USER_DATA*) (m_lpMMFile + i * ALLOCATED_USER_DATA_BLOCK);
		m_DBAgent.m_UserDataArray.push_back(pUser);
	}

	return TRUE;
}

BOOL CAujardDlg::LoadItemTable()
{
	STLMapRecordSetLoader::Error err = {};
	if (!STLMapRecordSetLoader::LoadForbidEmpty(m_ItemtableArray, err))
	{
		ReportTableLoadError(err, __func__);
		return FALSE;
	}

	return TRUE;
}

void CAujardDlg::ReportTableLoadError(const STLMapRecordSetLoader::Error& err, const char* source)
{
	CString msg;
	msg.Format(_T("%hs failed: %hs"), source, err.Message.c_str());
	AfxMessageBox(msg);
}

void CAujardDlg::SelectCharacter(char* pBuf)
{
	int index = 0, uid = -1, send_index = 0, idlen1 = 0, idlen2 = 0, t_uid = -1, count = 0, packetindex = 0;
	BYTE bInit = 0x01;
	char send_buff[256] = {},
		accountid[MAX_ID_SIZE + 1] = {},
		userid[MAX_ID_SIZE + 1] = {};

	_USER_DATA* pUser = nullptr;

	uid = GetShort(pBuf, index);
	idlen1 = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen1, index);
	idlen2 = GetShort(pBuf, index);
	GetString(userid, pBuf, idlen2, index);
	bInit = GetByte(pBuf, index);
	packetindex = GetDWORD(pBuf, index);

	CTime t = CTime::GetCurrentTime();
	char logstr[256]; memset(logstr, 0x00, 256);
	sprintf(logstr, "SelectCharacter : acname=%s, name=%s, index=%d, pid : %d, front : %d\r\n", accountid, userid, packetindex, _getpid(), m_LoggerRecvQueue.GetFrontPointer());
	WriteLogFile(logstr);
	//m_LogFile.Write(logstr, strlen(logstr));

	m_iRecvPacketCount++;		// packet count

	if (uid < 0
		|| uid >= MAX_USER)
		goto fail_return;

	if (strlen(accountid) == 0)
		goto fail_return;

	if (strlen(userid) == 0)
		goto fail_return;

	if (GetUserPtr(userid, t_uid) != nullptr)
	{
		SetShort(send_buff, t_uid, send_index);
		SetShort(send_buff, idlen1, send_index);
		SetString(send_buff, accountid, idlen1, send_index);
		SetShort(send_buff, idlen2, send_index);
		SetString(send_buff, userid, idlen2, send_index);
		UserLogOut(send_buff);
		return;
	}

	if (!m_DBAgent.LoadUserData(accountid, userid, uid))
		goto fail_return;

	if (!m_DBAgent.LoadWarehouseData(accountid, uid))
		goto fail_return;

	pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[uid];
	if (pUser == nullptr)
		goto fail_return;

	strcpy(pUser->m_Accountid, accountid);

	SetByte(send_buff, WIZ_SEL_CHAR, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, 0x01, send_index);
	SetByte(send_buff, bInit, send_index);

	m_iPacketCount++;		// packet count

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
		{
			m_iSendPacketCount++;
			break;
		}

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Sel char Packet Drop!!!"));

	return;

fail_return:
	send_index = 0;
	SetByte(send_buff, WIZ_SEL_CHAR, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, 0x00, send_index);

	m_LoggerSendQueue.PutData(send_buff, send_index);
}

void CAujardDlg::UserLogOut(char* pBuf)
{
	int index = 0, uid = -1, idlen1 = 0, idlen2 = 0, send_index = 0, count = 0;
	int retval_1 = 0, retval_2 = 0, retval_3 = 1;
	char userid[MAX_ID_SIZE + 1] = {},
		accountid[MAX_ID_SIZE + 1] = {},
		send_buff[256] = {},
		logstr[256] = {};
	_USER_DATA* pUser = nullptr;

	uid = GetShort(pBuf, index);
	idlen1 = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen1, index);
	idlen2 = GetShort(pBuf, index);
	GetString(userid, pBuf, idlen2, index);

	if (uid < 0
		|| uid >= MAX_USER)
		return;

	if (strlen(userid) == 0)
		return;

	pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[uid];
	if (pUser == nullptr)
		return;

	retval_2 = m_DBAgent.UpdateUser(userid, uid, UPDATE_LOGOUT);
	Sleep(10);
	retval_1 = m_DBAgent.UpdateWarehouseData(accountid, uid, UPDATE_LOGOUT);

	if (pUser->m_bLogout != 2)	// zone change logout
		retval_3 = m_DBAgent.AccountLogout(accountid);

	CTime t = CTime::GetCurrentTime();
	sprintf(logstr, "Logout : %s, %s (W:%d,U:%d)\r\n", accountid, userid, retval_1, retval_2);
	WriteLogFile(logstr);
	//m_LogFile.Write(logstr, strlen(logstr));

	if (retval_2 == 0
		|| retval_3 != 1)
	{
		sprintf(logstr, "Invalid Logout : %s, %s (W:%d,U:%d,A:%d) \r\n", accountid, userid, retval_1, retval_2, retval_3);
		WriteLogFile(logstr);
		//m_LogFile.Write(logstr, strlen(logstr));
	}

	if (!retval_1
		|| !retval_2)
	{
		do
		{
			retval_2 = m_DBAgent.UpdateUser(userid, uid, UPDATE_LOGOUT);
			Sleep(10);
			retval_1 = m_DBAgent.UpdateWarehouseData(accountid, uid, UPDATE_LOGOUT);

			if (retval_1 == TRUE && retval_2 == TRUE)
				break;
			count++;
		}
		while (count < 10);

		if (count >= 10)
		{
			sprintf(logstr, "Logout Save Error: %s, %s (W:%d,U:%d) \r\n", accountid, userid, retval_1, retval_2);
			WriteLogFile(logstr);
			//m_LogFile.Write(logstr, strlen(logstr));
		}
	}

	count = 0;

	do
	{
		if (retval_1 = m_DBAgent.CheckUserData(accountid, userid, 1, pUser->m_dwTime, pUser->m_iBank))
			break;

		m_DBAgent.UpdateWarehouseData(accountid, uid, UPDATE_LOGOUT);	// retry
		count++;
		Sleep(10);
	}
	while (count < 10);

	if (count >= 10)
	{
		sprintf(logstr, "WarehouseData Logout Check Error: %s, %s (W:%d) \r\n", accountid, userid, retval_1);
		WriteLogFile(logstr);
		//m_LogFile.Write(logstr, strlen(logstr));
	}

	count = 0;

	do
	{
		if (retval_1 = m_DBAgent.CheckUserData(accountid, userid, 2, pUser->m_dwTime, pUser->m_iExp))
			break;

		m_DBAgent.UpdateUser(userid, uid, UPDATE_LOGOUT);	// retry
		count++;
		Sleep(10);
	}
	while (count < 10);

	if (count >= 10)
	{
		sprintf(logstr, "UserDataLogout Check Error: %s, %s (W:%d) \r\n", accountid, userid, retval_1);
		WriteLogFile(logstr);
		//m_LogFile.Write(logstr, strlen(logstr));
	}

	m_DBAgent.MUserInit(uid);

	SetByte(send_buff, WIZ_LOGOUT, send_index);
	SetShort(send_buff, uid, send_index);

	count = 0;
	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Logout Packet Drop!!!"));
}

void CAujardDlg::AccountLogIn(char* pBuf)
{
	int index = 0, uid = -1, idlen = 0, pwdlen = 0, send_index = 0, count = 0;
	int nation = -1;

	char accountid[MAX_ID_SIZE + 1] = {},
		password[MAX_PW_SIZE + 1] = {},
		send_buff[256] = {};

	uid = GetShort(pBuf, index);
	idlen = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen, index);
	pwdlen = GetShort(pBuf, index);
	GetString(password, pBuf, pwdlen, index);

	nation = m_DBAgent.AccountLogInReq(accountid, password);

	SetByte(send_buff, WIZ_LOGIN, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, nation, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Login Packet Drop!!!"));
}

void CAujardDlg::SelectNation(char* pBuf)
{
	int index = 0, uid = -1, idlen = 0, send_index = 0, count = 0;
	int nation = -1;
	BYTE result;
	char accountid[MAX_ID_SIZE + 1] = {},
		password[MAX_PW_SIZE + 1] = {},
		send_buff[256] = {};

	uid = GetShort(pBuf, index);
	idlen = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen, index);
	nation = GetByte(pBuf, index);

	result = m_DBAgent.NationSelect(accountid, nation);

	SetByte(send_buff, WIZ_SEL_NATION, send_index);
	SetShort(send_buff, uid, send_index);

	if (result != 0)
		SetByte(send_buff, nation, send_index);
	else
		SetByte(send_buff, 0x00, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Sel Nation Packet Drop!!!"));
}

void CAujardDlg::CreateNewChar(char* pBuf)
{
	int index = 0, uid = -1, idlen = 0, charidlen = 0, send_index = 0, result = 0, count = 0;
	int charindex = 0, race = 0, Class = 0, hair = 0, face = 0, str = 0, sta = 0, dex = 0, intel = 0, cha = 0;
	char accountid[MAX_ID_SIZE + 1] = {},
		charid[MAX_ID_SIZE + 1] = {},
		send_buff[256] = {};

	uid = GetShort(pBuf, index);
	idlen = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen, index);
	charindex = GetByte(pBuf, index);
	charidlen = GetShort(pBuf, index);
	GetString(charid, pBuf, charidlen, index);
	race = GetByte(pBuf, index);
	Class = GetShort(pBuf, index);
	face = GetByte(pBuf, index);
	hair = GetByte(pBuf, index);
	str = GetByte(pBuf, index);
	sta = GetByte(pBuf, index);
	dex = GetByte(pBuf, index);
	intel = GetByte(pBuf, index);
	cha = GetByte(pBuf, index);

	result = m_DBAgent.CreateNewChar(accountid, charindex, charid, race, Class, hair, face, str, sta, dex, intel, cha);

	SetByte(send_buff, WIZ_NEW_CHAR, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, result, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("New Char Packet Drop!!!"));
}

void CAujardDlg::DeleteChar(char* pBuf)
{
	int index = 0, uid = -1, idlen = 0, charidlen = 0, send_index = 0, result = 0, count = 0;
	int charindex = 0, soclen = 0;
	char accountid[MAX_ID_SIZE + 1] = {},
		charid[MAX_ID_SIZE + 1] = {},
		socno[15] = {},
		send_buff[256] = {};

	uid = GetShort(pBuf, index);
	idlen = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen, index);
	charindex = GetByte(pBuf, index);
	charidlen = GetShort(pBuf, index);
	GetString(charid, pBuf, charidlen, index);
	soclen = GetShort(pBuf, index);
	GetString(socno, pBuf, soclen, index);

	result = m_DBAgent.DeleteChar(charindex, accountid, charid, socno);

	TRACE(_T("*** DeleteChar == charid=%hs, socno=%hs ***\n"), charid, socno);

	SetByte(send_buff, WIZ_DEL_CHAR, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, result, send_index);
	if (result > 0)
		SetByte(send_buff, charindex, send_index);
	else
		SetByte(send_buff, 0xFF, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Del Char Packet Drop!!!"));
}

void CAujardDlg::AllCharInfoReq(char* pBuf)
{
	int index = 0, uid = 0, idlen = 0, send_index = 0, buff_index = 0, count = 0;
	char accountid[MAX_ID_SIZE + 1] = {},
		send_buff[1024] = {},
		buff[1024] = {},
		charid1[MAX_ID_SIZE + 1] = {},
		charid2[MAX_ID_SIZE + 1] = {},
		charid3[MAX_ID_SIZE + 1] = {},
		charid4[MAX_ID_SIZE + 1] = {},
		charid5[MAX_ID_SIZE + 1] = {};

	uid = GetShort(pBuf, index);
	idlen = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen, index);

	SetByte(buff, 0x01, buff_index);	// result

	m_DBAgent.GetAllCharID(accountid, charid1, charid2, charid3, charid4, charid5);
	m_DBAgent.LoadCharInfo(charid1, buff, buff_index);
	m_DBAgent.LoadCharInfo(charid2, buff, buff_index);
	m_DBAgent.LoadCharInfo(charid3, buff, buff_index);

	SetByte(send_buff, WIZ_ALLCHAR_INFO_REQ, send_index);
	SetShort(send_buff, uid, send_index);
	SetShort(send_buff, buff_index, send_index);
	SetString(send_buff, buff, buff_index, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("All Char Packet Drop!!!"));
}

BOOL CAujardDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_RETURN
			|| pMsg->wParam == VK_ESCAPE)
			return TRUE;
	}

	return CDialog::PreTranslateMessage(pMsg);
}

void CAujardDlg::OnOK()
{
	if (AfxMessageBox(_T("진짜 끝낼까요?"), MB_YESNO) == IDYES)
		CDialog::OnOK();
}

void CAujardDlg::OnTimer(UINT nIDEvent)
{
	HANDLE	hProcess = nullptr;

	switch (nIDEvent)
	{
		case PROCESS_CHECK:
			hProcess = OpenProcess(PROCESS_ALL_ACCESS | PROCESS_VM_READ, FALSE, m_LoggerSendQueue.GetProcessId());
			if (hProcess == nullptr)
				AllSaveRoutine();
			break;

		case CONCURRENT_CHECK:
#ifdef __SAMMA
			ConCurrentUserCount();
#endif
			break;

		case SERIAL_TIME:
			g_increase_serial = 50001;
			break;

		case PACKET_CHECK:
			WritePacketLog();
	//		SaveUserData();
			break;
	}

	CDialog::OnTimer(nIDEvent);
}

void CAujardDlg::AllSaveRoutine()
{
	int t_count = 0, retval_1 = 0, retval_2 = 0, count = 0;
	bool bsaved = false;
	CString msgstr;
	char logstr[256] = {};

	CTime cur = CTime::GetCurrentTime();
	TRACE(_T("Dead Time : %d년 %d월 %d일 %d시 %d분\n"), cur.GetYear(), cur.GetMonth(), cur.GetDay(), cur.GetHour(), cur.GetMinute());

	t_count = m_DBAgent.m_UserDataArray.size();
	for (int i = 0; i < t_count; i++)
	{
		_USER_DATA* pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[i];
		if (pUser == nullptr)
			continue;

		if (strlen(pUser->m_id) == 0)
			continue;

		m_DBAgent.AccountLogout(pUser->m_Accountid);
		Sleep(100);

		m_DBAgent.UpdateWarehouseData(pUser->m_Accountid, i, UPDATE_ALL_SAVE);
		Sleep(100);

		if (m_DBAgent.UpdateUser(pUser->m_id, i, UPDATE_ALL_SAVE))
		{
			TRACE(_T("GameServer Dead!! - %hs Saved\n"), pUser->m_id);
			Sleep(100);

			count = 0;
			do
			{
				if (retval_1 = m_DBAgent.CheckUserData(pUser->m_Accountid, pUser->m_id, 1, pUser->m_dwTime, pUser->m_iBank))
					break;
				m_DBAgent.UpdateWarehouseData(pUser->m_Accountid, i, UPDATE_ALL_SAVE); // retry
				count++;
				Sleep(10);
			}
			while (count < 10);

			if (count >= 10)
			{
				sprintf(logstr, "WarehouseData All Save Check Error: %s, %s (W:%d) \r\n", pUser->m_Accountid, pUser->m_id, retval_1);
				WriteLogFile(logstr);
				//m_LogFile.Write(logstr, strlen(logstr));
				TRACE(logstr);
			}

			count = 0;
			do
			{
				if (retval_1 = m_DBAgent.CheckUserData(pUser->m_Accountid, pUser->m_id, 2, pUser->m_dwTime, pUser->m_iExp))
					break;
				m_DBAgent.UpdateUser(pUser->m_id, i, UPDATE_ALL_SAVE);	// retry
				count++;
				Sleep(10);
			}
			while (count < 10);

			if (count >= 10)
			{
				sprintf(logstr, "UserDataSave All Save Check Error: %s, %s (W:%d,U:%d) \r\n", pUser->m_Accountid, pUser->m_id, retval_1, retval_2);
				WriteLogFile(logstr);
				//m_LogFile.Write(logstr, strlen(logstr));
				TRACE(logstr);
			}

			m_DBAgent.MUserInit(i);
			bsaved = true;
		}
		else
		{
			TRACE(_T("GameServer Dead!! - %hs Not Saved\n"), pUser->m_id);
		}

		Sleep(100);
	}
	if (bsaved)
	{
		msgstr.Format(_T("All Data Saved:%d년%d월%d일%d시%d분"), cur.GetYear(), cur.GetMonth(), cur.GetDay(), cur.GetHour(), cur.GetMinute());
		m_OutputList.AddString(msgstr);
	}
}

void CAujardDlg::ConCurrentUserCount()
{
	int t_count = 0;

	for (int i = 0; i < MAX_USER; i++)
	{
		_USER_DATA* pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[i];
		if (pUser == nullptr)
			continue;

		if (strlen(pUser->m_id) == 0)
			continue;

		t_count++;
	}

	TRACE(_T("*** ConCurrentUserCount : server=%d, zone=%d, usercount=%d ***\n"), m_nServerNo, m_nZoneNo, t_count);

	m_DBAgent.UpdateConCurrentUserCount(m_nServerNo, m_nZoneNo, t_count);
}

void CAujardDlg::UserDataSave(char* pBuf)
{
	int index = 0, uid = -1, idlen1 = 0, idlen2 = 0, retval_1 = 0, retval_2 = 0, count = 0;
	_USER_DATA* pUser = nullptr;
	char logstr[256] = {},
		accountid[MAX_ID_SIZE + 1] = {},
		userid[MAX_ID_SIZE + 1] = {};

	uid = GetShort(pBuf, index);
	idlen1 = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen1, index);
	idlen2 = GetShort(pBuf, index);
	GetString(userid, pBuf, idlen2, index);

	if (uid < 0
		|| uid >= MAX_USER)
		return;

	if (strlen(accountid) == 0)
		return;

	if (strlen(userid) == 0)
		return;

	if (uid < 0
		|| uid >= MAX_USER)
		return;

	pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[uid];
	if (pUser == nullptr)
		return;

	retval_2 = m_DBAgent.UpdateUser(userid, uid, UPDATE_PACKET_SAVE);
	Sleep(10);
	retval_1 = m_DBAgent.UpdateWarehouseData(accountid, uid, UPDATE_PACKET_SAVE);

	if (!retval_1
		|| !retval_2)
	{
		do
		{
			retval_2 = m_DBAgent.UpdateUser(userid, uid, UPDATE_PACKET_SAVE);
			Sleep(10);
			retval_1 = m_DBAgent.UpdateWarehouseData(accountid, uid, UPDATE_PACKET_SAVE);

			if (retval_1
				&& retval_2)
				break;

			count++;
		}
		while (count < 10);

		if (count >= 10)
		{
			sprintf(logstr, "UserData Save Error: %s, %s (W:%d,U:%d) \r\n", accountid, userid, retval_1, retval_2);
			WriteLogFile(logstr);
			//m_LogFile.Write(logstr, strlen(logstr));
		}
	}

	count = 0;

	do
	{
		if (retval_1 = m_DBAgent.CheckUserData(accountid, userid, 1, pUser->m_dwTime, pUser->m_iBank))
			break;

		Sleep(10);
		count++;
	}
	while (count < 10);

	if (count >= 10)
	{
		sprintf(logstr, "WarehouseDataSave Check Error: %s, %s (W:%d) \r\n", accountid, userid, retval_1);
		WriteLogFile(logstr);
		//m_LogFile.Write(logstr, strlen(logstr));
	}

	count = 0;

	do
	{
		if (retval_1 = m_DBAgent.CheckUserData(accountid, userid, 2, pUser->m_dwTime, pUser->m_iExp))
			break;

		Sleep(10);
		count++;
	}
	while (count < 10);

	if (count >= 10)
	{
		sprintf(logstr, "UserDataSave Check Error: %s, %s (W:%d) \r\n", accountid, userid, retval_1);
		WriteLogFile(logstr);
		//m_LogFile.Write(logstr, strlen(logstr));
	}

//	sprintf( logstr, "UserDataSave Packet Receive: %s, %s (W:%d,U:%d) \r\n", accountid, userid, retval_1, retval_2 );
//	m_LogFile.Write(logstr, strlen(logstr));
//	TRACE(logstr);

	return;
}

_USER_DATA* CAujardDlg::GetUserPtr(const char* struserid, int& uid)
{
	for (int i = 0; i < MAX_USER; i++)
	{
		_USER_DATA* pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[i];
		if (!pUser)
			continue;

		if (_strnicmp(struserid, pUser->m_id, MAX_ID_SIZE) == 0)
		{
			uid = i;
			return pUser;
		}
	}

	return nullptr;
}

void CAujardDlg::KnightsPacket(char* pBuf)
{
	int index = 0, nation = 0;
	BYTE command = GetByte(pBuf, index);

	switch (command)
	{
		case KNIGHTS_CREATE:
			CreateKnights(pBuf + index);
			break;

		case KNIGHTS_JOIN:
			JoinKnights(pBuf + index);
			break;

		case KNIGHTS_WITHDRAW:
			WithdrawKnights(pBuf + index);
			break;

		case KNIGHTS_REMOVE:
		case KNIGHTS_ADMIT:
		case KNIGHTS_REJECT:
		case KNIGHTS_CHIEF:
		case KNIGHTS_VICECHIEF:
		case KNIGHTS_OFFICER:
		case KNIGHTS_PUNISH:
			ModifyKnightsMember(pBuf + index, command);
			break;

		case KNIGHTS_DESTROY:
			DestroyKnights(pBuf + index);
			break;

		case KNIGHTS_MEMBER_REQ:
			AllKnightsMember(pBuf + index);
			break;

		case KNIGHTS_STASH:
			break;

		case KNIGHTS_LIST_REQ:
			KnightsList(pBuf + index);
			break;

		case KNIGHTS_ALLLIST_REQ:
			nation = GetByte(pBuf, index);
			m_DBAgent.LoadKnightsAllList(nation);
			break;
	}
}

void CAujardDlg::CreateKnights(char* pBuf)
{
	int index = 0, send_index = 0, namelen = 0, idlen = 0, knightindex = 0, uid = -1, nation = 0, result = 0, community = 0, count = 0;
	char knightsname[MAX_ID_SIZE + 1] = {},
		chiefname[MAX_ID_SIZE + 1] = {},
		send_buff[256] = {};

	uid = GetShort(pBuf, index);
	community = GetByte(pBuf, index);
	knightindex = GetShort(pBuf, index);
	nation = GetByte(pBuf, index);
	namelen = GetShort(pBuf, index);
	GetString(knightsname, pBuf, namelen, index);
	idlen = GetShort(pBuf, index);
	GetString(chiefname, pBuf, idlen, index);

	if (uid < 0
		|| uid >= MAX_USER)
		return;

	result = m_DBAgent.CreateKnights(knightindex, nation, knightsname, chiefname, community);

	TRACE(_T("CreateKnights - nid=%d, index=%d, result=%d \n"), uid, knightindex, result);

	SetByte(send_buff, KNIGHTS_CREATE, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, result, send_index);
	SetByte(send_buff, community, send_index);
	SetShort(send_buff, knightindex, send_index);
	SetByte(send_buff, nation, send_index);
	SetShort(send_buff, namelen, send_index);
	SetString(send_buff, knightsname, namelen, send_index);
	SetShort(send_buff, idlen, send_index);
	SetString(send_buff, chiefname, idlen, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Create Knight Packet Drop!!!"));
}

void CAujardDlg::JoinKnights(char* pBuf)
{
	int index = 0, send_index = 0, knightindex = 0, uid = -1, count = 0;
	BYTE result = 0;
	char send_buff[256] = {};
	_USER_DATA* pUser = nullptr;

	uid = GetShort(pBuf, index);
	knightindex = GetShort(pBuf, index);

	if (uid < 0
		|| uid >= MAX_USER)
		return;

	pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[uid];
	if (pUser == nullptr)
		return;

	result = m_DBAgent.UpdateKnights(KNIGHTS_JOIN, pUser->m_id, knightindex, 0);

	TRACE(_T("JoinKnights - nid=%d, name=%hs, index=%d, result=%d \n"), uid, pUser->m_id, knightindex, result);

	SetByte(send_buff, KNIGHTS_JOIN, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, result, send_index);
	SetShort(send_buff, knightindex, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Join Packet Drop!!!"));
}

void CAujardDlg::WithdrawKnights(char* pBuf)
{
	int index = 0, send_index = 0, knightindex = 0, uid = -1, count = 0;
	BYTE result = 0;
	char send_buff[256] = {};
	_USER_DATA* pUser = nullptr;

	uid = GetShort(pBuf, index);
	knightindex = GetShort(pBuf, index);

	if (uid < 0
		|| uid >= MAX_USER)
		return;

	pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[uid];

	result = m_DBAgent.UpdateKnights(KNIGHTS_WITHDRAW, pUser->m_id, knightindex, 0);
	TRACE(_T("WithDrawKnights - nid=%d, index=%d, result=%d \n"), uid, knightindex, result);

	SetByte(send_buff, KNIGHTS_WITHDRAW, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, result, send_index);
	SetShort(send_buff, knightindex, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Withdraw Packet Drop!!!"));
}

void CAujardDlg::ModifyKnightsMember(char* pBuf, BYTE command)
{
	int index = 0, send_index = 0, knightindex = 0, uid = -1, idlen = 0, vicechief = 0, remove_flag = 0, count = 0;
	BYTE result = 0;
	char send_buff[256] = {},
		userid[MAX_ID_SIZE + 1] = {};

	uid = GetShort(pBuf, index);
	knightindex = GetShort(pBuf, index);
	idlen = GetShort(pBuf, index);
	GetString(userid, pBuf, idlen, index);
	remove_flag = GetByte(pBuf, index);

	if (uid < 0
		|| uid >= MAX_USER)
		return;

/*	if( remove_flag == 0 && command == KNIGHTS_REMOVE )	{		// 없는 유저 추방시에는 디비에서만 처리한다
		result = m_DBAgent.UpdateKnights( command, userid, knightindex, remove_flag );
		TRACE(_T("ModifyKnights - command=%d, nid=%d, index=%d, result=%d \n"), command, uid, knightindex, result);
		return;
	}	*/

	result = m_DBAgent.UpdateKnights(command, userid, knightindex, remove_flag);
	TRACE(_T("ModifyKnights - command=%d, nid=%d, index=%d, result=%d \n"), command, uid, knightindex, result);

	//SetByte( send_buff, WIZ_KNIGHTS_PROCESS, send_index );
	SetByte(send_buff, command, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, result, send_index);
	SetShort(send_buff, knightindex, send_index);
	SetShort(send_buff, idlen, send_index);
	SetString(send_buff, userid, idlen, send_index);
	SetByte(send_buff, remove_flag, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Modify Packet Drop!!!"));
}

void CAujardDlg::DestroyKnights(char* pBuf)
{
	int index = 0, send_index = 0, knightindex = 0, uid = -1, count = 0;
	BYTE result = 0;
	char send_buff[256] = {};

	uid = GetShort(pBuf, index);
	knightindex = GetShort(pBuf, index);
	if (uid < 0
		|| uid >= MAX_USER)
		return;

	result = m_DBAgent.DeleteKnights(knightindex);
	TRACE(_T("DestoryKnights - nid=%d, index=%d, result=%d \n"), uid, knightindex, result);

	SetByte(send_buff, KNIGHTS_DESTROY, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, result, send_index);
	SetShort(send_buff, knightindex, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;
		else
			count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("Destroy Packet Drop!!!"));
}

void CAujardDlg::AllKnightsMember(char* pBuf)
{
	int index = 0, send_index = 0, knightindex = 0, uid = -1, buff_index = 0, page = 0, count = 0, t_count = 0;
	BYTE result = 0;
	char send_buff[2048] = {},
		temp_buff[2048] = {};

	uid = GetShort(pBuf, index);
	knightindex = GetShort(pBuf, index);
	//page = GetShort( pBuf, index );

	if (uid < 0
		|| uid >= MAX_USER)
		return;

	//if( page < 0 )  return;

	count = m_DBAgent.LoadKnightsAllMembers(knightindex, 0, temp_buff, buff_index);
	//count = m_DBAgent.LoadKnightsAllMembers( knightindex, page*10, temp_buff, buff_index );

	SetByte(send_buff, KNIGHTS_MEMBER_REQ, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, 0x00, send_index);		// Success
	SetShort(send_buff, 4 + buff_index, send_index);	// total packet size -> short(*3) + buff_index 
	//SetShort( send_buff, page, send_index );
	SetShort(send_buff, count, send_index);
	SetString(send_buff, temp_buff, buff_index, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		t_count++;
	}
	while (t_count < 50);

	if (t_count >= 50)
		m_OutputList.AddString(_T("Member Packet Drop!!!"));
}

void CAujardDlg::KnightsList(char* pBuf)
{
	int index = 0, send_index = 0, knightindex = 0, uid = -1, buff_index = 0, count = 0, retvalue = 0;
	char send_buff[256] = {},
		temp_buff[256] = {};

	uid = GetShort(pBuf, index);
	knightindex = GetShort(pBuf, index);
	if (uid < 0
		|| uid >= MAX_USER)
		return;

	retvalue = m_DBAgent.LoadKnightsInfo(knightindex, temp_buff, buff_index);

	SetByte(send_buff, KNIGHTS_LIST_REQ, send_index);
	SetShort(send_buff, uid, send_index);
	SetByte(send_buff, 0x00, send_index);							// Success
	SetString(send_buff, temp_buff, buff_index, send_index);

	do
	{
		if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
			break;

		count++;
	}
	while (count < 50);

	if (count >= 50)
		m_OutputList.AddString(_T("KnightsList Packet Drop!!!"));
}

void CAujardDlg::SetLogInInfo(char* pBuf)
{
	int index = 0, idlen = 0, serverno = 0, iplen1 = 0, iplen2 = 0, uid = -1, send_index = 0, count = 0, idlen2 = 0;
	BYTE bInit;
	char accountid[MAX_ID_SIZE + 1] = {},
		serverip[20] = {},
		clientip[20] = {},
		charid[MAX_ID_SIZE + 1] = {},
		send_buff[256] = {};

	uid = GetShort(pBuf, index);
	idlen = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen, index);
	idlen2 = GetShort(pBuf, index);
	GetString(charid, pBuf, idlen2, index);
	iplen1 = GetShort(pBuf, index);
	GetString(serverip, pBuf, iplen1, index);
	serverno = GetShort(pBuf, index);
	iplen2 = GetShort(pBuf, index);
	GetString(clientip, pBuf, iplen2, index);
	bInit = GetByte(pBuf, index);

	if (m_DBAgent.SetLogInInfo(accountid, charid, serverip, serverno, clientip, bInit) == FALSE)
	{
		SetByte(send_buff, WIZ_LOGIN_INFO, send_index);
		SetShort(send_buff, uid, send_index);
		SetByte(send_buff, 0x00, send_index);							// FAIL
		do
		{
			if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
				break;

			count++;
		}
		while (count < 50);

		if (count >= 50)
			m_OutputList.AddString(_T("Login Info Packet Drop!!!"));

		char logstr[256] = {};
		sprintf(logstr, "LoginINFO Insert Fail : %s, %s, %d\r\n", accountid, charid, bInit);
		WriteLogFile(logstr);
		//m_LogFile.Write(logstr, strlen(logstr));
	}
}

void CAujardDlg::UserKickOut(char* pBuf)
{
	int index = 0, idlen = 0;
	char accountid[MAX_ID_SIZE + 1] = {};

	idlen = GetShort(pBuf, index);
	GetString(accountid, pBuf, idlen, index);

	m_DBAgent.AccountLogout(accountid);
}

void CAujardDlg::WritePacketLog()
{
	CTime t = CTime::GetCurrentTime();
	char logstr[256] = {};
	sprintf(logstr, "* Packet Count : recv=%d, send=%d, realsend=%d , time = %d:%d\r\n", m_iRecvPacketCount, m_iPacketCount, m_iSendPacketCount, t.GetHour(), t.GetMinute());
	WriteLogFile(logstr);
	//m_LogFile.Write(logstr, strlen(logstr));
}

void CAujardDlg::SaveUserData()
{
	char send_buff[256] = {};
	int send_index = 0;

	for (int i = 0; i < MAX_USER; i++)
	{
		_USER_DATA* pUser = (_USER_DATA*) m_DBAgent.m_UserDataArray[i];
		if (pUser == nullptr)
			continue;

		if (strlen(pUser->m_id) > 0)
		{
			if (::GetTickCount() - pUser->m_dwTime > 360000)
			{
				memset(send_buff, 0, sizeof(send_buff));
				send_index = 0;
				SetShort(send_buff, i, send_index);
				SetShort(send_buff, strlen(pUser->m_Accountid), send_index);
				SetString(send_buff, pUser->m_Accountid, strlen(pUser->m_Accountid), send_index);
				SetShort(send_buff, strlen(pUser->m_id), send_index);
				SetString(send_buff, pUser->m_id, strlen(pUser->m_id), send_index);
				UserDataSave(send_buff);
				Sleep(100);
			}
		}
	}
}

void CAujardDlg::WriteLogFile(char* pData)
{
	CTime cur = CTime::GetCurrentTime();
	char strLog[1024] = {};
	int nDay = cur.GetDay();

	if (m_iLogFileDay != nDay)
	{
		if (m_LogFile.m_hFile != CFile::hFileNull)
			m_LogFile.Close();

		CString filename;
		filename.Format(_T("AujardLog-%d-%d-%d.txt"), cur.GetYear(), cur.GetMonth(), cur.GetDay());
		m_LogFile.Open(filename, CFile::modeWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone);
		m_LogFile.SeekToEnd();
		m_iLogFileDay = nDay;
	}

	sprintf(strLog, "%d-%d-%d %d:%d, %s\r\n", cur.GetYear(), cur.GetMonth(), cur.GetDay(), cur.GetHour(), cur.GetMinute(), pData);
	int nLen = strlen(strLog);
	if (nLen >= 1024)
	{
		TRACE(_T("### WriteLogFile Fail : length = %d ###\n"), nLen);
		return;
	}

	m_LogFile.Write(strLog, nLen);
}

void CAujardDlg::BattleEventResult(char* pData)
{
	int nType = 0, nResult = 0, nLen = 0, index = 0;
	char strMaxUserName[MAX_ID_SIZE + 1] = {};

	nType = GetByte(pData, index);
	nResult = GetByte(pData, index);
	nLen = GetByte(pData, index);
	if (nLen > 0
		&& nLen < MAX_ID_SIZE + 1)
	{
		GetString(strMaxUserName, pData, nLen, index);
		TRACE(_T("--> BattleEventResult : 적국의 대장을 죽인 유저이름은? %hs, len=%d, nation=%d \n"), strMaxUserName, nResult, nResult);
		m_DBAgent.UpdateBattleEvent(strMaxUserName, nResult);
	}
}

void CAujardDlg::CouponEvent(char* pData)
{
	int nSid = 0, nEventNum = 0, nLen = 0, nCharLen = 0, nCouponLen = 0, index = 0, nType = 0, nResult = 0, send_index = 0, count = 0;
	int nItemID = 0, nItemCount = 0, nMessageNum = 0;
	char strAccountName[MAX_ID_SIZE + 1] = {},
		strCharName[MAX_ID_SIZE + 1] = {},
		strCouponID[MAX_ID_SIZE + 1] = {},
		send_buff[256] = {};

	nType = GetByte(pData, index);
	if (nType == CHECK_COUPON_EVENT)
	{
		nSid = GetShort(pData, index);
		nLen = GetShort(pData, index);
		GetString(strAccountName, pData, nLen, index);
		nEventNum = GetDWORD(pData, index);
		// 비러머글 대사문 >.<
		nMessageNum = GetDWORD(pData, index);
		//
		nResult = m_DBAgent.CheckCouponEvent(strAccountName);

		SetByte(send_buff, DB_COUPON_EVENT, send_index);
		SetShort(send_buff, nSid, send_index);
		SetByte(send_buff, nResult, send_index);
		SetDWORD(send_buff, nEventNum, send_index);
		// 비러머글 대사문 >.<
		SetDWORD(send_buff, nMessageNum, send_index);
		//

		do
		{
			if (m_LoggerSendQueue.PutData(send_buff, send_index) == 1)
				break;

			count++;
		}
		while (count < 50);

		if (count >= 50)
			m_OutputList.AddString(_T("CouponEvent Packet Drop!!!"));
	}
	else if (nType == UPDATE_COUPON_EVENT)
	{
		nSid = GetShort(pData, index);
		nLen = GetShort(pData, index);
		GetString(strAccountName, pData, nLen, index);
		nCharLen = GetShort(pData, index);
		GetString(strCharName, pData, nCharLen, index);
		nCouponLen = GetShort(pData, index);
		GetString(strCouponID, pData, nCouponLen, index);
		nItemID = GetDWORD(pData, index);
		nItemCount = GetDWORD(pData, index);

		nResult = m_DBAgent.UpdateCouponEvent(strAccountName, strCharName, strCouponID, nItemID, nItemCount);
	}
}
