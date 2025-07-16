// DBProcess.cpp: implementation of the CDBProcess class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "versionmanager.h"
#include "Define.h"
#include "DBProcess.h"

#include <nanodbc/nanodbc.h>

#include "VersionManagerDlg.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

import VersionManagerBinder;
import StoredProc;

// NOTE: Explicitly handled under DEBUG_NEW override
#include <db-library/RecordSetLoader_STLMap.h>


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CDBProcess::CDBProcess(CVersionManagerDlg* main)
	: Main(main)
{
}

CDBProcess::~CDBProcess()
{
}

BOOL CDBProcess::InitDatabase() noexcept(false)
{
	try
	{
		conn = db::ConnectionManager::GetConnectionTo(modelUtil::DbType::ACCOUNT, DB_PROCESS_TIMEOUT);
		if (conn == nullptr)
		{
			return FALSE;
		}
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.InitDatabase(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
		return FALSE;
	}
	return TRUE;
}

void CDBProcess::ReConnectODBC()
{
	CString logstr;
	CTime t = CTime::GetCurrentTime();
	logstr.Format(_T("Try ReConnectODBC... %02d/%02d %02d:%02d\r\n"), t.GetMonth(), t.GetDay(), t.GetHour(), t.GetMinute());
	LogFileWrite(logstr);
	try
	{
		conn->Reconnect();
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.ReConnectODBC(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
	}
}

BOOL CDBProcess::LoadVersionList()
{
	recordset_loader::STLMap loader(Main->VersionList);
	if (!loader.Load_ForbidEmpty())
	{
		Main->ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
	}

	Main->LastVersion = 0;

	for (const auto& [_, pInfo] : Main->VersionList)
	{
		if (Main->LastVersion < pInfo->Number)
			Main->LastVersion = pInfo->Number;
	}

	return TRUE;
}

int CDBProcess::AccountLogin(const char* id, const char* pwd)
{
	// TODO: Restore this, but it should be handled ideally in its own database, or a separate stored procedure.
	// As we're currently using a singular database (and we expect people to be using our database), and we have
	// no means of syncing this currently, we'll temporarily hack this to fetch and handle basic auth logic
	// without a procedure.
#if 1

	db::SqlBuilder<model::TbUser> sql;
	sql.IsWherePK = true;
	nanodbc::statement stmt = nanodbc::statement(*conn->Conn, sql.SelectString());
	stmt.bind(0, id);
	try
	{
		conn->Reconnect();
		db::ModelRecordSet<model::TbUser> recordSet;
		std::shared_ptr<nanodbc::statement> prepStmt = std::make_shared<nanodbc::statement>(stmt);
		recordSet.open(prepStmt);
		if (!recordSet.next())
		{
			return AUTH_NOT_FOUND;
		}
		model::TbUser user = recordSet.get();
		if (user.Password != pwd)
		{
			return AUTH_NOT_FOUND;
		}
		if (user.Authority == AUTHORITY_BLOCK_USER)
		{
			return AUTH_BANNED;
		}
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.AccountLogin(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
		return AUTH_FAILED;
	}
	
#else
	// retVal
	// 0 = failed login
	// 1 = account with no characters
	// 2 = account with karus characters
	// 3 = account with el morad characters
	// -1 = not modified by proc
	int16_t retVal = -1;
	storedProc::AccountLogin login(conn->Conn);
	try
	{
		conn->Reconnect();
		std::weak_ptr<nanodbc::result> weak_result = login.execute(id, pwd, &retVal);
		std::shared_ptr<nanodbc::result> result = weak_result.lock();
		// TODO: This stored proc isn't currently written for this task
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.AccountLogin(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
		return AUTH_FAILED;
	}
#endif
	return AUTH_OK;
}

/// \brief attempts to create a new Version table record
BOOL CDBProcess::InsertVersion(int version, const char* fileName, const char* compressName, int historyVersion)
{

	db::SqlBuilder<model::Version> sql;
	std::string insert = sql.InsertString();
	nanodbc::statement stmt = nanodbc::statement(*conn->Conn, insert);
	stmt.bind(0, &version);
	stmt.bind(1, fileName);
	stmt.bind(2, compressName);
	stmt.bind(3, &historyVersion);
	try
	{
		conn->Reconnect();
		nanodbc::result result = stmt.execute();
		if (result.affected_rows() > 0)
		{
			return TRUE;
		}
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.InsertVersion(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
		return FALSE;
	}
	
	return FALSE;
}

/// \brief Deletes Version table entries associated with the associated fileName
BOOL CDBProcess::DeleteVersion(const char* fileName)
{
	db::SqlBuilder<model::Version> sql;
	std::string deleteQuery = "DELETE FROM [" + model::Version::TableName() + "] WHERE [strFileName] = ?";
	nanodbc::statement stmt = nanodbc::statement(*conn->Conn, deleteQuery);
	stmt.bind(0, fileName);
	try
	{
		conn->Reconnect();
		nanodbc::result result = stmt.execute();
		if (result.affected_rows() > 0)
		{
			return TRUE;
		}
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.DeleteVersion(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
		return FALSE;
	}
	
	return FALSE;
}

/// \brief updates the server's concurrent user counts
BOOL CDBProcess::LoadUserCountList()
{
	db::ModelRecordSet<model::Concurrent> recordSet;
	try
	{
		conn->Reconnect();
		recordSet.open();
		while (recordSet.next())
		{
			model::Concurrent concurrent = recordSet.get();
			if (concurrent.ServerId - 1 < Main->ServerCount)
				Main->ServerList[concurrent.ServerId - 1]->sUserCount = concurrent.Zone1Count + concurrent.Zone2Count + concurrent.Zone3Count;
		}
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.LoadUserCountList(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
		return AUTH_FAILED;
	}
	
	return TRUE;
}

/// \brief Checks to see if a user is present in CURRENTUSER for a particular server
/// writes to serverIp and serverId
BOOL CDBProcess::IsCurrentUser(const char* accountId, char* serverIp, int& serverId)
{
	db::SqlBuilder<model::CurrentUser> sql;
	sql.IsWherePK = true;
	nanodbc::statement stmt = nanodbc::statement(*conn->Conn, sql.SelectString());
	stmt.bind(0, accountId);
	try
	{
		conn->Reconnect();
		db::ModelRecordSet<model::CurrentUser> recordSet;
		std::shared_ptr<nanodbc::statement> prepStmt = std::make_shared<nanodbc::statement>(stmt);
		recordSet.open(prepStmt);
		if (!recordSet.next())
		{
			return FALSE;
		}
		model::CurrentUser user = recordSet.get();
		serverId = user.ServerId;
		strcpy(serverIp, user.ServerIP.c_str());
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.IsCurrentUser(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
		return FALSE;
	}

	return TRUE;
}

/// \brief calls LoadPremiumServiceUser and returns how many days of premium remain
BOOL CDBProcess::LoadPremiumServiceUser(const char* accountId, short* premiumDaysRemaining)
{
	int32_t premiumType = 0; // NOTE: we don't need this in the login server
	int32_t daysRemaining = 0;
	storedProc::LoadPremiumServiceUser premium(conn->Conn);
	try
	{
		conn->Reconnect();
		std::weak_ptr<nanodbc::result> weak_result = premium.execute(accountId, &premiumType, &daysRemaining);
		short sDays = static_cast<short>(daysRemaining);
		premiumDaysRemaining = &sDays;
	}
	catch (nanodbc::database_error dbErr)
	{
		std::string logLine = "DBProcess.LoadPremiumServiceUser(): ";
		logLine += dbErr.what();
		logLine += "\n";
		LogFileWrite(logLine);
		return AUTH_FAILED;
	}
	
	return TRUE;
}
