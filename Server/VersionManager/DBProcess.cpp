// DBProcess.cpp: implementation of the CDBProcess class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DBProcess.h"
#include "Define.h"

#include <db-library/Connection.h>
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

static void LogDatabaseError(const nanodbc::database_error& dbErr, const char* source)
{
	CString logLine;
	logLine.Format(_T("%hs: %hs\r\n"), source, dbErr.what());
	LogFileWrite(logLine);
}

CDBProcess::CDBProcess(CVersionManagerDlg* main)
	: _main(main)
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
		_conn = db::ConnectionManager::GetConnectionTo(modelUtil::DbType::GAME, DB_PROCESS_TIMEOUT);
		if (_conn == nullptr)
		{
			return FALSE;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.InitDatabase()");
		return FALSE;
	}
	return TRUE;
}

/// \brief checks if the managed connection is disconnected and attempts to reconnect if it is
/// \throws nanodbc::database_error
void CDBProcess::ReconnectIfDisconnected() noexcept(false)
{
	try
	{
		int8_t result = _conn->Reconnect();

		// general error
		if (result == -1)
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] failed to connect. this usually means the connection is null.");
		}

		// reconnect was necessary and successful
		if (result == 1)
		{
			CTime t = CTime::GetCurrentTime();
			LogFileWrite(std::format("ReconnectIfDisconnected(): reconnect successful on {:02}/{:02}/{:04}T{:02}:{:02}:{02}\r\n",
				t.GetMonth(), t.GetDay(), t.GetYear(), t.GetHour(), t.GetMinute(), t.GetSecond()));
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.ReconnectIfDisconnected()");
		throw;
	}
}

/// \brief loads the VERSION table into VersionManagerDlg.VersionList
/// \returns TRUE if successful, FALSE otherwise
BOOL CDBProcess::LoadVersionList(VersionInfoList* versionList)
{
	recordset_loader::STLMap loader(*versionList);
	if (!loader.Load_ForbidEmpty())
	{
		_main->ReportTableLoadError(loader.GetError(), __func__);
		return FALSE;
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
	
	try
	{
		ReconnectIfDisconnected();

		db::ModelRecordSet<model::TbUser> recordSet;

		auto stmt = recordSet.prepare(sql);
		if (stmt == nullptr)
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] statement could not be allocated");
		}

		stmt->bind(0, accountId);
		recordSet.execute();

		if (!recordSet.next())
			return AUTH_NOT_FOUND;

		model::TbUser user = {};
		recordSet.get_ref(user);

		if (user.Password != password)
		{
			// Use AUTH_NOT_FOUND here instead of AUTH_INVALID_PW
			// to ensure attackers have no way of identifying real accounts to bruteforce passwords on.
			return AUTH_NOT_FOUND;
		}

		if (user.Authority == AUTHORITY_BLOCK_USER)
		{
			return AUTH_BANNED;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.AccountLogin()");
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
	try
	{
		ReconnectIfDisconnected();

		nanodbc::statement stmt(*_conn->Conn, insert);
		stmt.bind(0, &version);
		stmt.bind(1, fileName);
		stmt.bind(2, compressName);
		stmt.bind(3, &historyVersion);

		nanodbc::result result = stmt.execute();
		if (result.affected_rows() > 0)
			return TRUE;
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.InsertVersion()");
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
	try
	{
		ReconnectIfDisconnected();

		nanodbc::statement stmt(*_conn->Conn, deleteQuery);
		stmt.bind(0, &version);

		nanodbc::result result = stmt.execute();
		if (result.affected_rows() > 0)
			return TRUE;
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.DeleteVersion()");
		return FALSE;
	}
	
	return FALSE;
}

/// \brief updates the server's concurrent user counts
/// \return TRUE on success, FALSE on failure
BOOL CDBProcess::LoadUserCountList()
{
	try
	{
		ReconnectIfDisconnected();

		db::ModelRecordSet<model::Concurrent> recordSet(_conn);
		recordSet.open();

		while (recordSet.next())
		{
			model::Concurrent concurrent = recordSet.get();

			int serverId = concurrent.ServerId - 1;
			if (serverId >= static_cast<int>(_main->ServerList.size()))
				continue;

			_main->ServerList[serverId]->sUserCount = concurrent.Zone1Count + concurrent.Zone2Count + concurrent.Zone3Count;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadUserCountList()");
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
	try
	{
		ReconnectIfDisconnected();

		db::ModelRecordSet<model::CurrentUser> recordSet(_conn);

		auto stmt = recordSet.prepare(sql);
		if (stmt == nullptr)
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] statement could not be allocated");
		}

		stmt->bind(0, accountId);
		recordSet.execute();

		if (!recordSet.next())
			return FALSE;

		model::CurrentUser user = recordSet.get();
		serverId = user.ServerId;
		strcpy(serverIp, user.ServerIP.c_str());
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.IsCurrentUser()");
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
	try
	{
		ReconnectIfDisconnected();

		storedProc::LoadPremiumServiceUser premium(_conn->Conn);
		premium.execute(accountId, &premiumType, &daysRemaining);

		*premiumDaysRemaining = static_cast<short>(daysRemaining);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadPremiumServiceUser()");
		return FALSE;
	}
	
	return TRUE;
}
