﻿// VersionManagerDlg.h : header file
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

#include "resource.h"

/////////////////////////////////////////////////////////////////////////////
// CVersionManagerDlg dialog
typedef std::vector<_SERVER_INFO*>	ServerInfoList;

namespace recordset_loader
{
	struct Error;
}

class CVersionManagerDlg : public CDialog
{
public:
	const char* FtpUrl() const
	{
		return _ftpUrl;
	}

	const char* FtpPath() const
	{
		return _ftpPath;
	}

	int LastVersion() const
	{
		return _lastVersion;
	}

	CVersionManagerDlg(CWnd* parent = nullptr);	// standard constructor
	~CVersionManagerDlg();
	BOOL GetInfoFromIni();
	BOOL LoadVersionList();

	static CIOCPort	 IocPort;

	VersionInfoList	VersionList;
	ServerInfoList	ServerList;
	_NEWS			News;
	CDBProcess		DbProcess;

protected:
// Dialog Data
	//{{AFX_DATA(CVersionManagerDlg)
	enum { IDD = IDD_VERSIONMANAGER_DIALOG };
	CListBox _outputList;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CVersionManagerDlg)
	BOOL PreTranslateMessage(MSG* msg) override;
	BOOL DestroyWindow() override;

public:
	void ReportTableLoadError(const recordset_loader::Error& err, const char* source);

	/// \brief clears the OutputList text area and regenerates default output
	/// \see _outputList
	void ResetOutputList();

	// \brief updates the last/latest version and resets the output list
	void SetLastVersion(int lastVersion);

protected:
	virtual void DoDataExchange(CDataExchange* data);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	HICON			_icon;

	char			_ftpUrl[256];
	char			_ftpPath[256];

	/// \brief DefaultPath loaded from CONFIGURATION.DEFAULT_PATH
	std::string		_defaultPath;
	int				_lastVersion;

	// Generated message map functions
	//{{AFX_MSG(CVersionManagerDlg)
	BOOL OnInitDialog() override;
	afx_msg void OnTimer(UINT EventId);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnVersionSetting();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VERSIONMANAGERDLG_H__1563BFF5_5A54_47E5_A62C_7C123D067588__INCLUDED_)
