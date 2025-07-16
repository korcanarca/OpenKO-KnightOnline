// DBProcess.h: interface for the CDBProcess class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_DBPROCESS_H__D7F54E57_B37F_40C8_9E76_8C9F083842BF__INCLUDED_)
#define AFX_DBPROCESS_H__D7F54E57_B37F_40C8_9E76_8C9F083842BF__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <memory>

#include "db-library/ConnectionManager.h"

class CVersionManagerDlg;
class CDBProcess
{
public:
	BOOL LoadPremiumServiceUser(const char* accountId, short* premiumDaysRemaining);
	BOOL IsCurrentUser(const char* accountId, char* serverIp, int& serverId);
	void ReConnectODBC();
	BOOL DeleteVersion(const char* fileName);
	BOOL InsertVersion(int version, const char* fileName, const char* compressName, int historyVersion);
	BOOL InitDatabase();
	int AccountLogin(const char* id, const char* pwd);
	BOOL LoadVersionList();
	BOOL LoadUserCountList();

	CDBProcess(CVersionManagerDlg* main);
	virtual ~CDBProcess();

	CDatabase	VersionDB;
	CVersionManagerDlg* Main;
private:
	std::shared_ptr<db::ConnectionManager::Connection> conn;
};

#endif // !defined(AFX_DBPROCESS_H__D7F54E57_B37F_40C8_9E76_8C9F083842BF__INCLUDED_)
