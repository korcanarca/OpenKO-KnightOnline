// VersionManagerDlg.cpp : implementation file
//

#include "stdafx.h"
#include "VersionManager.h"
#include "VersionManagerDlg.h"
#include "IOCPSocket2.h"
#include "SettingDlg.h"
#include "User.h"

#include <shared/Ini.h>

#include <db-library/ConnectionManager.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#include "db-library/RecordSetLoader.h"

import VersionManagerBinder;

using namespace db;

CIOCPort CVersionManagerDlg::IocPort;

/////////////////////////////////////////////////////////////////////////////
// CVersionManagerDlg dialog

CVersionManagerDlg::CVersionManagerDlg(CWnd* parent)
	: CDialog(IDD, parent),
	DbProcess(this)
{
	//{{AFX_DATA_INIT(CVersionManagerDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT

	memset(FtpUrl, 0, sizeof(FtpUrl));
	memset(FilePath, 0, sizeof(FilePath));
	LastVersion = 0;

	Icon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	ConnectionManager::Create();
}

CVersionManagerDlg::~CVersionManagerDlg()
{
	ConnectionManager::Destroy();
}

void CVersionManagerDlg::DoDataExchange(CDataExchange* data)
{
	CDialog::DoDataExchange(data);
	//{{AFX_DATA_MAP(CVersionManagerDlg)
	DDX_Control(data, IDC_LIST1, OutputList);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CVersionManagerDlg, CDialog)
	//{{AFX_MSG_MAP(CVersionManagerDlg)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_SETTING, OnVersionSetting)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CVersionManagerDlg message handlers

BOOL CVersionManagerDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(Icon, TRUE);			// Set big icon
	SetIcon(Icon, FALSE);		// Set small icon
	
	IocPort.Init(MAX_USER, CLIENT_SOCKSIZE, 1);

	for (int i = 0; i < MAX_USER; i++)
		IocPort.m_SockArrayInActive[i] = new CUser(this);

	if (!IocPort.Listen(_LISTEN_PORT))
	{
		AfxMessageBox(_T("FAIL TO CREATE LISTEN STATE"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!GetInfoFromIni())
	{
		AfxMessageBox(_T("Ini File Info Error!!"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!DbProcess.InitDatabase())
	{
		AfxMessageBox(_T("Database Connection Fail!!"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	if (!DbProcess.LoadVersionList())
	{
		AfxMessageBox(_T("Load Version List Fail!!"));
		AfxPostQuitMessage(0);
		return FALSE;
	}

	ResetOutputList();

	::ResumeThread(IocPort.m_hAcceptThread);

	return TRUE;  // return TRUE unless you set the focus to a control
}

BOOL CVersionManagerDlg::GetInfoFromIni()
{
	std::filesystem::path iniPath(GetProgPath().GetString());
	iniPath /= L"Version.ini";

	CIni ini(iniPath);

	ini.GetString("DOWNLOAD", "URL", "127.0.0.1", FtpUrl, _countof(FtpUrl));
	ini.GetString("DOWNLOAD", "PATH", "/", FilePath, _countof(FilePath));
	
	// TODO: KN_online should be Knight_Account
	std::string datasourceName = ini.GetString(ini::ODBC, ini::DSN, "KN_online");
	std::string datasourceUser = ini.GetString(ini::ODBC, ini::UID, "knight");
	std::string datasourcePass = ini.GetString(ini::ODBC, ini::PWD, "knight");

	// TODO: modelUtil::DbType::ACCOUNT;  Currently all models are assigned to GAME
	ConnectionManager::SetDatasourceConfig(
		modelUtil::DbType::GAME,
		datasourceName, datasourceUser, datasourcePass);

	DefaultPath = ini.GetString(ini::CONFIGURATION, ini::DEFAULT_PATH, "");
	ServerCount = ini.GetInt(ini::SERVER_LIST, ini::COUNT, 1);

	if (strlen(FtpUrl) == 0
		|| strlen(FilePath) == 0)
		return FALSE;

	if (datasourceName.length() == 0
		// TODO: Should we not validate UID/Pass length?  Would that allow Windows Auth?
		|| datasourceUser.length() == 0
		|| datasourcePass.length() == 0)
		return FALSE;

	if (ServerCount <= 0)
		return FALSE;

	char key[20] = {};
	ServerList.reserve(20);

	for (int i = 0; i < ServerCount; i++)
	{
		_SERVER_INFO* pInfo = new _SERVER_INFO;

		snprintf(key, sizeof(key), "SERVER_%02d", i);
		ini.GetString("SERVER_LIST", key, "127.0.0.1", pInfo->strServerIP, _countof(pInfo->strServerIP));

		snprintf(key, sizeof(key), "NAME_%02d", i);
		ini.GetString("SERVER_LIST", key, "TEST|Server 1", pInfo->strServerName, _countof(pInfo->strServerName));

		snprintf(key, sizeof(key), "ID_%02d", i);
		pInfo->sServerID = static_cast<short>(ini.GetInt("SERVER_LIST", key, 1));

		snprintf(key, sizeof(key), "USER_LIMIT_%02d", i);
		pInfo->sUserLimit = static_cast<short>(ini.GetInt("SERVER_LIST", key, MAX_USER));

		ServerList.push_back(pInfo);
	}

	// Read news from INI (max 3 blocks)
	std::stringstream ss;
	std::string title, message;

	News.Size = 0;
	for (int i = 0; i < MAX_NEWS_COUNT; i++)
	{
		snprintf(key, sizeof(key), "TITLE_%02d", i);
		title = ini.GetString("NEWS", key, "");
		if (title.empty())
			continue;

		snprintf(key, sizeof(key), "MESSAGE_%02d", i);
		message = ini.GetString("NEWS", key, "");
		if (message.empty())
			continue;

		ss << title;
		ss.write(NEWS_MESSAGE_START, sizeof(NEWS_MESSAGE_START));
		ss << message;
		ss.write(NEWS_MESSAGE_END, sizeof(NEWS_MESSAGE_END));
	}

	const std::string newsContent = ss.str();
	if (!newsContent.empty())
	{
		if (newsContent.size() > sizeof(News.Content))
		{
			AfxMessageBox(_T("News too long"));
			return FALSE;
		}

		memcpy(&News.Content, newsContent.c_str(), newsContent.size());
		News.Size = static_cast<short>(newsContent.size());
	}

	// Trigger a save to flush defaults to file.
	ini.Save();

	return TRUE;
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CVersionManagerDlg::OnPaint()
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
		dc.DrawIcon(x, y, Icon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CVersionManagerDlg::OnQueryDragIcon()
{
	return (HCURSOR) Icon;
}

BOOL CVersionManagerDlg::PreTranslateMessage(MSG* msg)
{
	if (msg->message == WM_KEYDOWN)
	{
		if (msg->wParam == VK_RETURN
			|| msg->wParam == VK_ESCAPE)
			return TRUE;
	}

	return CDialog::PreTranslateMessage(msg);
}

BOOL CVersionManagerDlg::DestroyWindow()
{
	if (!VersionList.IsEmpty())
		VersionList.DeleteAllData();

	for (_SERVER_INFO* pInfo : ServerList)
		delete pInfo;
	ServerList.clear();

	return CDialog::DestroyWindow();
}

void CVersionManagerDlg::OnVersionSetting() 
{
	CSettingDlg	setdlg(LastVersion, this);

	CString conv = DefaultPath.c_str();
	_tcscpy_s(setdlg.DefaultPath, conv);
	if (setdlg.DoModal() != IDOK)
		return;

	std::filesystem::path iniPath(GetProgPath().GetString());
	iniPath /= L"Version.ini";

	CIni ini(iniPath);
	DefaultPath = ini.GetString(ini::CONFIGURATION, ini::DEFAULT_PATH, "");
	ini.Save();
}

void CVersionManagerDlg::ReportTableLoadError(const recordset_loader::Error& err, const char* source)
{
	CString msg;
	msg.Format(_T("%hs failed: %hs"), source, err.Message.c_str());
	AfxMessageBox(msg);
}

/// \brief clears the OutputList text area and regenerates default output
/// \see OutputList
void CVersionManagerDlg::ResetOutputList()
{
	OutputList.ResetContent();

	// print the ODBC connection string
	// TODO: modelUtil::DbType::ACCOUNT;  Currently all models are assigned to GAME
	std::string odbcString = ConnectionManager::OdbcConnString(modelUtil::DbType::GAME);
	CString strConnection = odbcString.c_str();
	OutputList.AddString(strConnection);

	// print the current version
	CString version;
	version.Format(_T("Latest Version : %d"), LastVersion);
	OutputList.AddString(version);
}
