// ServerDlg.cpp : implementation file
//

#include "stdafx.h"
#include "Server.h"
#include "ServerDlg.h"
#include "GameSocket.h"
#include "Region.h"

#include <shared/crc32.h>
#include <shared/lzf.h>
#include <shared/globals.h>
#include <shared/Ini.h>

#include <db-library/DatabaseConnManager.h>

#include <math.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// NOTE: Explicitly handled under DEBUG_NEW override
#include <db-library/RecordSetLoader_STLMap.h>
#include <db-library/RecordsetLoader_Vector.h>

BOOL g_bNpcExit = FALSE;
ZoneArray g_arZone;

CRITICAL_SECTION g_User_critical;
CRITICAL_SECTION g_region_critical;
CRITICAL_SECTION g_LogFileWrite;

#define CHECK_ALIVE 	100		//  게임서버와 통신이 끊김여부 판단, 타이머 변수
#define REHP_TIME		200

import AIServerBinder;

using namespace db;

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

/*
	 ** Repent AI Server 작업시 참고 사항 **
	1. 3개의 함수 추가
		int GetSpeed(BYTE bySpeed);
		int GetAttackSpeed(BYTE bySpeed);
		int GetCatsSpeed(BYTE bySpeed);
	2. Repent에  맞개 아래의 함수 수정
		CreateNpcThread();
		GetMonsterTableData();
		GetNpcTableData();
		GetNpcItemTable();
*/

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

// ClassWizard generated virtual function overrides
//{{AFX_VIRTUAL(CAboutDlg)
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CServerDlg dialog

CServerDlg* CServerDlg::s_pInstance = nullptr;

CServerDlg::CServerDlg(CWnd* pParent /*=nullptr*/)
	: CDialog(CServerDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CServerDlg)
	m_strStatus = _T("");
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	m_iYear = 0;
	m_iMonth = 0;
	m_iDate = 0;
	m_iHour = 0;
	m_iMin = 0;
	m_iWeather = 0;
	m_iAmount = 0;
	m_byNight = 1;
	m_byZone = KARUS_ZONE;
	m_byBattleEvent = BATTLEZONE_CLOSE;
	m_sKillKarusNpc = 0;
	m_sKillElmoNpc = 0;
	m_pZoneEventThread = nullptr;
	m_byTestMode = 0;
	//m_ppUserActive = nullptr;
	//m_ppUserInActive = nullptr;

	memset(m_strGameDSN, 0, sizeof(m_strGameDSN));
	memset(m_strGameUID, 0, sizeof(m_strGameUID));
	memset(m_strGamePWD, 0, sizeof(m_strGamePWD));

	DatabaseConnManager::Create();
}

CServerDlg::~CServerDlg()
{
	DatabaseConnManager::Destroy();
}

void CServerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CServerDlg)
	DDX_Control(pDX, IDC_LIST1, m_StatusList);
	DDX_Text(pDX, IDC_STATUS, m_strStatus);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CServerDlg, CDialog)
	//{{AFX_MSG_MAP(CServerDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_TIMER()
	//}}AFX_MSG_MAP
	ON_MESSAGE(WM_GAMESERVER_LOGIN, OnGameServerLogin)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CServerDlg message handlers

///////////////////////////////////////////////////////////////////////////////
//	각종 초기화
//
BOOL CServerDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	s_pInstance = this;

	// Default Init ...
	DefaultInit();

	// TestCode
	TestCode();

	//----------------------------------------------------------------------
	//	Sets a random number starting point.
	//----------------------------------------------------------------------
	SetTimer(CHECK_ALIVE, 10000, nullptr);

	srand((unsigned int) time(nullptr));
	for (int i = 0; i < 10; i++)
		myrand(1, 10000);	// don't delete

	// Compress Init
	memset(m_CompBuf, 0, sizeof(m_CompBuf));	// 압축할 데이터를 모으는 버퍼
	m_iCompIndex = 0;							// 압축할 데이터의 길이
	m_CompCount = 0;							// 압축할 데이터의 개수

	InitializeCriticalSection(&g_User_critical);
	InitializeCriticalSection(&g_LogFileWrite);
	InitializeCriticalSection(&g_region_critical);

	m_sSocketCount = 0;
	m_sErrorSocketCount = 0;
	m_sMapEventNpc = 0;
	m_sReSocketCount = 0;
	m_fReConnectStart = 0.0f;
	m_bFirstServerFlag = FALSE;
	m_byTestMode = NOW_TEST_MODE;

	// User Point Init
	for (int i = 0; i < MAX_USER; i++)
		m_pUser[i] = nullptr;

	// Server Start
	CString logstr;
	CTime time = CTime::GetCurrentTime();
	logstr.Format(_T("[AI ServerStart - %d-%d-%d, %d:%d]"), time.GetYear(), time.GetMonth(), time.GetDay(), time.GetHour(), time.GetMinute());
	m_StatusList.AddString(logstr);
	logstr.Format(_T("[AI ServerStart - %d-%d-%d, %d:%d]\r\n"), time.GetYear(), time.GetMonth(), time.GetDay(), time.GetHour(), time.GetMinute());
	TRACE(logstr);
	LogFileWrite(logstr);

	//----------------------------------------------------------------------
	//	Logfile initialize
	//----------------------------------------------------------------------
	TCHAR strLogFile[50] = {};
	wsprintf(strLogFile, _T("UserLog-%d-%d-%d.txt"), time.GetYear(), time.GetMonth(), time.GetDay());
	m_UserLogFile.Open(strLogFile, CFile::modeWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone);
	m_UserLogFile.SeekToEnd();

	memset(strLogFile, 0, sizeof(strLogFile));
	wsprintf(strLogFile, _T("ItemLog-%d-%d-%d.txt"), time.GetYear(), time.GetMonth(), time.GetDay());
	m_ItemLogFile.Open(strLogFile, CFile::modeWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::shareDenyNone);
	m_ItemLogFile.SeekToEnd();


	//----------------------------------------------------------------------
	//	DB part initialize
	//----------------------------------------------------------------------
	GetServerInfoIni();

	if (m_byZone == UNIFY_ZONE)
		m_strStatus.Format(_T("UNIFY_ZONE 서버의 현재 상태"));
	else if (m_byZone == KARUS_ZONE)
		m_strStatus.Format(_T("KARUS 서버의 현재 상태"));
	else if (m_byZone == ELMORAD_ZONE)
		m_strStatus.Format(_T("ELMORAD 서버의 현재 상태"));
	else if (m_byZone == BATTLE_ZONE)
		m_strStatus.Format(_T("BATTLE 서버의 현재 상태"));

	//----------------------------------------------------------------------
	//	DB part initialize
	//----------------------------------------------------------------------


	//----------------------------------------------------------------------
	//	Communication Part Initialize ...
	//----------------------------------------------------------------------
	m_Iocport.Init(MAX_SOCKET, 1, 1);

	for (int i = 0; i < MAX_SOCKET; i++)
		m_Iocport.m_SockArrayInActive[i] = new CGameSocket;

	//----------------------------------------------------------------------
	//	Load Magic Table
	//----------------------------------------------------------------------
	if (!GetMagicTableData())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	if (!GetMagicType1Data())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	if (!GetMagicType2Data())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	if (!GetMagicType3Data())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	if (!GetMagicType4Data())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	//----------------------------------------------------------------------
	//	Load NPC Item Table
	//----------------------------------------------------------------------
	if (!GetNpcItemTable())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	if (!GetMakeWeaponItemTableData())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	if (!GetMakeDefensiveItemTableData())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	if (!GetMakeGradeItemTableData())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	if (!GetMakeLareItemTableData())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	//----------------------------------------------------------------------
	//	Load NPC Chat Table
	//----------------------------------------------------------------------

	//----------------------------------------------------------------------
	//	Load NPC Data & Activate NPC
	//----------------------------------------------------------------------

	// Monster 특성치 테이블 Load
	if (!GetMonsterTableData())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	// NPC 특성치 테이블 Load
	if (!GetNpcTableData())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	//----------------------------------------------------------------------
	//	Load Zone & Event...
	//----------------------------------------------------------------------
	if (!MapFileLoad())
		AfxPostQuitMessage(0);

	if (!CreateNpcThread())
	{
		EndDialog(IDCANCEL);
		return FALSE;
	}

	//----------------------------------------------------------------------
	//	Load NPC DN Table
	//----------------------------------------------------------------------

	//----------------------------------------------------------------------
	//	Start NPC THREAD
	//----------------------------------------------------------------------
	ResumeAI();

	//----------------------------------------------------------------------
	//	Start Accepting...
	//----------------------------------------------------------------------
	if (m_byZone == KARUS_ZONE
		|| m_byZone == UNIFY_ZONE)
	{
		if (!m_Iocport.Listen(AI_KARUS_SOCKET_PORT))
			AfxMessageBox(_T("FAIL TO CREATE LISTEN STATE"), MB_OK);
	}
	else if (m_byZone == ELMORAD_ZONE)
	{
		if (!m_Iocport.Listen(AI_ELMO_SOCKET_PORT))
			AfxMessageBox(_T("FAIL TO CREATE LISTEN STATE"), MB_OK);
	}
	else if (m_byZone == BATTLE_ZONE)
	{
		if (!m_Iocport.Listen(AI_BATTLE_SOCKET_PORT))
			AfxMessageBox(_T("FAIL TO CREATE LISTEN STATE"), MB_OK);
	}

	//::ResumeThread( m_Iocport.m_hAcceptThread );
	UpdateData(FALSE);
	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CServerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CServerDlg::OnPaint()
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
HCURSOR CServerDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

void CServerDlg::DefaultInit()
{
	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
}

void CServerDlg::ReportTableLoadError(const recordset_loader::Error& err, const char* source)
{
	CString msg;
	msg.Format(_T("%hs failed: %hs"), source, err.Message.c_str());
	AfxMessageBox(msg);
}

//	Magic Table 을 읽는다.
BOOL CServerDlg::GetMagicTableData()
{
	recordset_loader::STLMap loader(m_MagictableArray);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CServerDlg::GetMakeWeaponItemTableData()
{
	recordset_loader::STLMap loader(m_MakeWeaponItemArray);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CServerDlg::GetMakeDefensiveItemTableData()
{
	recordset_loader::STLMap<MakeWeaponItemTableArray, model::MakeDefensive> loader(
		m_MakeDefensiveItemArray);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CServerDlg::GetMakeGradeItemTableData()
{
	recordset_loader::STLMap loader(m_MakeGradeItemArray);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CServerDlg::GetMakeLareItemTableData()
{
	recordset_loader::STLMap loader(m_MakeLareItemArray);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////
//	NPC Item Table 을 읽는다.
//
BOOL CServerDlg::GetNpcItemTable()
{
	using ModelType = model::MonsterItem;

	std::vector<ModelType*> rows;

	recordset_loader::Vector<ModelType> loader(rows);
	if (!loader.Load_ForbidEmpty(true))
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	m_NpcItem.m_nField = loader.GetColumnCount();
	m_NpcItem.m_nRow = static_cast<int>(rows.size());

	if (rows.empty())
		return FALSE;

	m_NpcItem.m_ppItem = new int* [m_NpcItem.m_nRow];
	for (int i = 0; i < m_NpcItem.m_nRow; i++)
		m_NpcItem.m_ppItem[i] = new int[m_NpcItem.m_nField];

	for (size_t i = 0; i < rows.size(); i++)
	{
		ModelType* row = rows[i];

		m_NpcItem.m_ppItem[i][0] = row->MonsterId;
		m_NpcItem.m_ppItem[i][1] = row->ItemId1;
		m_NpcItem.m_ppItem[i][2] = row->DropChance1;
		m_NpcItem.m_ppItem[i][3] = row->ItemId2;
		m_NpcItem.m_ppItem[i][4] = row->DropChance2;
		m_NpcItem.m_ppItem[i][5] = row->ItemId3;
		m_NpcItem.m_ppItem[i][6] = row->DropChance3;
		m_NpcItem.m_ppItem[i][7] = row->ItemId4;
		m_NpcItem.m_ppItem[i][8] = row->DropChance4;
		m_NpcItem.m_ppItem[i][9] = row->ItemId5;
		m_NpcItem.m_ppItem[i][10] = row->DropChance5;

		delete row;
	}

	rows.clear();

	return TRUE;
}

//	Monster Table Data 를 읽는다.
BOOL CServerDlg::GetMonsterTableData()
{
	recordset_loader::STLMap<
		NpcTableArray,
		model::Monster> loader(m_arMonTable);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

//	NPC Table Data 를 읽는다. (경비병 & NPC)
BOOL CServerDlg::GetNpcTableData()
{
	recordset_loader::STLMap loader(m_arNpcTable);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

//	Npc Thread 를 만든다.
BOOL CServerDlg::CreateNpcThread()
{
	m_TotalNPC = 0;			// DB에 있는 수
	m_CurrentNPC = 0;
	m_CurrentNPCError = 0;

	std::vector<model::NpcPos*> rows;
	if (!LoadNpcPosTable(rows))
		return FALSE;

	for (model::NpcPos* row : rows)
		delete row;
	rows.clear();

	int step = 0;
	int nThreadNumber = 0;
	CNpcThread* pNpcThread = nullptr;

	for (auto& [_, pNpc] : m_arNpc)
	{
		if (step == 0)
			pNpcThread = new CNpcThread;

		pNpcThread->m_pNpc[step] = pNpc;
		pNpcThread->m_pNpc[step]->m_sThreadNumber = nThreadNumber;
		pNpcThread->m_pNpc[step]->Init();

		++step;

		if (step == NPC_NUM)
		{
			pNpcThread->m_sThreadNumber = nThreadNumber++;
			pNpcThread->pIOCP = &m_Iocport;
			pNpcThread->m_pThread = AfxBeginThread(NpcThreadProc, &pNpcThread->m_ThreadInfo, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
			m_arNpcThread.push_back(pNpcThread);
			step = 0;
		}
	}
	if (step != 0)
	{
		pNpcThread->m_sThreadNumber = nThreadNumber++;
		pNpcThread->pIOCP = &m_Iocport;
		pNpcThread->m_pThread = AfxBeginThread(NpcThreadProc, &pNpcThread->m_ThreadInfo, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
		m_arNpcThread.push_back(pNpcThread);
	}

	// Event Npc Logic
	m_pZoneEventThread = AfxBeginThread(ZoneEventThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);

	//TRACE(_T("m_TotalNPC = %d \n"), m_TotalNPC);
	CString logstr;
	logstr.Format(_T("[Monster Init - %d]"), m_TotalNPC);
	m_StatusList.AddString(logstr);

	return TRUE;
}

BOOL CServerDlg::LoadNpcPosTable(std::vector<model::NpcPos*>& rows)
{
	CRoomEvent* pRoom = nullptr;

	recordset_loader::Vector<model::NpcPos> loader(rows);
	if (!loader.Load_ForbidEmpty(true))
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	int nSerial = m_sMapEventNpc;

	for (model::NpcPos* row : rows)
	{
		bool bMoveNext = true;
		int nPathSerial = 1;
		int nNpcCount = 0;

		do
		{
			int nMonsterNumber = row->NumNpc;
			int nServerNum = GetServerNumber(row->ZoneId);

			if (m_byZone == nServerNum
				|| m_byZone == UNIFY_ZONE)
			{
				for (int j = 0; j < nMonsterNumber; j++)
				{
					CNpc* pNpc = new CNpc();
					pNpc->m_sNid = nSerial++;						// 서버 내에서의 고유 번호
					pNpc->m_sSid = (short) row->NpcId;				// MONSTER(NPC) Serial ID

					pNpc->m_byMoveType = row->ActType;
					pNpc->m_byInitMoveType = row->ActType;
					pNpc->m_byDirection = row->Direction;

					model::Npc* pNpcTable = nullptr;

					if (row->ActType >= 0
						&& row->ActType < 100)
					{
						pNpcTable = m_arMonTable.GetData(pNpc->m_sSid);
					}
					else if (row->ActType >= 100)
					{
						pNpc->m_byMoveType = row->ActType - 100;
						//pNpc->m_byInitMoveType = row->ActType - 100;

						pNpcTable = m_arNpcTable.GetData(pNpc->m_sSid);
					}

					pNpc->m_byBattlePos = 0;

					if (pNpc->m_byMoveType >= 2)
					{
						pNpc->m_byBattlePos = myrand(1, 3);
						pNpc->m_byPathCount = nPathSerial++;
					}

					pNpc->InitPos();

					if (pNpcTable == nullptr)
					{
						TRACE(_T("#### CreateNpcThread Fail : [nid = %d, sid = %d] #####\n"), pNpc->m_sNid, pNpc->m_sSid);
						break;
					}

					if (bMoveNext)
					{
						bMoveNext = false;
						nNpcCount = row->NumNpc;
					}

					pNpc->Load(pNpcTable, true);

					//////// MONSTER POS ////////////////////////////////////////
					pNpc->m_sCurZone = row->ZoneId;

					float fRandom_X = 0.0f, fRandom_Z = 0.0f;

					// map에 몬스터의 위치를 랜덤하게 위치시킨다.. (테스트 용 : 수정-DB에서 읽어오는데로 몬 위치 결정)
					int nRandom = abs(row->LeftX - row->RightX);
					if (nRandom <= 1)
					{
						fRandom_X = (float) row->LeftX;
					}
					else
					{
						if (row->LeftX < row->RightX)
							fRandom_X = (float) myrand(row->LeftX, row->RightX);
						else
							fRandom_X = (float) myrand(row->RightX, row->LeftX);
					}

					nRandom = abs(row->TopZ - row->BottomZ);
					if (nRandom <= 1)
					{
						fRandom_Z = (float) row->TopZ;
					}
					else
					{
						if (row->TopZ < row->BottomZ)
							fRandom_Z = (float) myrand(row->TopZ, row->BottomZ);
						else
							fRandom_Z = (float) myrand(row->BottomZ, row->TopZ);
					}

					pNpc->m_fCurX = fRandom_X;
					pNpc->m_fCurY = 0;
					pNpc->m_fCurZ = fRandom_Z;

					if (row->RespawnTime < 15)
					{
						TRACE(_T("##### ServerDlg:CreateNpcThread - RegenTime Error :  nid=%d, name=%hs, regentime=%d #####\n"), pNpc->m_sNid + NPC_BAND, pNpc->m_strName, row->RespawnTime);
						row->RespawnTime = 30;
					}

					pNpc->m_sRegenTime = row->RespawnTime * 1000;	// 초(DB)단위-> 밀리세컨드로

					pNpc->m_sMaxPathCount = row->PathPointCount;

					if (pNpc->m_byMoveType == 2
						|| pNpc->m_byMoveType == 3)
					{
						if (row->PathPointCount == 0
							|| !row->Path.has_value())
						{
							CString error;
							error.Format(
								_T("LoadNpcPosTable: NPC expects path to be set - zone=%d nid=%d, sid=%d, name=%hs, acttype=%d, path=%d #####\n"),
								row->ZoneId,
								pNpc->m_sNid + NPC_BAND,
								pNpc->m_sSid,
								pNpc->m_strName.c_str(),
								pNpc->m_byMoveType,
								pNpc->m_sMaxPathCount);
							TRACE(error);
							AfxMessageBox(error);

							return FALSE;
						}
					}

					int index = 0;

					if (row->PathPointCount != 0
						&& row->Path.has_value())
					{
						// The path is a series of points (x,z), each in the form ("%04d%04d", x, z)
						// As such, we expect there to be at least 8 characters per point.
						constexpr size_t CharactersPerPoint = 8;

						const std::string& path = *row->Path;
						if ((row->PathPointCount * CharactersPerPoint) > path.size())
						{
							CString error;
							error.Format(
								_T("LoadNpcPosTable: NPC expects a larger path for this DotCnt[%d] (path point count) - zone=%d nid=%d, sid=%d, name=%hs, acttype=%d, path=%d #####\n"),
								row->ZoneId,
								row->PathPointCount,
								pNpc->m_sNid + NPC_BAND,
								pNpc->m_sSid,
								pNpc->m_strName.c_str(),
								pNpc->m_byMoveType,
								pNpc->m_sMaxPathCount);
							TRACE(error);
							AfxMessageBox(error);
							return FALSE;
						}

						for (int l = 0; l < row->PathPointCount; l++)
						{
							char szX[5] = {}, szZ[5] = {};
							GetString(szX, path.c_str(), 4, index);
							GetString(szZ, path.c_str(), 4, index);
							pNpc->m_PathList.pPattenPos[l].x = atoi(szX);
							pNpc->m_PathList.pPattenPos[l].z = atoi(szZ);
							//	TRACE(_T(" l=%d, x=%d, z=%d\n"), l, pNpc->m_PathList.pPattenPos[l].x, pNpc->m_PathList.pPattenPos[l].z);
						}
					}

					pNpc->m_nInitMinX = pNpc->m_nLimitMinX = row->LeftX;
					pNpc->m_nInitMinY = pNpc->m_nLimitMinZ = row->TopZ;
					pNpc->m_nInitMaxX = pNpc->m_nLimitMaxX = row->RightX;
					pNpc->m_nInitMaxY = pNpc->m_nLimitMaxZ = row->BottomZ;
					// dungeon work
					pNpc->m_byDungeonFamily = row->DungeonFamily;
					pNpc->m_bySpecialType = row->SpecialType;
					pNpc->m_byRegenType = row->RegenType;
					pNpc->m_byTrapNumber = row->TrapNumber;

					if (pNpc->m_byDungeonFamily > 0)
					{
						pNpc->m_nLimitMinX = row->LimitMinX;
						pNpc->m_nLimitMinZ = row->LimitMinZ;
						pNpc->m_nLimitMaxX = row->LimitMaxX;
						pNpc->m_nLimitMaxZ = row->LimitMaxZ;
					}

					pNpc->m_ZoneIndex = -1;

					MAP* pMap = nullptr;
					for (size_t i = 0; i < g_arZone.size(); i++)
					{
						if (g_arZone[i]->m_nZoneNumber == pNpc->m_sCurZone)
						{
							pNpc->m_ZoneIndex = static_cast<short>(i);
							pMap = g_arZone[i];
							break;
						}
					}

					if (pMap == nullptr)
					{
						AfxMessageBox(_T("Error : CServerDlg,, Invaild zone Index!!"));
						return FALSE;
					}

					//pNpc->Init();
					//m_arNpc.Add(pNpc);
					if (!m_arNpc.PutData(pNpc->m_sNid, pNpc))
					{
						TRACE(_T("Npc PutData Fail - %d\n"), pNpc->m_sNid);
						delete pNpc;
						pNpc = nullptr;
					}

					if (pNpc != nullptr
						&& pMap->m_byRoomEvent > 0
						&& pNpc->m_byDungeonFamily > 0)
					{
						pRoom = pMap->m_arRoomEventArray.GetData(pNpc->m_byDungeonFamily);
						if (pRoom == nullptr)
						{
							TRACE(_T("Error : CServerDlg,, Map Room Npc Fail!! : nid=%d, sid=%d, name=%hs, fam=%d, zoneindex=%d\n"), pNpc->m_sNid + NPC_BAND, pNpc->m_sSid, pNpc->m_strName, pNpc->m_byDungeonFamily, pNpc->m_ZoneIndex);
							AfxMessageBox(_T("Error : CServerDlg,, Map Room Npc Fail!!"));
							return FALSE;
						}

						int* pInt = new int;
						*pInt = pNpc->m_sNid;
						if (!pRoom->m_mapRoomNpcArray.PutData(pNpc->m_sNid, pInt))
						{
							delete pInt;
							TRACE(_T("### Map - Room Array MonsterNid Fail : nid=%d, sid=%d ###\n"), pNpc->m_sNid, pNpc->m_sSid);
						}
					}

					m_TotalNPC = nSerial;

					if (--nNpcCount > 0)
						continue;

					bMoveNext = true;
					nNpcCount = 0;
				}
			}
		} 
		while (!bMoveNext);
	}

	return TRUE;
}

//	NPC Thread 들을 작동시킨다.
void CServerDlg::ResumeAI()
{
	int i, j;

	for (i = 0; i < m_arNpcThread.size(); i++)
	{
		for (j = 0; j < NPC_NUM; j++)
			m_arNpcThread[i]->m_ThreadInfo.pNpc[j] = m_arNpcThread[i]->m_pNpc[j];

		m_arNpcThread[i]->m_ThreadInfo.pIOCP = &m_Iocport;

		::ResumeThread(m_arNpcThread[i]->m_pThread->m_hThread);
	}


	// Event Npc Logic
/*	m_arEventNpcThread[0]->m_ThreadInfo.hWndMsg = this->GetSafeHwnd();
	for(j = 0; j < NPC_NUM; j++)
	{
		m_arEventNpcThread[0]->m_ThreadInfo.pNpc[j] = nullptr;	// 초기 소환 몹이 당연히 없으므로 NULL로 작동을 안시킴
		m_arEventNpcThread[0]->m_ThreadInfo.m_byNpcUsed[j] = 0;
	}
	m_arEventNpcThread[0]->m_ThreadInfo.pIOCP = &m_Iocport;

	::ResumeThread(m_arEventNpcThread[0]->m_pThread->m_hThread);
	*/

	::ResumeThread(m_pZoneEventThread->m_hThread);
}

//	메모리 정리
BOOL CServerDlg::DestroyWindow()
{
	// TODO: Add your specialized code here and/or call the base class
	KillTimer(CHECK_ALIVE);
	//KillTimer( REHP_TIME );

	g_bNpcExit = TRUE;

	if (m_UserLogFile.m_hFile != CFile::hFileNull)
		m_UserLogFile.Close();

	if (m_ItemLogFile.m_hFile != CFile::hFileNull)
		m_ItemLogFile.Close();

	for (size_t i = 0; i < m_arNpcThread.size(); i++)
		WaitForSingleObject(m_arNpcThread[i]->m_pThread->m_hThread, INFINITE);

	// Event Npc Logic
/*	for(i = 0; i < m_arEventNpcThread.size(); i++)
	{
		WaitForSingleObject(m_arEventNpcThread[i]->m_pThread->m_hThread, INFINITE);
	}	*/

	WaitForSingleObject(m_pZoneEventThread, INFINITE);

	// DB테이블 삭제 부분

	// Map(Zone) Array Delete...
	for (size_t i = 0; i < g_arZone.size(); i++)
		delete g_arZone[i];
	g_arZone.clear();

	// NpcTable Array Delete
	if (!m_arMonTable.IsEmpty())
		m_arMonTable.DeleteAllData();

	// NpcTable Array Delete
	if (!m_arNpcTable.IsEmpty())
		m_arNpcTable.DeleteAllData();

	// NpcThread Array Delete
	for (size_t i = 0; i < m_arNpcThread.size(); i++)
		delete m_arNpcThread[i];
	m_arNpcThread.clear();

	// Event Npc Logic
	// EventNpcThread Array Delete
/*	for(i = 0; i < m_arEventNpcThread.size(); i++)
		delete m_arEventNpcThread[i];
	m_arEventNpcThread.clear();		*/

	// Item Array Delete
	if (m_NpcItem.m_ppItem)
	{
		for (int i = 0; i < m_NpcItem.m_nRow; i++)
		{
			delete[] m_NpcItem.m_ppItem[i];
			m_NpcItem.m_ppItem[i] = nullptr;
		}
		delete[] m_NpcItem.m_ppItem;
		m_NpcItem.m_ppItem = nullptr;
	}

	if (!m_MakeWeaponItemArray.IsEmpty())
		m_MakeWeaponItemArray.DeleteAllData();

	if (!m_MakeDefensiveItemArray.IsEmpty())
		m_MakeDefensiveItemArray.DeleteAllData();

	if (!m_MakeGradeItemArray.IsEmpty())
		m_MakeGradeItemArray.DeleteAllData();

	if (!m_MakeLareItemArray.IsEmpty())
		m_MakeLareItemArray.DeleteAllData();

	// MagicTable Array Delete
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

	// Npc Array Delete
	if (!m_arNpc.IsEmpty())
		m_arNpc.DeleteAllData();

	// User Array Delete
	for (int i = 0; i < MAX_USER; i++)
	{
		delete m_pUser[i];
		m_pUser[i] = nullptr;
	}

	// Party Array Delete 
	if (!m_arParty.IsEmpty())
		m_arParty.DeleteAllData();

	while (!m_ZoneNpcList.empty())
		m_ZoneNpcList.pop_front();

	DeleteCriticalSection(&g_User_critical);
	DeleteCriticalSection(&g_LogFileWrite);
	DeleteCriticalSection(&g_region_critical);

	s_pInstance = nullptr;

	return CDialog::DestroyWindow();
}

void CServerDlg::DeleteUserList(int uid)
{
	if (uid < 0
		|| uid >= MAX_USER)
	{
		TRACE(_T("#### ServerDlg:DeleteUserList Uid Fail : uid=%d\n"), uid);
		return;
	}

	EnterCriticalSection(&g_User_critical);

	CUser* pUser = nullptr;
	pUser = m_pUser[uid];
	if (!pUser)
	{
		LeaveCriticalSection(&g_User_critical);
		TRACE(_T("#### ServerDlg:DeleteUserList UserPtr Fail : uid=%d\n"), uid);
		return;
	}
	if (pUser->m_iUserId == uid)
	{
		TRACE(_T("*** UserLogOut으로 포인터 반환 : uid=%d, %hs ***\n"), uid, pUser->m_strUserID);
		pUser->m_lUsed = 1;
		delete m_pUser[uid];
		m_pUser[uid] = nullptr;
	}
	else
	{
		TRACE(_T("#### ServerDlg:DeleteUserList Not Uid Fail : uid=%d\n"), uid);
	}

	LeaveCriticalSection(&g_User_critical);
}

BOOL CServerDlg::MapFileLoad()
{
	using ModelType = model::ZoneInfo;

	BOOL loaded = FALSE;

	m_sTotalMap = 0;

	recordset_loader::Base<ModelType> loader;
	loader.SetProcessFetchCallback([&](db::ModelRecordSet<ModelType>& recordset)
	{
		CString szFullPath, errormsg;

		// Build the base MAP directory
		std::filesystem::path mapDir(GetProgPath().GetString());
		mapDir /= MAP_DIR;

		// Resolve it to strip the relative references to be nice.
		if (std::filesystem::exists(mapDir))
			mapDir = std::filesystem::canonical(mapDir);

		do
		{
			ModelType row = {};
			recordset.get_ref(row);

			std::filesystem::path mapPath
				= mapDir / row.Name;

			szFullPath.Format(
				_T("%ls"),
				mapPath.c_str());

			CFile file;
			if (!file.Open(szFullPath, CFile::modeRead))
			{
				errormsg.Format(_T("파일 Open 실패 - %s\n"), szFullPath.GetString());
				AfxMessageBox(errormsg);
				return;
			}

			MAP* pMap = new MAP();
			pMap->m_nServerNo = row.ServerId;
			pMap->m_nZoneNumber = row.ZoneId;

			if (!pMap->LoadMap(file.m_hFile))
			{
				errormsg.Format(_T("Map Load 실패 - %s\n"), szFullPath.GetString());
				AfxMessageBox(errormsg);
				delete pMap;
				return;
			}

			file.Close();

			// dungeon work
			if (row.RoomEvent > 0)
			{
				if (!pMap->LoadRoomEvent(row.RoomEvent))
				{
					errormsg.Format(_T("Map Room Event Load 실패 - %s\n"), szFullPath.GetString());
					AfxMessageBox(errormsg);
					delete pMap;
					return;
				}

				pMap->m_byRoomEvent = 1;
			}

			g_arZone.push_back(pMap);
			++m_sTotalMap;
		}
		while (recordset.next());

		loaded = TRUE;
	});

	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return loaded;
}

// sungyong 2002.05.23
// game server에 모든 npc정보를 전송..
void CServerDlg::AllNpcInfo()
{
	// server alive check
	CNpc* pNpc = nullptr;
	int nZone = 0;
	int size = m_arNpc.GetSize();

	int send_index = 0, zone_index = 0, packet_size = 0;
	int count = 0, send_count = 0, send_tot = 0;
	char send_buff[2048] = {};

	for (MAP* pMap : g_arZone)
	{
		if (pMap == nullptr)
			continue;

		nZone = pMap->m_nZoneNumber;

		memset(send_buff, 0, sizeof(send_buff));
		send_index = 0;
		SetByte(send_buff, AG_SERVER_INFO, send_index);
		SetByte(send_buff, SERVER_INFO_START, send_index);
		SetByte(send_buff, nZone, send_index);
		packet_size = Send(send_buff, send_index, nZone);

		zone_index = GetZoneIndex(nZone);
		send_index = 2;
		count = 0;
		send_count = 0;
		m_CompCount = 0;
		m_iCompIndex = 0;
		memset(send_buff, 0, sizeof(send_buff));

		TRACE(_T("****  allNpcInfo start = %d *****\n"), nZone);

		for (int i = 0; i < size; i++)
		{
			pNpc = m_arNpc.GetData(i);
			if (pNpc == nullptr)
			{
				TRACE(_T("##### allNpcInfo Fail = %d\n"), i);
				continue;
			}

			if (pNpc->m_sCurZone != nZone)
				continue;

			pNpc->SendNpcInfoAll(send_buff, send_index, count);
			count++;

			if (count == NPC_NUM)
			{
				SetByte(send_buff, NPC_INFO_ALL, send_count);
				SetByte(send_buff, (BYTE) count, send_count);
				m_CompCount++;
				//::CopyMemory(m_CompBuf+m_iCompIndex, send_buff, send_index);
				memset(m_CompBuf, 0, sizeof(m_CompBuf));
				::CopyMemory(m_CompBuf, send_buff, send_index);
				m_iCompIndex = send_index;
				SendCompressedData(nZone);
				send_index = 2;
				send_count = 0;
				count = 0;
				send_tot++;
				//TRACE(_T("AllNpcInfo - send_count=%d, count=%d, zone=%d\n"), send_tot, count, nZone);
				memset(send_buff, 0, sizeof(send_buff));
				Sleep(50);
			}
		}

		//if( count != 0 )	TRACE(_T("--> AllNpcInfo - send_count=%d, count=%d, zone=%d\n"), send_tot, count, nZone);
		if (count != 0
			&& count < NPC_NUM)
		{
			send_count = 0;
			SetByte(send_buff, NPC_INFO_ALL, send_count);
			SetByte(send_buff, (BYTE) count, send_count);
			Send(send_buff, send_index, nZone);
			send_tot++;
			//TRACE(_T("AllNpcInfo - send_count=%d, count=%d, zone=%d\n"), send_tot, count, nZone);
			Sleep(50);
		}

		send_index = 0;
		memset(send_buff, 0, sizeof(send_buff));
		SetByte(send_buff, AG_SERVER_INFO, send_index);
		SetByte(send_buff, SERVER_INFO_END, send_index);
		SetByte(send_buff, nZone, send_index);
		SetShort(send_buff, (short) m_TotalNPC, send_index);
		packet_size = Send(send_buff, send_index, nZone);

		TRACE(_T("****  allNpcInfo end = %d *****\n"), nZone);
	}

	Sleep(1000);
}
// ~sungyong 2002.05.23

CUser* CServerDlg::GetUserPtr(int nid)
{
	CUser* pUser = nullptr;

	if (nid < 0
		|| nid >= MAX_USER)
	{
		if (nid != -1)
			TRACE(_T("### GetUserPtr :: User Array Overflow [%d] ###\n"), nid);

		return nullptr;
	}

/*	if( !m_ppUserActive[nid] )
		return nullptr;

	if( m_ppUserActive[nid]->m_lUsed == 1 ) return nullptr;	// 포인터 사용을 허락치 않음.. (logout중)

	pUser = (CUser*)m_ppUserActive[nid];
*/
	pUser = m_pUser[nid];
	if (pUser == nullptr)
		return nullptr;

	// 포인터 사용을 허락치 않음.. (logout중)
	if (pUser->m_lUsed == 1)
		return nullptr;

	if (pUser->m_iUserId < 0
		|| pUser->m_iUserId >= MAX_USER)
		return nullptr;

	if (pUser->m_iUserId == nid)
		return pUser;

	return nullptr;
}

void CServerDlg::OnTimer(UINT nIDEvent)
{
	switch (nIDEvent)
	{
		case CHECK_ALIVE:
			CheckAliveTest();
			break;

		case REHP_TIME:
			//RechargeHp();
			break;
	}

	CDialog::OnTimer(nIDEvent);
}

// sungyong 2002.05.23
void CServerDlg::CheckAliveTest()
{
	int send_index = 0;
	char send_buff[256] = {};
	int iErrorCode = 0;

	SetByte(send_buff, AG_CHECK_ALIVE_REQ, send_index);

	CGameSocket* pSocket = nullptr;
	int size = 0, count = 0;

	CString logstr;
	CTime time = CTime::GetCurrentTime();

	for (int i = 0; i < MAX_SOCKET; i++)
	{
		pSocket = (CGameSocket*) m_Iocport.m_SockArray[i];
		if (pSocket == nullptr)
			continue;

		size = pSocket->Send(send_buff, send_index);
		if (size > 0)
		{
			++m_sErrorSocketCount;
			if (m_sErrorSocketCount == 10)
			{
				logstr.Format(_T("*** All Socket Closed ***  %d-%d-%d, %d:%d]\r\n"), time.GetYear(), time.GetMonth(), time.GetDay(), time.GetHour(), time.GetMinute());
				//LogFileWrite( logstr );
			}
			count++;
		}
		//TRACE(_T("size = %d, socket_num = %d, i=%d \n"), size, pSocket->m_sSocketID, i);
	}

	if (count <= 0)
		DeleteAllUserList(9999);

	RegionCheck();
}

void CServerDlg::DeleteAllUserList(int zone)
{
	if (zone < 0)
		return;

	CString logstr;

	// 모든 소켓이 끊어진 상태...
	if (zone == 9999
		&& m_bFirstServerFlag)
	{
		TRACE(_T("*** DeleteAllUserList - Start *** \n"));

		for (MAP* pMap : g_arZone)
		{
			if (pMap == nullptr)
				continue;

			for (int i = 0; i < pMap->m_sizeRegion.cx; i++)
			{
				for (int j = 0; j < pMap->m_sizeRegion.cy; j++)
				{
					if (!pMap->m_ppRegion[i][j].m_RegionUserArray.IsEmpty())
						pMap->m_ppRegion[i][j].m_RegionUserArray.DeleteAllData();
				}
			}
		}

		EnterCriticalSection(&g_User_critical);
		// User Array Delete
		for (int i = 0; i < MAX_USER; i++)
		{
			delete m_pUser[i];
			m_pUser[i] = nullptr;
		}
		// 파티 정보 삭제..
		LeaveCriticalSection(&g_User_critical);

		// Party Array Delete 
		if (!m_arParty.IsEmpty())
			m_arParty.DeleteAllData();

		m_bFirstServerFlag = FALSE;
		TRACE(_T("*** DeleteAllUserList - End *** \n"));

		logstr.Format(_T("[ DELETE All User List ]"));
		m_StatusList.AddString(logstr);
	}
	else
	{
		if (zone != 9999)
		{
			logstr.Format(_T("[GameServer DisConnect - zone = %d]"), zone);
			m_StatusList.AddString(logstr);
		}
	}
}
// ~sungyong 2002.05.23

void CServerDlg::SendCompressedData(int nZone)
{
	if (m_CompCount <= 0
		|| m_iCompIndex <= 0)
	{
		m_CompCount = 0;
		m_iCompIndex = 0;
		TRACE(_T("#### SendCompressData Fail --> count=%d, index=%d\n"), m_CompCount, m_iCompIndex);
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
		TRACE(_T("Failed to compress packet\n"));
		return;
	}

	crc_value = crc32(reinterpret_cast<uint8_t*>(m_CompBuf), m_iCompIndex);

	SetByte(send_buff, AG_COMPRESSED_DATA, send_index);
	SetShort(send_buff, (short) comp_data_len, send_index);
	SetShort(send_buff, (short) m_iCompIndex, send_index);
	SetDWORD(send_buff, crc_value, send_index);
	SetShort(send_buff, (short) m_CompCount, send_index);
	SetString(send_buff, reinterpret_cast<const char*>(comp_buff), comp_data_len, send_index);

	Send(send_buff, send_index, nZone);

	m_CompCount = 0;
	m_iCompIndex = 0;
}

BOOL CServerDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (pMsg->wParam == VK_RETURN)
			return TRUE;

		if (pMsg->wParam == VK_F9)
			SyncTest();
	}

	return CDialog::PreTranslateMessage(pMsg);
}

// sungyong 2002.05.23
int CServerDlg::Send(char* pData, int length, int nZone)
{
	SEND_DATA* pNewData = nullptr;
	pNewData = new SEND_DATA;
	if (pNewData == nullptr)
		return 0;

	pNewData->sCurZone = nZone;
	pNewData->sLength = length;
	::CopyMemory(pNewData->pBuf, pData, length);

	EnterCriticalSection(&(m_Iocport.m_critSendData));
	m_Iocport.m_SendDataList.push_back(pNewData);
	LeaveCriticalSection(&(m_Iocport.m_critSendData));

	PostQueuedCompletionStatus(m_Iocport.m_hSendIOCP, 0, 0, nullptr);

	return 0;
}
// ~sungyong 2002.05.23

LRESULT CServerDlg::OnGameServerLogin(WPARAM wParam, LPARAM lParam)
{
/*	if( m_bNpcInfoDown ) {
		m_ZoneNpcList.push_back(wParam);
		return;
	}

	AllNpcInfo( wParam );	*/
	return 0;
}

void CServerDlg::GameServerAcceptThread()
{
	::ResumeThread(m_Iocport.m_hAcceptThread);
}

void CServerDlg::SyncTest()
{
	FILE* stream = fopen("c:\\aiserver.txt", "w");

	fprintf(stream, "*****   Check ... List  *****\n");

	int send_index = 0;
	char send_buff[256] = {};
	int iErrorCode = 0;

	SetByte(send_buff, AG_CHECK_ALIVE_REQ, send_index);

	CGameSocket* pSocket = nullptr;
	int size = 0;

	for (int i = 0; i < MAX_SOCKET; i++)
	{
		pSocket = (CGameSocket*) m_Iocport.m_SockArray[i];
		if (pSocket == nullptr)
			continue;

		size = pSocket->Send(send_buff, send_index);

		fprintf(stream, " size = %d, socket_num = %d \n", size, pSocket->m_sSocketID);
	}

/*
	int size = m_arNpc.GetSize();
	CNpc* pNpc = nullptr;
	CUser* pUser = nullptr;
	__Vector3 vUser;
	__Vector3 vNpc;
	__Vector3 vDistance;
	float fDis = 0.0f;
	int count = 0;

	fprintf(stream, "***** NPC List : %d *****\n", size);
	for(int i=0; i<size; i++)
	{
		pNpc = m_arNpc.GetData(i);
		if(pNpc == nullptr)
		{
			TRACE(_T("##### allNpcInfo Fail = %d\n"), i);
			continue;
		}

		fprintf(stream, "nid=(%d, %s), zone=%d, x=%.2f, z=%.2f, rx=%d, rz=%d\n", pNpc->m_sNid+NPC_BAND, pNpc->m_strName,pNpc->m_sCurZone, pNpc->m_fCurX, pNpc->m_fCurZ, pNpc->m_iRegion_X, pNpc->m_iRegion_Z);

	/*	vNpc.Set(pNpc->m_fCurX, 0, pNpc->m_fCurZ);
		if(pNpc->m_byAttackPos)	{
			//EnterCriticalSection( &g_User_critical );
			pUser = m_arUser.GetData(pNpc->m_Target.id);
			if(pUser == nullptr) {
				fprintf(stream, "## Fail ## nid=(%d, %s), att_pos=%d, x=%.2f, z=%.2f\n", pNpc->m_sNid+NPC_BAND, pNpc->m_strName, pNpc->m_byAttackPos, pNpc->m_fCurX, pNpc->m_fCurZ);
				continue;
			}
			vUser.Set(pNpc->m_Target.x, 0, pNpc->m_Target.z);
			fDis = pNpc->GetDistance(vNpc, vUser);
			//fprintf(stream, "[ target : x=%.2f, z=%.2f ] [ user : x=%.2f, z=%.2f ] \n", pNpc->m_Target.x, pNpc->m_Target.z, pUser->m_curx, pUser->m_curz);
			fprintf(stream, "nid=(%d, %s), att_pos=%d, dis=%.2f, x=%.2f, z=%.2f\n", pNpc->m_sNid+NPC_BAND, pNpc->m_strName, pNpc->m_byAttackPos, fDis, pNpc->m_fCurX, pNpc->m_fCurZ);
			//LeaveCriticalSection( &g_User_critical );
		}	*/
	//}	
/*
	fprintf(stream, "*****   User List  *****\n");

	for(i=0; i<MAX_USER; i++)	{
		//pUser = m_ppUserActive[i];
		pUser = m_pUser[i];
		if(pUser == nullptr)		continue;
		fprintf(stream, "nid=(%d, %s), zone=%d, x=%.2f, z=%.2f, rx=%d, rz=%d\n", pUser->m_iUserId, pUser->m_strUserID, pUser->m_curZone, pUser->m_curx, pUser->m_curz, pUser->m_sRegionX, pUser->m_sRegionZ);
	}

	fprintf(stream, "*****   Region List  *****\n");
	int k=0, total_user = 0, total_mon=0;
	MAP* pMap = nullptr;

	for(k=0; k<m_sTotalMap; k++)	{
		pMap = g_arZone[k];
		if(pMap == nullptr)	continue;
		for( i=0; i<pMap->m_sizeRegion.cx; i++ ) {
			for( int j=0; j<pMap->m_sizeRegion.cy; j++ ) {
				EnterCriticalSection( &g_User_critical );
				total_user = pMap->m_ppRegion[i][j].m_RegionUserArray.GetSize();
				total_mon = pMap->m_ppRegion[i][j].m_RegionNpcArray.GetSize();
				LeaveCriticalSection( &g_User_critical );

				if(total_user > 0 || total_mon > 0)	{
					fprintf(stream, "rx=%d, rz=%d, user=%d, monster=%d\n", i, j, total_user, total_mon);
				}
			}
		}
	}	*/

	fclose(stream);
}

CUser* CServerDlg::GetActiveUserPtr(int index)
{
	CUser* pUser = nullptr;

/*	if(index < 0 || index > MAX_USER)	{
		TRACE(_T("### Fail :: User Array Overflow[%d] ###\n"), index );
		return nullptr;
	}

	EnterCriticalSection( &g_User_critical );

	if ( m_ppUserActive[index] ) {
		LeaveCriticalSection( &g_User_critical );
		TRACE(_T("### Fail : ActiveUser Array Invalid[%d] ###\n"), index );
		return nullptr;
	}
	else {
		pUser = (CUser *)m_ppUserInActive[index];
		if( !pUser ) {
			LeaveCriticalSection( &g_User_critical );
			TRACE(_T("### Fail : InActiveUser Array Invalid[%d] ###\n"), index );
			return nullptr;
		}
	}

	m_ppUserActive[index] = pUser;
	m_ppUserInActive[index] = nullptr;

	LeaveCriticalSection( &g_User_critical );	*/

	return pUser;
}

CNpc* CServerDlg::GetNpcPtr(const char* pNpcName)
{
	for (const auto& [_, pNpc] : m_arNpc)
	{
		if (pNpc != nullptr
			&& strcmp(pNpc->m_strName.c_str(), pNpcName) == 0)
			return pNpc;
	}

	return nullptr;
}


//	추가할 소환몹의 메모리를 참조하기위해 플래그가 0인 상태것만 넘긴다.
CNpc* CServerDlg::GetEventNpcPtr()
{
	for (auto& [_, pNpc] : m_arNpc)
	{
		if (pNpc == nullptr)
			continue;

		if (pNpc->m_lEventNpc != 0)
			continue;

		pNpc->m_lEventNpc = 1;
		return pNpc;
	}

	return nullptr;
}

int CServerDlg::MonsterSummon(const char* pNpcName, int zone_id, float fx, float fz)
{
	MAP* pMap = GetMapByID(zone_id);
	if (pMap == nullptr)
	{
		TRACE(_T("#### 소환 실패 : %hs, zone_id=%d #####\n"), pNpcName, zone_id);
		return -1;
	}

	CNpc* pNpc = GetNpcPtr(pNpcName);
	if (pNpc == nullptr)
	{
		TRACE(_T("소환할  몬스터의 이름(%hs)이 잘못되었습니다.\n"), pNpcName);
		return  -1;
	}

	BOOL bFlag = FALSE;
	bFlag = SetSummonNpcData(pNpc, zone_id, fx, fz);

	return 1;
}

//	소환할 몹의 데이타값을 셋팅한다.
BOOL CServerDlg::SetSummonNpcData(CNpc* pNpc, int zone_id, float fx, float fz)
{
	int  iCount = 0;
	CNpc* pEventNpc = GetEventNpcPtr();

	if (pEventNpc == nullptr)
	{
		TRACE(_T("소환할수 있는 몹은 최대 20마리입니다.\n"));
		return FALSE;
	}

	pEventNpc->m_sSid = pNpc->m_sSid;						// MONSTER(NPC) Serial ID
	pEventNpc->m_byMoveType = 1;
	pEventNpc->m_byInitMoveType = 1;
	pEventNpc->m_byBattlePos = 0;
	pEventNpc->m_strName = pNpc->m_strName;					// MONSTER(NPC) Name
	pEventNpc->m_sPid = pNpc->m_sPid;						// MONSTER(NPC) Picture ID
	pEventNpc->m_sSize = pNpc->m_sSize;						// 캐릭터의 비율(100 퍼센트 기준)
	pEventNpc->m_iWeapon_1 = pNpc->m_iWeapon_1;				// 착용무기
	pEventNpc->m_iWeapon_2 = pNpc->m_iWeapon_2;				// 착용무기
	pEventNpc->m_byGroup = pNpc->m_byGroup;					// 소속집단
	pEventNpc->m_byActType = pNpc->m_byActType;				// 행동패턴
	pEventNpc->m_byRank = pNpc->m_byRank;					// 작위
	pEventNpc->m_byTitle = pNpc->m_byTitle;					// 지위
	pEventNpc->m_iSellingGroup = pNpc->m_iSellingGroup;
	pEventNpc->m_sLevel = pNpc->m_sLevel;					// level
	pEventNpc->m_iExp = pNpc->m_iExp;						// 경험치
	pEventNpc->m_iLoyalty = pNpc->m_iLoyalty;				// loyalty
	pEventNpc->m_iHP = pNpc->m_iMaxHP;						// 최대 HP
	pEventNpc->m_iMaxHP = pNpc->m_iMaxHP;					// 현재 HP
	pEventNpc->m_sMP = pNpc->m_sMaxMP;						// 최대 MP
	pEventNpc->m_sMaxMP = pNpc->m_sMaxMP;					// 현재 MP
	pEventNpc->m_sAttack = pNpc->m_sAttack;					// 공격값
	pEventNpc->m_sDefense = pNpc->m_sDefense;				// 방어값
	pEventNpc->m_sHitRate = pNpc->m_sHitRate;				// 타격성공률
	pEventNpc->m_sEvadeRate = pNpc->m_sEvadeRate;			// 회피성공률
	pEventNpc->m_sDamage = pNpc->m_sDamage;					// 기본 데미지
	pEventNpc->m_sAttackDelay = pNpc->m_sAttackDelay;		// 공격딜레이
	pEventNpc->m_sSpeed = pNpc->m_sSpeed;					// 이동속도
	pEventNpc->m_fSpeed_1 = pNpc->m_fSpeed_1;				// 기본 이동 타입
	pEventNpc->m_fSpeed_2 = pNpc->m_fSpeed_2;				// 뛰는 이동 타입..
	pEventNpc->m_fOldSpeed_1 = pNpc->m_fOldSpeed_1;			// 기본 이동 타입
	pEventNpc->m_fOldSpeed_2 = pNpc->m_fOldSpeed_2;			// 뛰는 이동 타입..
	pEventNpc->m_fSecForMetor = 4.0f;						// 초당 갈 수 있는 거리..
	pEventNpc->m_sStandTime = pNpc->m_sStandTime;			// 서있는 시간
	pEventNpc->m_iMagic1 = pNpc->m_iMagic1;					// 사용마법 1
	pEventNpc->m_iMagic2 = pNpc->m_iMagic2;					// 사용마법 2
	pEventNpc->m_iMagic3 = pNpc->m_iMagic3;					// 사용마법 3
	pEventNpc->m_sFireR = pNpc->m_sFireR;					// 화염 저항력
	pEventNpc->m_sColdR = pNpc->m_sColdR;					// 냉기 저항력
	pEventNpc->m_sLightningR = pNpc->m_sLightningR;			// 전기 저항력
	pEventNpc->m_sMagicR = pNpc->m_sMagicR;					// 마법 저항력
	pEventNpc->m_sDiseaseR = pNpc->m_sDiseaseR;				// 저주 저항력
	pEventNpc->m_sPoisonR = pNpc->m_sPoisonR;				// 독 저항력
	pEventNpc->m_sLightR = pNpc->m_sLightR;					// 빛 저항력
	pEventNpc->m_fBulk = pNpc->m_fBulk;
	pEventNpc->m_bySearchRange = pNpc->m_bySearchRange;		// 적 탐지 범위
	pEventNpc->m_byAttackRange = pNpc->m_byAttackRange;		// 사정거리
	pEventNpc->m_byTracingRange = pNpc->m_byTracingRange;	// 추격거리
	pEventNpc->m_tNpcType = pNpc->m_tNpcType;				// NPC Type
	pEventNpc->m_byFamilyType = pNpc->m_byFamilyType;		// 몹들사이에서 가족관계를 결정한다.
	pEventNpc->m_iMoney = pNpc->m_iMoney;					// 떨어지는 돈
	pEventNpc->m_iItem = pNpc->m_iItem;						// 떨어지는 아이템
	pEventNpc->m_tNpcLongType = pNpc->m_tNpcLongType;
	pEventNpc->m_byWhatAttackType = pNpc->m_byWhatAttackType;

	//////// MONSTER POS ////////////////////////////////////////
	pEventNpc->m_sCurZone = static_cast<short>(zone_id);
	pEventNpc->m_fCurX = fx;
	pEventNpc->m_fCurY = 0;
	pEventNpc->m_fCurZ = fz;
	pEventNpc->m_nInitMinX = pNpc->m_nInitMinX;
	pEventNpc->m_nInitMinY = pNpc->m_nInitMinY;
	pEventNpc->m_nInitMaxX = pNpc->m_nInitMaxX;
	pEventNpc->m_nInitMaxY = pNpc->m_nInitMaxY;
	pEventNpc->m_sRegenTime = pNpc->m_sRegenTime;			// 초(DB)단위-> 밀리세컨드로

	pEventNpc->m_ZoneIndex = -1;

	pEventNpc->m_NpcState = NPC_DEAD;						// 상태는 죽은것으로 해야 한다.. 
	pEventNpc->m_bFirstLive = 1;							// 처음 살아난 경우로 해줘야 한다..

	for (size_t i = 0; i < g_arZone.size(); i++)
	{
		if (g_arZone[i]->m_nZoneNumber == zone_id)
		{
			pEventNpc->m_ZoneIndex = static_cast<short>(i);
			break;
		}
	}

	if (pEventNpc->m_ZoneIndex == -1)
	{
		TRACE(_T("Invaild zone Index!!\n"));
		return FALSE;
	}

	pEventNpc->Init();

	BOOL bSuccess = FALSE;

	int test = 0;

	for (int i = 0; i < NPC_NUM; i++)
	{
		test = m_arEventNpcThread[0]->m_ThreadInfo.m_byNpcUsed[i];
		TRACE(_T("setsummon == %d, used=%d\n"), i, test);
		if (m_arEventNpcThread[0]->m_ThreadInfo.m_byNpcUsed[i] == 0)
		{
			m_arEventNpcThread[0]->m_ThreadInfo.m_byNpcUsed[i] = 1;
			bSuccess = TRUE;
			m_arEventNpcThread[0]->m_ThreadInfo.pNpc[i] = pEventNpc;
			break;
		}
	}

	if (!bSuccess)
	{
		pEventNpc->m_lEventNpc = 0;
		TRACE(_T("### 소환에 실패했습니다. ###\n"));
		return FALSE;
	}

	TRACE(_T("*** %d, %hs 를 소환하였습니다. state = %d ***\n"), pEventNpc->m_sNid + NPC_BAND, pEventNpc->m_strName, pEventNpc->m_NpcState);

	return TRUE;
}

void CServerDlg::TestCode()
{
	//InitTrigonometricFunction();

	int random = 0, count_1 = 0, count_2 = 0, count_3 = 0;

	// TestCoding
	for (int i = 0; i < 100; i++)
	{
		random = myrand(1, 3);
		if (random == 1)
			count_1++;
		else if (random == 2)
			count_2++;
		else if (random == 3)
			count_3++;
	}

	//TRACE(_T("$$$ random test == 1=%d, 2=%d, 3=%d,, %d,%hs $$$\n"), count_1, count_2, count_3, __FILE__, __LINE__);

}

BOOL CServerDlg::GetMagicType1Data()
{
	recordset_loader::STLMap loader(m_Magictype1Array);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CServerDlg::GetMagicType2Data()
{
	recordset_loader::STLMap loader(m_Magictype2Array);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CServerDlg::GetMagicType3Data()
{
	recordset_loader::STLMap loader(m_Magictype3Array);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

BOOL CServerDlg::GetMagicType4Data()
{
	recordset_loader::STLMap loader(m_Magictype4Array);
	if (!loader.Load_ForbidEmpty())
	{
		ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	return TRUE;
}

void CServerDlg::RegionCheck()
{
	for (MAP* pMap : g_arZone)
	{
		if (pMap == nullptr)
			continue;

		for (int i = 0; i < pMap->m_sizeRegion.cx; i++)
		{
			for (int j = 0; j < pMap->m_sizeRegion.cy; j++)
			{
				EnterCriticalSection(&g_User_critical);
				int total_user = pMap->m_ppRegion[i][j].m_RegionUserArray.GetSize();
				LeaveCriticalSection(&g_User_critical);
				if (total_user > 0)  
					pMap->m_ppRegion[i][j].m_byMoving = 1;
				else
					pMap->m_ppRegion[i][j].m_byMoving = 0;
			}
		}
	}
}

BOOL CServerDlg::AddObjectEventNpc(_OBJECT_EVENT* pEvent, int zone_number)
{
	int i = 0, j = 0, objectid = 0;
	model::Npc* pNpcTable = nullptr;
	BOOL bFindNpcTable = FALSE;
	int offset = 0;
	int nServerNum = 0;
	nServerNum = GetServerNumber(zone_number);
	//if(m_byZone != zone_number)	 return FALSE;
	//if(m_byZone != UNIFY_ZONE)	{
	//	if(m_byZone != nServerNum)	 return FALSE;
	//}

	//if( zone_number > 201 )	return FALSE;	// test
	pNpcTable = m_arNpcTable.GetData(pEvent->sIndex);
	if (pNpcTable == nullptr)
	{
		bFindNpcTable = FALSE;
		TRACE(_T("#### AddObjectEventNpc Fail : [sid = %d], zone=%d #####\n"), pEvent->sIndex, zone_number);
		return FALSE;
	}

	bFindNpcTable = TRUE;

	CNpc* pNpc = new CNpc();

	pNpc->m_sNid = m_sMapEventNpc++;				// 서버 내에서의 고유 번호
	pNpc->m_sSid = (short) pEvent->sIndex;			// MONSTER(NPC) Serial ID

	pNpc->m_byMoveType = 100;
	pNpc->m_byInitMoveType = 100;
	bFindNpcTable = FALSE;

	pNpc->m_byMoveType = 0;
	pNpc->m_byInitMoveType = 0;

	pNpc->m_byBattlePos = 0;

	pNpc->m_fSecForMetor = 4.0f;					// 초당 갈 수 있는 거리..

	pNpc->Load(pNpcTable, false);

	//////// MONSTER POS ////////////////////////////////////////

	pNpc->m_sCurZone = zone_number;

	pNpc->m_byGateOpen = pEvent->sStatus;
	pNpc->m_fCurX = pEvent->fPosX;
	pNpc->m_fCurY = pEvent->fPosY;
	pNpc->m_fCurZ = pEvent->fPosZ;

	pNpc->m_nInitMinX = pEvent->fPosX - 1;
	pNpc->m_nInitMinY = pEvent->fPosZ - 1;
	pNpc->m_nInitMaxX = pEvent->fPosX + 1;
	pNpc->m_nInitMaxY = pEvent->fPosZ + 1;

	pNpc->m_sRegenTime = 10000 * 1000;	// 초(DB)단위-> 밀리세컨드로
	//pNpc->m_sRegenTime		= 30 * 1000;	// 초(DB)단위-> 밀리세컨드로
	pNpc->m_sMaxPathCount = 0;

	pNpc->m_ZoneIndex = -1;
	pNpc->m_byObjectType = SPECIAL_OBJECT;
	pNpc->m_bFirstLive = 1;		// 처음 살아난 경우로 해줘야 한다..
	//pNpc->m_ZoneIndex = GetZoneIndex(pNpc->m_sCurZone);
/*
	if(pNpc->m_ZoneIndex == -1)	{
		AfxMessageBox("Invaild zone Index!!");
		return FALSE;
	}	*/

	//pNpc->Init();
	if (!m_arNpc.PutData(pNpc->m_sNid, pNpc))
	{
		TRACE(_T("Npc PutData Fail - %d\n"), pNpc->m_sNid);
		delete pNpc;
		pNpc = nullptr;
	}

	m_TotalNPC = m_sMapEventNpc;

	return TRUE;
}

int CServerDlg::GetZoneIndex(int zone_id) const
{
	for (size_t i = 0; i < g_arZone.size(); i++)
	{
		MAP* pMap = g_arZone[i];
		if (pMap != nullptr
			&& pMap->m_nZoneNumber == zone_id)
			return i;
	}

	return -1;
}

int CServerDlg::GetServerNumber(int zone_id) const
{
	for (MAP* pMap : g_arZone)
	{
		if (pMap != nullptr
			&& pMap->m_nZoneNumber == zone_id)
			return pMap->m_nServerNo;
	}

	return -1;
}

void CServerDlg::ClostSocket(int zonenumber)
{
	CGameSocket* pSocket = nullptr;

	for (int i = 0; i < MAX_SOCKET; i++)
	{
		pSocket = (CGameSocket*) m_Iocport.m_SockArray[i];
		if (pSocket == nullptr)
			continue;

		if (pSocket->m_sSocketID == zonenumber)
		{
			//TRACE(_T("size = %d, socket_num = %d, i=%d \n"), size, pSocket->m_sSocketID, i);
			pSocket->CloseProcess();
			m_Iocport.RidIOCPSocket(pSocket->GetSocketID(), pSocket);
		}
	}
}

void CServerDlg::GetServerInfoIni()
{
	std::filesystem::path iniPath(GetProgPath().GetString());
	iniPath /= L"server.ini";

	CIni inifile;
	inifile.Load(iniPath);
	m_byZone = inifile.GetInt(_T("SERVER"), _T("ZONE"), 1);

	// TODO: This should be removed
	inifile.GetString(_T("ODBC"), _T("GAME_DSN"), _T("KN_online"), m_strGameDSN, _countof(m_strGameDSN));
	inifile.GetString(_T("ODBC"), _T("GAME_UID"), _T("knight"), m_strGameUID, _countof(m_strGameUID));
	inifile.GetString(_T("ODBC"), _T("GAME_PWD"), _T("knight"), m_strGamePWD, _countof(m_strGamePWD));

	std::string datasourceName = inifile.GetString("ODBC", "GAME_DSN", "KN_online");
	std::string datasourceUser = inifile.GetString("ODBC", "GAME_UID", "knight");
	std::string datasourcePass = inifile.GetString("ODBC", "GAME_PWD", "knight");

	DatabaseConnManager::SetDatasourceConfig(
		modelUtil::DbType::GAME,
		datasourceName, datasourceUser, datasourcePass);

	// Trigger a save to flush defaults to file.
	inifile.Save();
}

void CServerDlg::SendSystemMsg(char* pMsg, int zone, int type, int who)
{
	int send_index = 0;
	char buff[256] = {};
	short sLength = static_cast<short>(strlen(pMsg));

	SetByte(buff, AG_SYSTEM_MSG, send_index);
	SetByte(buff, type, send_index);				// 채팅형식
	SetShort(buff, who, send_index);				// 누구에게
	SetShort(buff, sLength, send_index);
	SetString(buff, pMsg, sLength, send_index);

	Send(buff, send_index, zone);
}

void CServerDlg::ResetBattleZone()
{
	TRACE(_T("ServerDlg - ResetBattleZone() : start \n"));

	for (MAP* pMap : g_arZone)
	{
		if (pMap== nullptr)
			continue;

		// 현재의 존이 던젼담당하는 존이 아니면 리턴..
		if (pMap->m_byRoomEvent == 0)
			continue;

		// 전체방이 클리어 되었다면
		// if (pMap->IsRoomStatusCheck())
		//	continue;

		pMap->InitializeRoom();
	}

	TRACE(_T("ServerDlg - ResetBattleZone() : end \n"));
}

MAP* CServerDlg::GetMapByIndex(int iZoneIndex) const
{
	if (iZoneIndex < 0
		|| iZoneIndex >= static_cast<int>(g_arZone.size()))
		return nullptr;

	return g_arZone[iZoneIndex];
}

MAP* CServerDlg::GetMapByID(int iZoneID) const
{
	for (MAP* pMap : g_arZone)
	{
		if (pMap != nullptr
			&& pMap->m_nZoneNumber == iZoneID)
			return pMap;
	}

	return nullptr;
}

CString CServerDlg::GetGameDBConnectionString()
{
	CString strConnection;
	strConnection.Format(
		_T("ODBC;DSN=%s;UID=%s;PWD=%s"),
		m_strGameDSN,
		m_strGameUID,
		m_strGamePWD);
	return strConnection;
}
