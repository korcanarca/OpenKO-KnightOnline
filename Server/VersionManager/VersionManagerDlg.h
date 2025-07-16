// VersionManagerDlg.h : header file
//

#if !defined(AFX_VERSIONMANAGERDLG_H__1563BFF5_5A54_47E5_A62C_7C123D067588__INCLUDED_)
#define AFX_VERSIONMANAGERDLG_H__1563BFF5_5A54_47E5_A62C_7C123D067588__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Define.h"
#include "Iocport.h"
#include "DBProcess.h"
#include <vector>
#include <string>

#include <shared/STLMap.h>

/////////////////////////////////////////////////////////////////////////////
// CVersionManagerDlg dialog
typedef CSTLMap <model::Version, std::string>	VersionInfoList;
typedef std::vector<_SERVER_INFO*>	ServerInfoList;

namespace recordset_loader
{
	struct Error;
}

class CVersionManagerDlg : public CDialog
{
// Construction
public:
	BOOL GetInfoFromIni();

	CVersionManagerDlg(CWnd* parent = nullptr);	// standard constructor
	~CVersionManagerDlg();

	static CIOCPort		IocPort;
	char				FtpUrl[256];
	char				FilePath[256];
	TCHAR				DefaultPath[_MAX_PATH];
	int					LastVersion;
	std::string			TableName; // TODO: remove?
	VersionInfoList		VersionList;
	ServerInfoList		ServerList;
	int					ServerCount;
	_NEWS				News;
	CDBProcess			DbProcess;

// Dialog Data
	//{{AFX_DATA(CVersionManagerDlg)
	enum { IDD = IDD_VERSIONMANAGER_DIALOG };
	CListBox	OutputList;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CVersionManagerDlg)
	virtual BOOL PreTranslateMessage(MSG* msg);
	virtual BOOL DestroyWindow();
	void ReportTableLoadError(const recordset_loader::Error& err, const char* source);
protected:
	virtual void DoDataExchange(CDataExchange* data);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON Icon;

	// Generated message map functions
	//{{AFX_MSG(CVersionManagerDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnVersionSetting();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VERSIONMANAGERDLG_H__1563BFF5_5A54_47E5_A62C_7C123D067588__INCLUDED_)
