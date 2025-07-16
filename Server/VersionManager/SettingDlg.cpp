// SettingDlg.cpp : implementation file
//

#include "stdafx.h"
#include "versionmanager.h"
#include "SettingDlg.h"
#include "DlgBrowsePath.h"
#include "VersionManagerDlg.h"

#include <string>

import VersionManagerModel;
namespace model = versionmanager_model;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#if defined(_UNICODE)
using PathType = std::wstring;
#else
using PathType = std::string;
#endif

/////////////////////////////////////////////////////////////////////////////
// CSettingDlg dialog

/// \brief standard constructor called by VersionManagerDlg::OnVersionSetting().
/// Constructs using Main->LastVersion
CSettingDlg::CSettingDlg(int lastVersion, CWnd* parent /*=NULL*/)
	: CDialog(IDD, parent)
{
	//{{AFX_DATA_INIT(CSettingDlg)
	LastVersion = lastVersion;
	IsRepackage = FALSE;
	IsAddAllFiles = FALSE;
	//}}AFX_DATA_INIT

	memset(DefaultPath, 0, sizeof(DefaultPath));

	Main = (CVersionManagerDlg*) parent;
}

/// \brief performs MFC data exchange
/// \see https://learn.microsoft.com/en-us/cpp/mfc/dialog-data-exchange?view=msvc-170
void CSettingDlg::DoDataExchange(CDataExchange* data)
{
	CDialog::DoDataExchange(data);
	//{{AFX_DATA_MAP(CSettingDlg)
	DDX_Control(data, IDC_PATH_EDIT, BasePathInput);
	DDX_Control(data, IDC_FILE_LIST, FileListTextArea);
	DDX_Control(data, IDC_PROGRESS, ProgressBar);
	DDX_Text(data, IDC_VERSION_EDIT, LastVersion);
	DDX_Check(data, IDC_CHECK, IsRepackage);
	DDX_Check(data, IDC_CHECK2, IsAddAllFiles);
	//}}AFX_DATA_MAP
}

/// \brief event mapping
BEGIN_MESSAGE_MAP(CSettingDlg, CDialog)
	//{{AFX_MSG_MAP(CSettingDlg)
	ON_BN_CLICKED(IDC_ADDFILE, OnAddFile)
	ON_BN_CLICKED(IDC_DELETEFILE, OnDeleteFile)
	ON_BN_CLICKED(IDC_COMPRESS, OnCompress)
	ON_BN_CLICKED(IDC_PATH_BROWSE, OnPathBrowse)
	ON_BN_CLICKED(IDC_REFRESH, OnRefresh)
	ON_BN_CLICKED(IDC_DBCSTEST, OnDbcstest)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CSettingDlg message handlers

/// \brief initializes the version settings dialogue box
BOOL CSettingDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	BasePathInput.SetWindowText(DefaultPath);

	ProgressBar.SetRange(0, 100);
	ProgressBar.SetPos(0);

	OnRefresh();

	return TRUE;  // return TRUE unless you set the focus to a control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

/// \brief closes the settings dialog window when the "OK" button is clicked
void CSettingDlg::OnOK()
{
	CDialog::OnOK();
}

/// \brief triggered when the Add button is clicked.  Opens a file picker and processes the selection
void CSettingDlg::OnAddFile()
{
	constexpr DWORD FilenameBufferSize = 512000;

	CFileDialog dlg(TRUE);
	PathType fileName, filePath, defaultPath;
	TCHAR tempstr1[_MAX_PATH] = {};
	std::vector<TCHAR> szFileName(FilenameBufferSize);
	int strSize = 0;

	UpdateData(TRUE);

	if (IsAddAllFiles)
	{
		if (AfxMessageBox(_T("All files of Base Path will be inserted."), MB_OKCANCEL) == IDCANCEL)
			return;

		BeginWaitCursor();
		FolderRecurse(DefaultPath);
		EndWaitCursor();
		return;
	}

	::SetCurrentDirectory(DefaultPath);

	dlg.m_ofn.lpstrInitialDir = DefaultPath;
	dlg.m_ofn.Flags |= OFN_ALLOWMULTISELECT | OFN_EXPLORER;
	dlg.m_ofn.lpstrFile = &szFileName[0];
	dlg.m_ofn.nMaxFile = FilenameBufferSize;

	if (dlg.DoModal() != IDOK)
		return;

	POSITION pos = dlg.GetStartPosition();
	while (pos != nullptr)
	{
		_tcscpy(tempstr1, dlg.GetNextPathName(pos));
		filePath = _tcslwr(tempstr1);
		defaultPath = _tcslwr(DefaultPath);
		strSize = defaultPath.size();
		fileName = filePath.substr(strSize);

		InsertProcess(&fileName[0]);
	}
}

/// \brief triggered when the delete button is clicked. Attempts to delete the
/// file selected in the FileListTextArea
void CSettingDlg::OnDeleteFile()
{
	CString delFileName, compressName, errMsg;
	std::string			fileName;
	std::vector<int>	selList;
	model::Version* delVersion = nullptr;

	int selCount = FileListTextArea.GetSelCount();
	if (selCount == 0)
	{
		AfxMessageBox(_T("File Not Selected."));
		return;
	}

	BeginWaitCursor();

	selList.reserve(selCount);
	for (int i = 0; i < FileListTextArea.GetCount(); i++)
	{
		if (FileListTextArea.GetSel(i))
		{
			selList.push_back(i);
		}
	}

	for (int i = 0; i < selCount; i++)
	{
		FileListTextArea.GetText(selList[i], delFileName);
		fileName = CT2A(delFileName);

		int delVersionId = -1;
		for (const auto& version : Main->VersionList)
		{
			if (version.second->FileName == fileName)
			{
				delVersionId = version.second->Number;
				break;
			}
		}

		delVersion = Main->VersionList.GetData(delVersionId);
		if (delVersion == nullptr)
			continue;

		if (!Main->DbProcess.DeleteVersion(delVersionId))
		{
			errMsg.Format(_T("%hs DB Delete Fail"), fileName.c_str());
			AfxMessageBox(errMsg);
			return;
		}

		// Restore
		if (delVersion->HistoryVersion > 0)
		{
			delVersion->Number = delVersion->HistoryVersion;
			delVersion->HistoryVersion = 0;

			compressName.Format(_T("patch%.4d.zip"), delVersion->Number);
			if (!Main->DbProcess.InsertVersion(delVersion->Number, fileName.c_str(), CT2A(compressName), 0))
			{
				Main->VersionList.DeleteData(delVersionId);
				errMsg.Format(_T("%hs DB Insert Fail"), fileName.c_str());
				AfxMessageBox(errMsg);
				return;
			}
		}
		else
		{
			Main->VersionList.DeleteData(delVersionId);
		}

		Sleep(10);
	}

	EndWaitCursor();

	OnRefresh();
}

/// \brief triggered when the Compress button is clicked. Attempts to
/// compress the selected entry, or all entries if CompressCheckbox is
/// checked
/// \see CompressCheckbox
void CSettingDlg::OnCompress()
{
	CString			pathName, fileName, errMsg, compressName, compressPath;
	DWORD size;
	CFile file;
	std::string		_fileName;

	UpdateData(TRUE);

	int count = FileListTextArea.GetCount();
	if (count == 0)
		return;

	BeginWaitCursor();

	compressName.Format(_T("patch%.4d.zip"), LastVersion);
	compressPath.Format(
		_T("%s\\%s"),
		DefaultPath,
		compressName.GetString());

	RepackingVersionList.clear();

	ZipArchive.Open(compressPath, CZipArchive::create);

	SetDlgItemText(IDC_STATUS, _T("Compressing.."));

	for (int i = 0; i < count; i++)
	{
		FileListTextArea.GetText(i, fileName);

		pathName = DefaultPath;
		pathName += fileName;
		if (!file.Open(pathName, CFile::modeRead))
		{
			errMsg.Format(_T("%s File Open Fail"), fileName.GetString());
			AfxMessageBox(errMsg);
			continue;
		}

		size = static_cast<DWORD>(file.GetLength());
		file.Close();

		if (!ZipArchive.AddNewFile(pathName, DefaultPath, -1, size))
		{
			errMsg.Format(_T("%s File Compress Fail"), fileName.GetString());
			AfxMessageBox(errMsg);
			continue;
		}

		ProgressBar.SetPos(i * 100 / count);

		_fileName = CT2A(fileName);

		int versionKey = -1;
		for (const auto& version : Main->VersionList)
		{
			if (version.second->FileName == _fileName)
			{
				versionKey = version.second->Number;
				break;
			}
		}

		model::Version* pInfo = Main->VersionList.GetData(versionKey);
		if (pInfo != nullptr)
			RepackingVersionList.insert(pInfo->HistoryVersion);
	}

	SetDlgItemText(IDC_STATUS, _T("Compressed"));

	ZipArchive.Close();

	// If unchecked, try repacking all items in history
	if (!IsRepackage)
	{
		if (!RepackingVersionList.empty())
			RepackingHistory();
	}

	ProgressBar.SetPos(100);

	EndWaitCursor();
}

/// \brief opens a folder picker dialog box
void CSettingDlg::OnPathBrowse()
{
	CDlgBrowsePath pathdlg;
	if (pathdlg.DoModal() != IDOK)
		return;

	_tcscpy_s(DefaultPath, pathdlg.m_szPath);
	BasePathInput.SetWindowText(DefaultPath);
}

/// \brief reloads the contents of FileListTextArea. Triggered by
/// the Refresh button and when focus leaves VersionInput
/// \see FileListTextArea
/// \see VersionInput
void CSettingDlg::OnRefresh()
{
	RefreshFileListTextArea();
	UpdateData(TRUE);
}

/// \brief destroys class resources when the window is being closed
BOOL CSettingDlg::DestroyWindow()
{
	RepackingVersionList.clear();
	return CDialog::DestroyWindow();
}

/// \brief repackages the versions in the RepackingVersionList
/// \see RepackingVersionList
void CSettingDlg::RepackingHistory()
{
	CString errMsg;

	SetDlgItemText(IDC_STATUS, _T("Repacking..."));

	for (const int version : RepackingVersionList)
	{
		if (!Repacking(version))
		{
			errMsg.Format(_T("%d Repacking Fail"), version);
			AfxMessageBox(errMsg);
		}
	}

	SetDlgItemText(IDC_STATUS, _T("Repacked"));
}

/// \brief attempts to repackage for the provided version number 
bool CSettingDlg::Repacking(int version)
{
	CString			filename, errmsg, compname, compfullpath;
	CFile file;

	compname.Format(_T("patch%.4d.zip"), version);
	compfullpath.Format(_T("%s\\%s"), DefaultPath, compname.GetString());

	ZipArchive.Open(compfullpath, CZipArchive::create);

	model::Version* pInfo = Main->VersionList.GetData(version);
	if (pInfo == nullptr)
	{
		errmsg.Format(_T("%d is not a valid version number"), version);
		AfxMessageBox(errmsg);
		ZipArchive.Close(true);
		return false;
	}

	filename.Format(_T("%s%hs"), DefaultPath, pInfo->FileName.c_str());
	if (!file.Open(filename, CFile::modeRead))
	{
		errmsg.Format(_T("%s File Open Fail"), filename.GetString());
		AfxMessageBox(errmsg);
		ZipArchive.Close(true);
		return false;
	}

	DWORD dwsize = static_cast<DWORD>(file.GetLength());
	file.Close();

	if (!ZipArchive.AddNewFile(filename, DefaultPath, -1, dwsize))
	{
		errmsg.Format(_T("%s File Compress Fail"), filename.GetString());
		AfxMessageBox(errmsg);
		ZipArchive.Close(true);
		return false;
	}

	ZipArchive.Close();

	return true;
}

/// \brief Attempts to insert a file as a patch
/// \details Checks if file name is used in the VERSION table.  If found,
/// attempts to overwrite that record
bool CSettingDlg::InsertProcess(const TCHAR* fileName)
{
	model::Version* pInfo1 = nullptr, *pInfo2 = nullptr;
	CString compressName, errMsg;
	std::string		_fileName;
	int historyVersion = 0;

	_fileName = CT2A(fileName);

	if (IsDBCSString(_fileName.c_str()))
	{
		errMsg.Format(_T("%s include DBCS character"), fileName);
		AfxMessageBox(errMsg);
		return false;
	}

	compressName.Format(_T("patch%.4d.zip"), LastVersion);

	pInfo1 = Main->VersionList.GetData(LastVersion);
	if (pInfo1 != nullptr)
	{
		historyVersion = pInfo1->HistoryVersion;
		if (!Main->DbProcess.DeleteVersion(LastVersion))
		{
			errMsg.Format(_T("%hs DB Delete Fail"), _fileName.c_str());
			AfxMessageBox(errMsg);
			return false;
		}
		Main->VersionList.DeleteData(LastVersion);
	}

	pInfo2 = new model::Version();
	pInfo2->Number = LastVersion;
	pInfo2->FileName = _fileName;
	pInfo2->CompressName = CT2A(compressName);
	pInfo2->HistoryVersion = historyVersion;
	if (!Main->VersionList.PutData(LastVersion, pInfo2))
	{
		delete pInfo2;
		return false;
	}

	if (!Main->DbProcess.InsertVersion(
		LastVersion,
		_fileName.c_str(),
		pInfo2->CompressName.c_str(),
		historyVersion))
	{
		Main->VersionList.DeleteData(LastVersion);

		errMsg.Format(_T("%hs DB Insert Fail"), _fileName.c_str());
		AfxMessageBox(errMsg);
		return false;
	}

	RefreshFileListTextArea();

	return true;
}

/// \brief attempts to find files recursively and insert them
/// \param folderName
/// \param isTestDBCS if true will check file names for DBCS, and will not insert files found
/// /// \see IsDBCSString()
void CSettingDlg::FolderRecurse(const TCHAR* folderName, bool isTestDBCS)
{
	CFileFind ff;
	PathType fileName, fullPath, defaultPath;
	TCHAR tempStr[_MAX_PATH] = {};

	::SetCurrentDirectory(folderName);

	BOOL bFind = ff.FindFile();
	while (bFind)
	{
		bFind = ff.FindNextFile();
		_tcscpy_s(tempStr, ff.GetFilePath());

		if (ff.IsDots())
			continue;

		if (ff.IsDirectory())
		{
			FolderRecurse(tempStr, isTestDBCS);
			continue;
		}

		// Test if the file includes a DBCS character, but don't attempt to insert
		if (isTestDBCS)
		{
			if (IsDBCSString(CT2A(tempStr)))
			{
				CString errmsg;
				errmsg.Format(_T("%s include DBCS character"), tempStr);
				AfxMessageBox(errmsg);
			}

			continue;
		}

		fullPath = _tcslwr(tempStr);
		defaultPath = _tcslwr(DefaultPath);
		fileName = fullPath.substr(defaultPath.size());

		InsertProcess(&fileName[0]);
	}
}

/// \brief determines if a string contains DBCS characters.
/// \details a DBCS character is a lead byte for the system default Windows ANSI code page (CP_ACP).
/// A lead byte is the first byte of a two-byte character in a double-byte character set (DBCS) for the code page.
/// \see https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-isdbcsleadbyte
bool CSettingDlg::IsDBCSString(const char* string)
{
	int total_count = static_cast<int>(strlen(string));
	for (int i = 0; i < total_count; i++)
	{
		if (IsDBCSLeadByte(string[i++]))
			return true;
	}

	return false;
}

/// \brief triggered when the DBCS Test button is clicked. Recursively checks
/// all the file names in the Base Directory for DBCS characters
/// \see IsDBCSString()
void CSettingDlg::OnDbcstest()
{
	FolderRecurse(DefaultPath, true);
	AfxMessageBox(_T("Test Done.."));
}

/// \brief clears the contents of FileListTextArea and reloads the default content
void CSettingDlg::RefreshFileListTextArea()
{
	FileListTextArea.ResetContent();
	LastVersion = 0;
	for (const auto& [_, pInfo] : Main->VersionList)
	{
		if (pInfo->Number > LastVersion)
		{
			LastVersion = pInfo->Number;
		}
		FileListTextArea.AddString(CA2T(pInfo->FileName.c_str()));
	}

	if (LastVersion > Main->LastVersion)
	{
		Main->LastVersion = LastVersion;
		Main->ResetOutputList();
	}
	SetDlgItemInt(IDC_VERSION_EDIT, LastVersion);
}
