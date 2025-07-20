﻿// DBProcess.cpp: implementation of the CDBProcess class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DBProcess.h"
#include "Define.h"
#include "VersionManagerDlg.h"

#include <db-library/Connection.h>
#include <db-library/Exceptions.h>
#include <db-library/utils.h>

#include <nanodbc/nanodbc.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

import VersionManagerBinder;
import StoredProc;

// NOTE: Explicitly handled under DEBUG_NEW override
#include <db-library/RecordSetLoader_STLMap.h>
#include <db-library/StoredProc.h>

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

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
		auto conn = db::ConnectionManager::CreatePoolConnection(modelUtil::DbType::ACCOUNT, DB_PROCESS_TIMEOUT);
		if (conn == nullptr)
			return FALSE;
	}
	catch (const nanodbc::database_error& dbErr)
	{
		db::utils::LogDatabaseError(dbErr, "DBProcess.InitDatabase()");
		return FALSE;
	}

	return TRUE;
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
		db::ModelRecordSet<model::TbUser> recordSet;

		auto stmt = recordSet.prepare(sql);
		if (stmt == nullptr)
		{
			throw db::ApplicationError("statement could not be allocated");
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
		db::utils::LogDatabaseError(dbErr, "DBProcess.AccountLogin()");
		return AUTH_FAILED;
	}
	
	return AUTH_OK;
}

/// \brief attempts to create a new Version table record
/// \returns TRUE on success, FALSE on failure
BOOL CDBProcess::InsertVersion(int version, const char* fileName, const char* compressName, int historyVersion)
{
	using ModelType = model::Version;

	db::SqlBuilder<ModelType> sql;
	std::string insert = sql.InsertString();
	try
	{
		auto conn = db::ConnectionManager::CreatePoolConnection(ModelType::DbType(), DB_PROCESS_TIMEOUT);
		if (conn == nullptr)
			return FALSE;

		nanodbc::statement stmt = conn->CreateStatement(insert);
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
		db::utils::LogDatabaseError(dbErr, "DBProcess.InsertVersion()");
		return FALSE;
	}
	
	return FALSE;
}

/// \brief Deletes Version table entry tied to the specified key
/// \return TRUE on success, FALSE on failure
BOOL CDBProcess::DeleteVersion(int version)
{
	using ModelType = model::Version;

	db::SqlBuilder<ModelType> sql;
	std::string deleteQuery = sql.DeleteByIdString();
	try
	{
		auto conn = db::ConnectionManager::CreatePoolConnection(ModelType::DbType(), DB_PROCESS_TIMEOUT);
		if (conn == nullptr)
			return FALSE;

		nanodbc::statement stmt = conn->CreateStatement(deleteQuery);
		stmt.bind(0, &version);

		nanodbc::result result = stmt.execute();
		if (result.affected_rows() > 0)
			return TRUE;
	}
	catch (const nanodbc::database_error& dbErr)
	{
		db::utils::LogDatabaseError(dbErr, "DBProcess.DeleteVersion()");
	}
	
	return FALSE;
}

/// \brief updates the server's concurrent user counts
/// \return TRUE on success, FALSE on failure
BOOL CDBProcess::LoadUserCountList()
{
	try
	{
		db::ModelRecordSet<model::Concurrent> recordSet;
		recordSet.open();

		while (recordSet.next())
		{
			model::Concurrent concurrent = recordSet.get();

			int serverId = concurrent.ServerId - 1;
			if (serverId >= static_cast<int>(_main->ServerList.size()))
				continue;

			_main->ServerList[serverId]->sUserCount = concurrent.Zone1Count + concurrent.Zone2Count + concurrent.Zone3Count;
		}

		return TRUE;
	}
	catch (const nanodbc::database_error& dbErr)
	{
		db::utils::LogDatabaseError(dbErr, "DBProcess.LoadUserCountList()");
	}
	
	return FALSE;
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
		db::ModelRecordSet<model::CurrentUser> recordSet;

		auto stmt = recordSet.prepare(sql);
		if (stmt == nullptr)
		{
			throw db::ApplicationError("statement could not be allocated");
		}

		stmt->bind(0, accountId);
		recordSet.execute();

		if (!recordSet.next())
			return FALSE;

		model::CurrentUser user = recordSet.get();
		serverId = user.ServerId;
		strcpy(serverIp, user.ServerIP.c_str());

		return TRUE;
	}
	catch (const nanodbc::database_error& dbErr)
	{
		db::utils::LogDatabaseError(dbErr, "DBProcess.IsCurrentUser()");
	}

	return FALSE;
}

/// \brief calls LoadPremiumServiceUser and writes how many days of premium remain
/// to premiumDaysRemaining
/// \param accountId
/// \param[out] premiumDaysRemaining output value of remaining premium days
/// \return TRUE on success, FALSE on failure
BOOL CDBProcess::LoadPremiumServiceUser(const char* accountId, short* premiumDaysRemaining)
{
	int32_t premiumType = 0, // NOTE: we don't need this in the login server
		daysRemaining = 0;
	try
	{
		db::StoredProc<storedProc::LoadPremiumServiceUser> premium;
		premium.execute(accountId, &premiumType, &daysRemaining);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		db::utils::LogDatabaseError(dbErr, "DBProcess.LoadPremiumServiceUser()");
		return FALSE;
	}

	*premiumDaysRemaining = static_cast<short>(daysRemaining);
	return TRUE;
}
