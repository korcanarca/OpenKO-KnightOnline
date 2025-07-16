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

/// \brief attempts a connection with db::ConnectionManager to the ACCOUNT dbType
/// \throws nanodbc::database_error
/// \returns TRUE is successful, FALSE otherwise
BOOL CDBProcess::InitDatabase() noexcept(false)
{
	try
	{
		// TODO: modelUtil::DbType::ACCOUNT;  Currently all models are assigned to GAME
		conn = db::ConnectionManager::GetConnectionTo(modelUtil::DbType::GAME, DB_PROCESS_TIMEOUT);
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

/// \brief checks if the managed connection is disconnected and attempts to reconnect if it is
/// \throws nanodbc::database_error
void CDBProcess::ReConnectODBC() noexcept(false)
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
		throw dbErr;
	}
	logstr = _T("ReConnectODBC Success\r\n");
	LogFileWrite(logstr);
}

/// \brief loads the VERSION table into VersionManagerDlg.VersionList
/// \returns TRUE if successful, FALSE otherwise
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

/// \brief Attempts account authentication with a given accountId and password
/// \returns AUTH_OK on success, AUTH_NOT_FOUND on failure, AUTH_BANNED for banned accounts
int CDBProcess::AccountLogin(const char* accountId, const char* password)
{
	// TODO: Restore this, but it should be handled ideally in its own database, or a separate stored procedure.
	// As we're currently using a singular database (and we expect people to be using our database), and we have
	// no means of syncing this currently, we'll temporarily hack this to fetch and handle basic auth logic
	// without a procedure.
	db::SqlBuilder<model::TbUser> sql;
	sql.IsWherePK = true;
	nanodbc::statement stmt = nanodbc::statement(*conn->Conn, sql.SelectString());
	stmt.bind(0, accountId);
	try
	{
		ReConnectODBC();
		db::ModelRecordSet<model::TbUser> recordSet;
		std::shared_ptr<nanodbc::statement> prepStmt = std::make_shared<nanodbc::statement>(stmt);
		recordSet.open(prepStmt);
		if (!recordSet.next())
		{
			return AUTH_NOT_FOUND;
		}
		model::TbUser user = recordSet.get();
		if (user.Password != password)
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
	
	return AUTH_OK;
}

/// \brief attempts to create a new Version table record
/// \returns TRUE on success, FALSE on failure
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
		ReConnectODBC();
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

/// \brief Deletes Version table entry tied to the specified key
/// \return TRUE on success, FALSE on failure
BOOL CDBProcess::DeleteVersion(int version)
{
	db::SqlBuilder<model::Version> sql;
	std::string deleteQuery = sql.DeleteByIdString();
	nanodbc::statement stmt = nanodbc::statement(*conn->Conn, deleteQuery);
	stmt.bind(0, &version);
	try
	{
		ReConnectODBC();
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
/// \return TRUE on success, FALSE on failure
BOOL CDBProcess::LoadUserCountList()
{
	db::ModelRecordSet<model::Concurrent> recordSet;
	try
	{
		ReConnectODBC();
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
		return FALSE;
	}
	
	return TRUE;
}

/// \brief Checks to see if a user is present in CURRENTUSER for a particular server
/// writes to serverIp and serverId
/// \param accountId
/// \param[out] serverIp output of the server IP the user is connected to
/// \param[out] serverId output of the serverId the user is connected to
/// \return TRUE on success, FALSE on failure
BOOL CDBProcess::IsCurrentUser(const char* accountId, char* serverIp, int& serverId)
{
	db::SqlBuilder<model::CurrentUser> sql;
	sql.IsWherePK = true;
	nanodbc::statement stmt = nanodbc::statement(*conn->Conn, sql.SelectString());
	stmt.bind(0, accountId);
	try
	{
		ReConnectODBC();
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

/// \brief calls LoadPremiumServiceUser and writes how many days of premium remain
/// to premiumDaysRemaining
/// \param accountId
/// \param[out] premiumDaysRemaining output value of remaining premium days
/// \return TRUE on success, FALSE on failure
BOOL CDBProcess::LoadPremiumServiceUser(const char* accountId, short* premiumDaysRemaining)
{
	int32_t premiumType = 0; // NOTE: we don't need this in the login server
	int32_t daysRemaining = 0;
	storedProc::LoadPremiumServiceUser premium(conn->Conn);
	try
	{
		ReConnectODBC();
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
		return FALSE;
	}
	
	return TRUE;
}
