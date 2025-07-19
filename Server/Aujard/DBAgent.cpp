// _dbAgent.cpp: implementation of the CDBAgent class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Aujard.h"
#include "DBAgent.h"

#include <format>
#include <nanodbc/nanodbc.h>

#include "AujardDlg.h"
#include <db-library/Connection.h>
#include <db-library/ModelRecordSet.h>
#include <db-library/SqlBuilder.h>

#include <shared/StringUtils.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

import AujardBinder;
import StoredProc;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

extern CRITICAL_SECTION g_LogFileWrite;

static void LogDatabaseError(const nanodbc::database_error& dbErr, const char* source)
{
	CString logLine;
	logLine.Format(_T("%hs: %hs\r\n"), source, dbErr.what());
	LogFileWrite(logLine);
}

CDBAgent::CDBAgent()
{
}

CDBAgent::~CDBAgent()
{
}

/// \brief attempts connections with db::ConnectionManager for the needed dbTypes
/// \returns true is successful, false otherwise
bool CDBAgent::InitDatabase()
{
	//	_main DB Connecting..
	/////////////////////////////////////////////////////////////////////////////////////
	_main = (CAujardDlg*) AfxGetApp()->GetMainWnd();

	// initialize each connection inside its own try/catch block so we can catch
	// exceptions per-connection
	try
	{
		_gameConn1 = db::ConnectionManager::GetConnectionTo(modelUtil::DbType::GAME, DB_PROCESS_TIMEOUT);
		if (_gameConn1 == nullptr)
		{
			return false;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.InitDatabase(_gameConn1)");
		return false;
	}

	try
	{
		// TODO: modelUtil::DbType::ACCOUNT; Currently all models are assigned to GAME
		_accountConn1 = db::ConnectionManager::GetConnectionTo(modelUtil::DbType::GAME, DB_PROCESS_TIMEOUT);
		if (_accountConn1 == nullptr)
		{
			return false;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.InitDatabase(_accountConn1)");
		return false;
	}

	try
	{
		// TODO: modelUtil::DbType::ACCOUNT; Currently all models are assigned to GAME
		_accountConn2 = db::ConnectionManager::GetConnectionTo(modelUtil::DbType::GAME, DB_PROCESS_TIMEOUT);
		if (_accountConn2 == nullptr)
		{
			return false;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.InitDatabase(_accountConn2)");
		return false;
	}

	return true;
}

/// \brief checks if the managed connection is disconnected and attempts to reconnect if it is
/// \param conn the connection to attempt a reconnect on
/// \throws nanodbc::database_error
void CDBAgent::ReConnectODBC(db::Connection* conn) noexcept(false)
{
	try
	{
		int8_t result = conn->Reconnect();
		if (result == -1)
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] failed to connect. This usually means the connection is null");
		}

		// reconnect was necessary and successful
		if (result == 1)
		{
			CTime t = CTime::GetCurrentTime();
			LogFileWrite(std::format("ReConnectODBC(): reconnect successful on {:02}/{:02}/{:04}T{:02}:{:02}:{02}\r\n",
				t.GetMonth(), t.GetDay(), t.GetYear(), t.GetHour(), t.GetMinute(), t.GetSecond()));
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBAgent.ReConnectODBC()");
		throw;
	}
}

/// \brief resets a UserData[userId] record.  Called after logout actions
/// \see UserData
void CDBAgent::ResetUserData(int userId)
{
	_USER_DATA* user = UserData[userId];
	if (user == nullptr)
		return;

	memset(user, 0, sizeof(_USER_DATA));
	user->m_bAuthority = AUTHORITY_USER;
}

/// \brief populates UserData[userId] from the database
bool CDBAgent::LoadUserData(const char* accountId, const char* charId, int userId)
{
	// verify UserData[userId] is valid for load
	_USER_DATA* user =  UserData[userId];
	if (user == nullptr)
	{
		LogFileWrite(std::format("LoadUserData(): UserData[{}] not found for charId={}\r\n", userId, charId));
		return false;
	}

	if (user->m_bLogout)
	{
		LogFileWrite(std::format("LoadUserData(): logout error: charId={}, logout={} \r\n", charId, user->m_bLogout));
		return false;
	}

	uint8_t Nation, Race, HairColor, Rank, Title, Level;
	uint32_t Exp, Loyalty, Gold, PX, PZ, PY, dwTime, MannerPoint, LoyaltyMonthly;
	uint8_t Face, City, Fame, Authority, Points;
	int16_t Hp, Mp, Sp, Class, Bind, Knights, QuestCount;
	uint8_t Str, Sta, Dex, Intel, Cha, Zone;
	char strSkill[10] = {},
		strItem[400] = {},
		strSerial[400] = {},
		strQuest[400] = {};
	
	int16_t rowCount = 0;
	try
	{
		DBProcessNumber(2);
		ReConnectODBC(_gameConn1.get());

		storedProc::LoadUserData proc(_gameConn1->Conn);
		auto weak_result = proc.execute(accountId, charId, &rowCount);
		auto result = weak_result.lock();
		if (result == nullptr)
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] expected result set");
		}

		if (!result->next())
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] expected row in result set");
		}

		// THIS IS WHERE THE FUN STARTS
		Nation = static_cast<uint8_t>(result->get<int16_t>(0));
		Race = static_cast<uint8_t>(result->get<int16_t>(1));
		Class = result->get<int16_t>(2);
		HairColor = static_cast<uint8_t>(result->get<int16_t>(3));
		Rank = static_cast<uint8_t>(result->get<int16_t>(4));
		Title = static_cast<uint8_t>(result->get<int16_t>(5));
		Level = static_cast<uint8_t>(result->get<int16_t>(6));
		Exp = result->get<uint32_t>(7);
		Loyalty = result->get<uint32_t>(8);
		Face = static_cast<uint8_t>(result->get<int16_t>(9));
		City = static_cast<uint8_t>(result->get<int16_t>(10));
		Knights = result->get<int16_t>(11);
		Fame = static_cast<uint8_t>(result->get<int16_t>(12));
		Hp = result->get<int16_t>(13);
		Mp = result->get<int16_t>(14);
		Sp = result->get<int16_t>(15);
		Str = static_cast<uint8_t>(result->get<int16_t>(16));
		Sta = static_cast<uint8_t>(result->get<int16_t>(17));
		Dex = static_cast<uint8_t>(result->get<int16_t>(18));
		Intel = static_cast<uint8_t>(result->get<int16_t>(19));
		Cha = static_cast<uint8_t>(result->get<int16_t>(20));
		Authority = static_cast<uint8_t>(result->get<int16_t>(21));
		Points = static_cast<uint8_t>(result->get<int16_t>(22));
		Gold = result->get<uint32_t>(23);
		Zone = static_cast<uint8_t>(result->get<int16_t>(24));
		Bind = result->get<int16_t>(25);
		PX = result->get<uint32_t>(26);
		PZ = result->get<uint32_t>(27);
		PY = result->get<uint32_t>(28);
		dwTime = result->get<uint32_t>(29);
			
		std::vector<uint8_t> temp(sizeof(strSkill));
		result->get_ref(30, temp);
		std::ranges::copy(temp, strSkill);
		temp.clear();

		result->get_ref(31, temp);
		std::ranges::copy(temp, strItem);
		temp.clear();
			
		result->get_ref(32, temp);
		std::ranges::copy(temp, strSerial);
		temp.clear();
			
		QuestCount = result->get<int16_t>(33);
			
		result->get_ref(34, temp);
		std::ranges::copy(temp, strQuest);
		temp.clear();
			
		MannerPoint = result->get<uint32_t>(35);
		LoyaltyMonthly = result->get<uint32_t>(36);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadUserData()");
		return false;
	}
	
	// data successfully loaded from the database, copy to UserData record
	if (strcpy_s(user->m_id, charId))
	{
		LogFileWrite(std::format("LoadUserData(): failed to write charId(len: {}, val: {}) to user->m_id\r\n",
			std::strlen(charId), charId));
		return false;
	}

	user->m_bZone = Zone;
	user->m_curx = static_cast<float>(PX / 100);
	user->m_curz = static_cast<float>(PZ / 100);
	user->m_cury = static_cast<float>(PY / 100);
	user->m_dwTime = dwTime+1;
	user->m_bNation = Nation;
	user->m_bRace = Race;
	user->m_sClass = Class;
	user->m_bHairColor = HairColor;
	user->m_bRank = Rank;
	user->m_bTitle = Title;
	user->m_bLevel = Level;
	user->m_iExp = Exp;
	user->m_iLoyalty = Loyalty;
	user->m_bFace = Face;
	user->m_bCity = City;
	user->m_bKnights = Knights;
	user->m_bFame = Fame;
	user->m_sHp = Hp;
	user->m_sMp = Mp;
	user->m_sSp = Sp;
	user->m_bStr = Str;
	user->m_bSta = Sta;
	user->m_bDex = Dex;
	user->m_bIntel = Intel;
	user->m_bCha = Cha;
	user->m_bAuthority = Authority;
	user->m_bPoints = Points;
	user->m_iGold = Gold;
	user->m_sBind = Bind;
	user->m_dwTime = dwTime + 1;
	user->m_iMannerPoint = MannerPoint;
	user->m_iLoyaltyMonthly = LoyaltyMonthly;

#ifdef _DEBUG
	CTime t = CTime::GetCurrentTime();
	LogFileWrite(std::format("[LoadUserData {:02}-{:02}-{:02}]: name={}, nation={}, zone={}, level={}, exp={}, money={}\r\n",
		t.GetHour(), t.GetMinute(), t.GetSecond(), charId, Nation, Zone, Level, Exp, Gold));
#endif	

	int index = 0, serial_index = 0;
	for (int i = 0; i < 9; i++)
		user->m_bstrSkill[i] = GetByte(strSkill, index);

	index = 0;
	DWORD itemId = 0;
	short count = 0, duration = 0;
	int64_t serial = 0;
	model::Item* pTable = nullptr;

	// Equip slots + inventory slots (14+28=42)
	for (int i = 0; i < HAVE_MAX + SLOT_MAX; i++)
	{
		itemId = GetDWORD(strItem, index);
		duration = GetShort(strItem, index);
		count = GetShort(strItem, index);

		serial = GetInt64(strSerial, serial_index);		// item serial number

		pTable = _main->ItemArray.GetData(itemId);

		if (pTable != nullptr)
		{
			user->m_sItemArray[i].nNum = itemId;
			user->m_sItemArray[i].sDuration = duration;
			user->m_sItemArray[i].nSerialNum = serial;
			user->m_sItemArray[i].byFlag = 0;
			user->m_sItemArray[i].sTimeRemaining = 0;

			if (count > ITEMCOUNT_MAX)
			{
				user->m_sItemArray[i].sCount = ITEMCOUNT_MAX;
			}
			else if (pTable->Countable && count <= 0)
			{
				user->m_sItemArray[i].nNum = 0;
				user->m_sItemArray[i].sDuration = 0;
				user->m_sItemArray[i].sCount = 0;
				user->m_sItemArray[i].nSerialNum = 0;
			}
			else
			{
				if (count <= 0)
					user->m_sItemArray[i].sCount = 1;

				user->m_sItemArray[i].sCount = count;
			}

#ifdef _DEBUG
			TRACE(_T("%hs : %d slot (%d : %I64d)\n"), user->m_id, i, user->m_sItemArray[i].nNum, user->m_sItemArray[i].nSerialNum);
			LogFileWrite(std::format("{} : {} slot ({} : {})\r\n",
				user->m_id, i, user->m_sItemArray[i].nNum, user->m_sItemArray[i].nSerialNum));
#endif
		}
		else
		{
			user->m_sItemArray[i].nNum = 0;
			user->m_sItemArray[i].sDuration = 0;
			user->m_sItemArray[i].sCount = 0;
			user->m_sItemArray[i].nSerialNum = 0;
			user->m_sItemArray[i].byFlag = 0;
			user->m_sItemArray[i].sTimeRemaining = 0;

			if (itemId > 0)
			{
				LogFileWrite(std::format("Item Drop : charId={}, itemId={}\r\n", charId, itemId));
			}
		}
	}

	short sQuestTotal = 0;
	index = 0;
	for (int i = 0; i < MAX_QUEST; i++)
	{
		_USER_QUEST& quest = user->m_quests[i];
		quest.sQuestID = GetShort(strQuest, index);
		quest.byQuestState = GetByte(strQuest, index);

		if (quest.sQuestID > 100
			|| quest.byQuestState > 3)
		{
			memset(&quest, 0, sizeof(_USER_QUEST));
			continue;
		}

		if (quest.sQuestID > 0)
			++sQuestTotal;
	}

	if (QuestCount != sQuestTotal)
		user->m_sQuestCount = sQuestTotal;

	if (user->m_bLevel == 1
		&& user->m_iExp == 0
		&& user->m_iGold == 0)
	{
		int emptySlot = 0;
		for (int j = SLOT_MAX; j < HAVE_MAX + SLOT_MAX; j++)
		{
			if (user->m_sItemArray[j].nNum == 0)
			{
				emptySlot = j;
				break;
			}
		}

		if (emptySlot == HAVE_MAX + SLOT_MAX)
			return true;

		switch (user->m_sClass)
		{
			case 101:
				user->m_sItemArray[emptySlot].nNum = 120010000;
				user->m_sItemArray[emptySlot].sDuration = 5000;
				break;

			case 102:
				user->m_sItemArray[emptySlot].nNum = 110010000;
				user->m_sItemArray[emptySlot].sDuration = 4000;
				break;

			case 103:
				user->m_sItemArray[emptySlot].nNum = 180010000;
				user->m_sItemArray[emptySlot].sDuration = 5000;
				break;

			case 104:
				user->m_sItemArray[emptySlot].nNum = 190010000;
				user->m_sItemArray[emptySlot].sDuration = 10000;
				break;

			case 201:
				user->m_sItemArray[emptySlot].nNum = 120050000;
				user->m_sItemArray[emptySlot].sDuration = 5000;
				break;

			case 202:
				user->m_sItemArray[emptySlot].nNum = 110050000;
				user->m_sItemArray[emptySlot].sDuration = 4000;
				break;

			case 203:
				user->m_sItemArray[emptySlot].nNum = 180050000;
				user->m_sItemArray[emptySlot].sDuration = 5000;
				break;

			case 204:
				user->m_sItemArray[emptySlot].nNum = 190050000;
				user->m_sItemArray[emptySlot].sDuration = 10000;
				break;
			default:
				user->m_sItemArray[emptySlot].sCount = 1;
				user->m_sItemArray[emptySlot].nSerialNum = 0;
		}
	}

	return true;
}

/// \brief updates the database with the data from UserData[userId]
/// \param charId
/// \param userId
/// \param updateType one of UPDATE_PACKET_SAVE, UPDATE_LOGOUT, UPDATE_ALL_SAVE
/// \see UPDATE_PACKET_SAVE, UPDATE_LOGOUT, UPDATE_ALL_SAVE
/// \returns true when database successfully updated, false otherwise
bool CDBAgent::UpdateUser(const char* charId, int userId, int updateType)
{
	_USER_DATA* user = UserData[userId];
	if (user == nullptr)
		return false;

	if (_strnicmp(user->m_id, charId, MAX_ID_SIZE) != 0)
		return false;

	if (updateType == UPDATE_PACKET_SAVE)
		user->m_dwTime++;
	else if (updateType == UPDATE_LOGOUT
		|| updateType == UPDATE_ALL_SAVE)
		user->m_dwTime = 0;

	char skills[10] = {},
		items[400] = {},
		serial[400] = {},
		quests[400] = {};
	short questTotal = 0;

	int index = 0, serialIndex = 0;
	for (int i = 0; i < 9; i++)
		SetByte(skills, user->m_bstrSkill[i], index);

	index = 0;
	for (int i = 0; i < MAX_QUEST; i++)
	{
		_USER_QUEST& quest = user->m_quests[i];

		if (quest.sQuestID > 100
			|| quest.byQuestState > 3)
		{
			memset(&quest, 0, sizeof(_USER_QUEST));
		}
		else
		{
			if (quest.sQuestID > 0)
				++questTotal;
		}

		SetShort(quests, quest.sQuestID, index);
		SetByte(quests, quest.byQuestState, index);
	}

	if (questTotal != user->m_sQuestCount)
		user->m_sQuestCount = questTotal;

	index = 0;

	// Equip slots + inventory slots (14+28=42)
	for (int i = 0; i < HAVE_MAX + SLOT_MAX; i++)
	{
		if (user->m_sItemArray[i].nNum > 0)
		{
			if (_main->ItemArray.GetData(user->m_sItemArray[i].nNum) == nullptr)
				TRACE(_T("Item Drop Saved({}) : %d (%hs)\n"), i, user->m_sItemArray[i].nNum, user->m_id);
		}

		SetDWORD(items, user->m_sItemArray[i].nNum, index);
		SetShort(items, user->m_sItemArray[i].sDuration, index);
		SetShort(items, user->m_sItemArray[i].sCount, index);

		SetInt64(serial, user->m_sItemArray[i].nSerialNum, serialIndex);
	}
	
	try
	{
		DBProcessNumber(3);
		ReConnectODBC(_gameConn1.get());

		storedProc::UpdateUserData proc(_gameConn1->Conn);

		auto weak_result = proc.execute(
			user->m_id, user->m_bNation, user->m_bRace, user->m_sClass,
			user->m_bHairColor,user->m_bRank, user->m_bTitle, user->m_bLevel,
			user->m_iExp, user->m_iLoyalty, user->m_bFace, user->m_bCity,
			user->m_bKnights, user->m_bFame, user->m_sHp, user->m_sMp, user->m_sSp,
			user->m_bStr, user->m_bSta, user->m_bDex, user->m_bIntel, user->m_bCha,
			user->m_bAuthority, user->m_bPoints, user->m_iGold, user->m_bZone, user->m_sBind,
			static_cast<int>(user->m_curx * 100),
			static_cast<int>(user->m_curz * 100),
			static_cast<int>(user->m_cury * 100),
			user->m_dwTime, questTotal, skills, items, serial,
			quests, user->m_iMannerPoint, user->m_iLoyaltyMonthly);

		nanodbc::statement stmt;

		auto result = weak_result.lock();
		if (result == nullptr)
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] expected result set");
		}

		// affected_rows will be -1 if unavailable should be 1 if available
		if (result->affected_rows() == 0)
		{
			LogFileWrite(std::format("UpdateUser(): No rows affected for charId={}\r\n", charId));
			return false;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadUserData()");
		return false;
	}
	
	return true;
}

/// \brief attempts to login a character to the game server
/// \returns -1 for failure, 0 for unselected nation, 1 for karus, 2 for elmorad
int CDBAgent::AccountLogInReq(char* accountId, char* password)
{
	int16_t retCode = 0;
	try
	{
		DBProcessNumber(4);
		ReConnectODBC(_gameConn1.get());

		storedProc::AccountLogin proc(_gameConn1->Conn);
		proc.execute(accountId, password, &retCode);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.AccountLogInReq()");
		return false;
	}
	
	return retCode - 1;
}

/// \brief ensures that database records are created in ACCOUNT_CHAR
/// and WAREHOUSE prior to logging into the game
/// \returns true if records are properly set, false otherwise
bool CDBAgent::NationSelect(char* accountId, int nation)
{
	int16_t retCode = 0;
	try
	{
		DBProcessNumber(5);
		ReConnectODBC(_gameConn1.get());

		storedProc::NationSelect proc(_gameConn1->Conn);
		proc.execute(&retCode, accountId, nation);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.NationSelect()");
		return false;
	}

	// should only be 0 if bind failed, -2 for a DB error
	if (retCode != 1)
	{
		return false;
	}

	return true;
}

/// \brief attempts to create a new character
/// \returns NEW_CHAR_SUCCESS on success, or one of the following on error:
/// NEW_CHAR_ERROR, NEW_CHAR_NO_FREE_SLOT, NEW_CHAR_INVALID_RACE, NEW_CHAR_NAME_IN_USE, NEW_CHAR_SYNC_ERROR
int CDBAgent::CreateNewChar(char* accountId, int index, char* charId, int race, int Class, int hair, int face, int str, int sta, int dex, int intel, int cha)
{
	int16_t retCode = 0;
	try
	{
		DBProcessNumber(6);
		ReConnectODBC(_gameConn1.get());

		storedProc::CreateNewChar proc(_gameConn1->Conn);
		proc.execute(
			&retCode, accountId, index, charId, race, Class, hair,
			face, str, sta, dex, intel, cha);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.CreateNewChar()");
		return -1;
	}

	return retCode;
}

/// \brief loads character information needed in WIZ_ALLCHAR_INFO_REQ
/// \param[out] charId will trime whitespace
/// \param[out] buff buffer to write character info to
/// \param[out] buffIndex
/// /// \see AllCharInfoReq(), WIZ_ALLCHAR_INFO_REQ
bool CDBAgent::LoadCharInfo(char* charId_, char* buff, int& buffIndex)
{
	// trim charId
	std::string charId = charId_;
	rtrim(charId);

	// This attempts to request all 3 of the account's characters.
	// This includes characters that don't exist/aren't set.
	// These should be skipped.
	if (charId.empty())
		return false;

	uint8_t Race = 0, HairColor = 0, Level = 0, Face = 0, Zone = 0;
	int16_t Class = 0;
	char strItem[400] = {};
	
	int16_t rowCount = 0;
	try
	{
		DBProcessNumber(8);
		ReConnectODBC(_gameConn1.get());

		storedProc::LoadCharInfo proc(_gameConn1->Conn);

		auto weak_result = proc.execute(charId.c_str(), &rowCount);
		auto result = weak_result.lock();

		// Officially this requests all 3, but we pre-emptively skip the NULL/empty character names,
		// so at this point we're only requesting character names that we expect to exist.
		if (result == nullptr)
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] expected result set");
		}

		if (!result->next())
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] expected row in result set");
		}

		Race = static_cast<uint8_t>(result->get<int16_t>(0));
		Class = result->get<int16_t>(1);
		HairColor = static_cast<uint8_t>(result->get<int16_t>(2));
		Level = static_cast<uint8_t>(result->get<int16_t>(3));
		Face = static_cast<uint8_t>(result->get<int16_t>(4));
		Zone = static_cast<uint8_t>(result->get<int16_t>(5));

		if (!result->is_null(6))
		{
			std::vector<uint8_t> itemData(sizeof(strItem));
			result->get_ref(6, itemData);
			std::ranges::copy(itemData, strItem);
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadCharInfo()");
		return false;
	}

	SetString2(buff, charId.c_str(), static_cast<short>(charId.length()), buffIndex);
	SetByte(buff, Race, buffIndex);
	SetShort(buff, Class, buffIndex);
	SetByte(buff, Level, buffIndex);
	SetByte(buff, Face, buffIndex);
	SetByte(buff, HairColor, buffIndex);
	SetByte(buff, Zone, buffIndex);

	int tempId = 0, count = 0, index = 0, duration = 0;
	for (int i = 0; i < SLOT_MAX; i++)
	{
		tempId = GetDWORD(strItem, index);
		duration = GetShort(strItem, index);
		count = GetShort(strItem, index);

		if (i == HEAD
			|| i == BREAST
			|| i == SHOULDER
			|| i == LEG
			|| i == GLOVE
			|| i == FOOT
			|| i == LEFTHAND
			|| i == RIGHTHAND)
		{
			SetDWORD(buff, tempId, buffIndex);
			SetShort(buff, duration, buffIndex);
		}
	}

	return true;
}

/// \brief loads all character names for an account
/// \param accountId
/// \param[out] charId1
/// \param[out] charId2
/// \param[out] charId3
/// \returns true if charIds were successfully loaded, false otherwise
bool CDBAgent::GetAllCharID(const char* accountId, char* charId1_, char* charId2_, char* charId3_)
{
	std::string charId1, charId2, charId3;
	
	int32_t rowCount = 0;
	try
	{
		DBProcessNumber(9);
		ReConnectODBC(_gameConn1.get());

		storedProc::LoadAccountCharid proc(_gameConn1->Conn);

		auto weak_result = proc.execute(&rowCount, accountId);
		auto result = weak_result.lock();
		if (result == nullptr
			|| !result->next())
		{
			LogFileWrite(std::format("GetAllCharID(): No rows selected for accountId={}\r\n", accountId));
			return false;
		}

		if (!result->is_null(0))
			result->get_ref(0, charId1);

		if (!result->is_null(1))
			result->get_ref(1, charId2);

		if (!result->is_null(2))
			result->get_ref(2, charId3);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.GetAllCharID()");
		return false;
	}

	if (strcpy_s(charId1_, MAX_ID_SIZE + 1, charId1.c_str()))
	{
		LogFileWrite(std::format("GetAllCharID(): failed to write charId1(len: {}, val: {}) to charId1\r\n",
			charId1.length(), charId1));
		return false;
	}

	if (strcpy_s(charId2_, MAX_ID_SIZE + 1, charId2.c_str()))
	{
		LogFileWrite(std::format("GetAllCharID(): failed to write charId2(len: {}, val: {}) to charId2\r\n",
			charId2.length(), charId2));
		return false;
	}

	if (strcpy_s(charId3_, MAX_ID_SIZE + 1, charId3.c_str()))
	{
		LogFileWrite(std::format("GetAllCharID(): failed to write charId3(len: {}, val: {}) to charId3\r\n",
			charId3.length(), charId3));
		return false;
	}

	return true;
}

/// \brief attempts to create a knights clan
/// \returns 0 for success, 3 for name already in use, 6 for db error
int CDBAgent::CreateKnights(int knightsId, int nation, char* name, char* chief, int flag)
{
	int16_t retCode = 0;
	try
	{
		DBProcessNumber(10);
		ReConnectODBC(_gameConn1.get());

		storedProc::CreateKnights proc(_gameConn1->Conn);
		proc.execute(&retCode, knightsId, nation, flag, name, chief);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.CreateKnights()");
		retCode = 6;
	}

	if (retCode == 6)
	{
		LogFileWrite(std::format("CreateKnights(): database error creating knights (knightsId={}, nation={}, name={}, chief={}, flag={}\r\n",
			knightsId, nation, name, chief, flag));
	}

	return retCode;
}

/// \brief attempts to update a knights clan
/// \returns 0 on success, 2 for charId not found or db error, 7 for not found, 8 for capacity error
int CDBAgent::UpdateKnights(int type, char* charId, int knightsId, int domination)
{
	int16_t retCode = 0;
	try
	{
		DBProcessNumber(11);
		ReConnectODBC(_gameConn1.get());

		storedProc::UpdateKnights proc(_gameConn1->Conn);
		proc.execute(&retCode, type, charId, knightsId, domination);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.UpdateKnights()");
		return 2;
	}

	return retCode;
}

/// \brief attemps to delete a knights clan from the database
/// \return 0 for success, 7 if not found
int CDBAgent::DeleteKnights(int knightsId)
{
	int16_t retCode = 0;
	try
	{
		DBProcessNumber(12);
		ReConnectODBC(_gameConn1.get());

		storedProc::DeleteKnights proc(_gameConn1->Conn);
		proc.execute(&retCode, knightsId);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.DeleteKnights()");
		return 7;
	}

	return retCode;
	
}

/// \brief loads knight member information into buffOut
/// \param knightsId
/// \param start likely made for pagination, but unused by callers (always 0)
/// \param[out] buffOut buffer to write member information to
/// \param[out] buffIndex
/// \returns rowCount - start
int CDBAgent::LoadKnightsAllMembers(int knightsId, int start, char* buffOut, int& buffIndex)
{
	int				tempIndex = 0, userId = 0;

	uint8_t Fame, Level;
	int16_t	Class;
	int32_t rowCount = 0;
	try
	{
		DBProcessNumber(13);
		ReConnectODBC(_gameConn1.get());

		storedProc::LoadKnightsMembers proc(_gameConn1->Conn);
		auto weak_result = proc.execute(knightsId);
		auto result = weak_result.lock();
		if (result != nullptr)
		{
			while (result->next())
			{
				std::string charId;
				result->get_ref(0, charId);

				Fame = static_cast<uint8_t>(result->get<int16_t>(1));
				Level = static_cast<uint8_t>(result->get<int16_t>(2));
				Class = result->get<int16_t>(3);

				rtrim(charId);
				SetString2(buffOut, charId.c_str(), static_cast<short>(charId.length()), tempIndex);
				SetByte(buffOut, Fame, tempIndex);
				SetByte(buffOut, Level, tempIndex);
				SetShort(buffOut, Class, tempIndex);

				// check if the user is online
				userId = -1;
				_USER_DATA* pUser = _main->GetUserPtr(charId.c_str(), userId);
				if (pUser != nullptr)
					SetByte(buffOut, 1, tempIndex);
				else
					SetByte(buffOut, 0, tempIndex);

				// pagination:
				//if (count >= start + 10)
				//	break;
				rowCount++;
			}
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadKnightsAllMembers()");
		return 0;
	}

	if (rowCount == 0)
	{
		LogFileWrite(std::format("LoadKnightsAllMembers(): No rows selected for knightsId={}\r\n", knightsId));
	}

	// clamp result so that start doesn't send rowCount negative
	return std::max(rowCount - start, 0);
}

/// \brief updates concurrent zone count
/// \returns true if successful, false otherwise
bool CDBAgent::UpdateConCurrentUserCount(int serverId, int zoneId, int userCount)
{
	std::string updateQuery = std::format("UPDATE CONCURRENT SET [zone{}_count] = ? WHERE [serverid] = ?", zoneId);
	try
	{
		DBProcessNumber(14);
		ReConnectODBC(_accountConn2.get());

		nanodbc::statement stmt(*_accountConn2->Conn, updateQuery);
		stmt.bind(0, &userCount);
		stmt.bind(1, &serverId);
		stmt.execute();
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.UpdateConCurrentUserCount()");
		return false;
	}

	return true;
}

/// \brief attempts to load warehouse data for an account into UserData[userId]
/// \returns true if successful, otherwise false
bool CDBAgent::LoadWarehouseData(const char* accountId, int userId)
{
	char items[1600] = {}, serial[1600] = {};

	_USER_DATA* user = UserData[userId];
	if (user == nullptr
		|| strlen(user->m_id) == 0)
	{
		LogFileWrite(std::format("LoadWarehouseData(): called for inactive userId={}\r\n", userId));
		return false;
	}
	
	db::SqlBuilder<model::Warehouse> sql;
	// note: dwTime from prior query wasn't used, so not selecting it here.
	sql.SetSelectColumns({"nMoney", "WarehouseData", "strSerial"});
	sql.IsWherePK = true;
	try
	{
		DBProcessNumber(15);
		ReConnectODBC(_gameConn1.get());

		auto stmt = std::make_shared<nanodbc::statement>(*_gameConn1->Conn, sql.SelectString());
		stmt->bind(0, accountId);

		db::ModelRecordSet<model::Warehouse> recordSet;
		recordSet.open(stmt);
		if (!recordSet.next())
		{
			LogFileWrite(std::format("LoadWarehouseData(): No rows selected for accountId={}\r\n", accountId));
			return false;
		}

		model::Warehouse warehouse = recordSet.get();
		user->m_iBank = warehouse.Money;

		if (warehouse.ItemData.has_value())
			std::ranges::copy(warehouse.ItemData.value(), items);

		if (warehouse.Serial.has_value())
			std::ranges::copy(warehouse.Serial.value(), serial);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadWarehouseData()");
		return false;
	}

	int index = 0, serialIndex = 0;
	for (int i = 0; i < WAREHOUSE_MAX; i++)
	{
		int itemId = GetDWORD(items, index);
		short durability = GetShort(items, index);
		short count = GetShort(items, index);

		int64_t serialNumber = GetInt64(serial, serialIndex);

		model::Item* itemData = _main->ItemArray.GetData(itemId);
		if (itemData != nullptr)
		{
			user->m_sWarehouseArray[i].nNum = itemId;
			user->m_sWarehouseArray[i].sDuration = durability;

			if (count > ITEMCOUNT_MAX)
				user->m_sWarehouseArray[i].sCount = ITEMCOUNT_MAX;
			else if (count <= 0)
				count = 1;

			user->m_sWarehouseArray[i].sCount = count;
			user->m_sWarehouseArray[i].nSerialNum = serialNumber;
			TRACE(_T("%hs : %d ware slot (%d : %I64d)\n"), user->m_id, i, user->m_sWarehouseArray[i].nNum, user->m_sWarehouseArray[i].nSerialNum);
		}
		else
		{
			user->m_sWarehouseArray[i].nNum = 0;
			user->m_sWarehouseArray[i].sDuration = 0;
			user->m_sWarehouseArray[i].sCount = 0;

			if (itemId > 0)
			{
				LogFileWrite(std::format("LoadWarehouseData(): item dropped itemId={} accountId={}\r\n",
					itemId, accountId));
			}
		}
	}

	return true;
}

/// \brief attempts to update warehouse data from UserData[userId]
/// \param accountId
/// \param userId
/// \param updateType one of UPDATE_LOGOUT, UPDATE_ALL_SAVE, UPDATE_PACKET_SAVE
/// \returns true for success, otherwise false
bool CDBAgent::UpdateWarehouseData(const char* accountId, int userId, int updateType)
{
	_USER_DATA* pUser = UserData[userId];
	if (pUser == nullptr
		|| strlen(accountId) == 0)
	{
		LogFileWrite(std::format("UpdateWarehouseData(): called with inactive userId={} accountId={}\r\n",
					userId, accountId));
		return false;
	}

	if (_strnicmp(pUser->m_Accountid, accountId, MAX_ID_SIZE) != 0)
	{
		LogFileWrite(std::format("UpdateWarehouseData(): accountId mismatch user.accountId={} accountId={}\r\n",
					pUser->m_Accountid, accountId));
		return false;
	}

	if (updateType == UPDATE_LOGOUT
		|| updateType == UPDATE_ALL_SAVE)
		pUser->m_dwTime = 0;

	char items[1600] = {},
		serial[1600] = {};

	int index = 0, serialIndex = 0;
	for (int i = 0; i < WAREHOUSE_MAX; i++)
	{
		SetDWORD(items, pUser->m_sWarehouseArray[i].nNum, index);
		SetShort(items, pUser->m_sWarehouseArray[i].sDuration, index);
		SetShort(items, pUser->m_sWarehouseArray[i].sCount, index);
		SetInt64(serial, pUser->m_sWarehouseArray[i].nSerialNum, serialIndex);
	}

	try
	{
		DBProcessNumber(16);
		ReConnectODBC(_gameConn1.get());

		storedProc::UpdateWarehouse proc(_gameConn1->Conn);

		auto weak_result = proc.execute(
			accountId, pUser->m_iBank, pUser->m_dwTime,
			items, serial);

		auto result = weak_result.lock();
		if (result == nullptr)
		{
			throw nanodbc::database_error(nullptr, 0, "[application error] expected result set");
		}

		// affected_rows will be -1 if unavailable should be 1 if available
		if (result->affected_rows() == 0)
		{
			LogFileWrite(std::format("UpdateWarehouseData(): No rows affected for accountId={}\r\n", accountId));
			return false;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.UpdateWarehouseData()");
		return false;
	}

	return true;
}

/// \brief loads knights clan metadata into the supplied buffer
/// \param knightsId
/// \param[out] buffOut buffer to write knights data to
/// \param[out] buffIndex
/// \returns true if data was successfully loaded to the buffer, otherwise false
bool CDBAgent::LoadKnightsInfo(int knightsId, char* buffOut, int& buffIndex)
{
	db::SqlBuilder<model::Knights> sql;
	sql.SetSelectColumns({"IDNum", "Nation", "IDName", "Members", "Points"});
	sql.IsWherePK = true;
	try
	{
		DBProcessNumber(17);
		ReConnectODBC(_gameConn1.get());

		auto stmt = std::make_shared<nanodbc::statement>(*_gameConn1->Conn, sql.SelectString());
		stmt->bind(0, &knightsId);

		db::ModelRecordSet<model::Knights> recordSet;
		recordSet.open(stmt);
		if (!recordSet.next())
		{
			LogFileWrite(std::format("LoadKnightsInfo(): No rows selected for knightsId={}\r\n", knightsId));
			return false;
		}

		model::Knights knights = recordSet.get();
		if (knights.Name.length() > MAX_ID_SIZE)
		{
			LogFileWrite(std::format("LoadKnightsInfo(): knights.Name(len: {}, val: {}) exceeds length\r\n",
				knights.Name.length(), knights.Name));
			return false;
		}

		SetShort(buffOut, knights.ID, buffIndex);
		SetByte(buffOut, knights.Nation, buffIndex);
		SetString2(buffOut, knights.Name.c_str(), static_cast<short>(knights.Name.length()), buffIndex);
		SetShort(buffOut, knights.Members, buffIndex);
		SetDWORD(buffOut, knights.Points, buffIndex);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadKnightsInfo()");
		return false;
	}

	return true;
}

/// \brief sets CURRENTUSER record for a given accountId
/// \param accountId
/// \param charId
/// \param serverIp
/// \param serverId
/// \param clientIp
/// \param init 0x01 to insert, 0x02 to update CURRENTUSER
/// \returns true when CURRENTUSER successfully updated, otherwise false
bool CDBAgent::SetLogInInfo(const char* accountId, const char* charId, const char* serverIp, int serverId, const char* clientIp, BYTE init)
{
	std::string query;
	if (init == 0x01)
	{
		query = std::format("INSERT INTO [CURRENTUSER] ([strAccountID], [strCharID], [nServerNo], [strServerIP], [strClientIP]) VALUES (\'{}\',\'{}\',{},\'{}\',\'{}\')",
			accountId, charId, serverId, serverIp, clientIp);
	}
	else if (init == 0x02)
	{
		query = std::format("UPDATE [CURRENTUSER] SET [nServerNo]={}, [strServerIP]=\'{}\' WHERE [strAccountID] = \'{}\'",
			serverId, serverIp, accountId);
	}
	else
	{
		LogFileWrite(std::format("SetLogInInfo(): invalid init code specified (init: {}) for accountId={}\r\n",
			init, accountId));
		return false;
	}

	try
	{
		DBProcessNumber(18);
		ReConnectODBC(_accountConn1.get());

		nanodbc::statement stmt(*_accountConn1->Conn, query);
		nanodbc::result result = stmt.execute();
		// affected_rows will be -1 if unavailable should be 1 if available
		if (result.affected_rows() == 0)
		{
			LogFileWrite(std::format("SetLogInInfo(): No rows affected for accountId={}\r\n", accountId));
			return false;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.SetLogInInfo()");
		return false;
	}

	return true;
}

/// \brief removes the CURRENTUSER record for accountId
/// \returns true on success, otherwise false
bool CDBAgent::AccountLogout(const char* accountId, int logoutCode)
{
	// these always return as 1 currently, not really useful
	int16_t ret1 = 0, ret2 = 0;
	try
	{
		DBProcessNumber(19);
		ReConnectODBC(_accountConn1.get());

		storedProc::AccountLogout proc(_accountConn1->Conn);

		auto weak_result = proc.execute(accountId, logoutCode, &ret1, &ret2);

		auto result = weak_result.lock();
		if (result == nullptr)
		{
			throw nanodbc::database_error(nullptr, 0, "expected result set");
		}

		// affected_rows will be -1 if unavailable should be 1 if available
		if (result->affected_rows() == 0)
		{
			LogFileWrite(std::format("AccountLogout(): No rows affected for accountId={}\r\n", accountId));
			return false;
		}
		if (ret1 != 1)
		{
			LogFileWrite(std::format("AccountLogout(): ret1 not updated by proc for accountId={}\r\n", accountId));
			// rows are affected and CURRENTUSER is clean; the value just doesn't seem to be bound?
			//return false;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.AccountLogout()");
		return false;
	}

	return true;
}

/// \brief verifies that USERDATA or WAREHOUSE are in sync with the provided input
/// \param accountId
/// \param charId
/// \param checkType 0 for USERDATA, 1 for WAREHOUSE
/// \param compareData is WAREHOUSE.nMoney when checkType is 1, otherwise USERDATA.Exp
/// \returns true when input data matches database record, false otherwise
bool CDBAgent::CheckUserData(const char* accountId, const char* charId, int checkType, int userUpdateTime, int compareData)
{
	uint32_t dbData = 0, dbTime = 0;

	std::string query;
	if (checkType == 1)
	{
		query = std::format("SELECT [dwTime], [nMoney] FROM [WAREHOUSE] WHERE [strAccountID] = \'{}\'", accountId);
	}
	else
	{
		query = std::format("SELECT [dwTime], [Exp] FROM [USERDATA] WHERE [strUserID] = \'{}\'", charId);
	}
	
	try
	{
		DBProcessNumber(20);
		ReConnectODBC(_gameConn1.get());

		nanodbc::statement stmt(*_gameConn1->Conn, query);
		nanodbc::result result = stmt.execute();

		if (!result.next())
		{
			LogFileWrite(std::format("CheckUserData(): No rows affected for accountId={} charId={}\r\n", accountId, charId));
			return false;
		}

		dbTime = result.get<uint32_t>(0);
		dbData = result.get<uint32_t>(1);
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.CheckUserData()");
		return false;
	}

	if (dbTime != userUpdateTime
		|| dbData != compareData)
	{
		LogFileWrite(std::format("CheckUserData(): data mismatch dbTime(expected: {}, actual: {}) dbData(expected: {}, actual: {})\r\n",
			userUpdateTime, dbTime,
			compareData, dbData));
		return false;
	}

	return true;
}


/// \brief loads knights ranking data by nation to handle KNIGHTS_ALLLIST_REQ
/// \param nation 1 = karus, 2 = elmorad, 3 = battlezone
/// \see KNIGHTS_ALLLIST_REQ
/// \note most other functions in this file are db-ops only.  This does db-ops
/// and socket IO.  Should likely be separated into separate functions
void CDBAgent::LoadKnightsAllList(int nation)
{
	int32_t count = 0;
	int8_t retryCount = 0, maxRetry = 50;
	int32_t sendIndex = 0, dbIndex = 0, maxBatchSize = 40;
	char sendBuff[512] = {},
		dbBuff[512] = {};
	
	db::SqlBuilder<model::Knights> sql;
	sql.PostWhereClause = "ORDER BY [Points] DESC";
	if (nation == 3)	// battle zone
	{
		sql.Where = "[Points] <> 0";
	}
	else
	{
		sql.Where = std::format("[Nation] = {} AND [Points] <> 0", nation);
	}

	try
	{
		db::ModelRecordSet<model::Knights> recordSet;

		DBProcessNumber(21);
		ReConnectODBC(_gameConn1.get());

		recordSet.open(sql);
		while (recordSet.next())
		{
			count++;
			model::Knights knights = recordSet.get();
			SetShort(dbBuff, knights.ID, dbIndex);
			SetDWORD(dbBuff, knights.Points, dbIndex);
			SetByte(dbBuff, knights.Ranking, dbIndex);

			// publish batch
			if (count >= maxBatchSize)
			{
				SetByte(sendBuff, KNIGHTS_ALLLIST_REQ, sendIndex);
				SetShort(sendBuff, -1, sendIndex);
				SetByte(sendBuff, count, sendIndex);
				SetString(sendBuff, dbBuff, dbIndex, sendIndex);

				retryCount = 0;
				do
				{
					if (_main->LoggerSendQueue.PutData(sendBuff, sendIndex) == 1)
						break;

					retryCount++;
				}
				while (retryCount < maxRetry);

				if (retryCount >= maxRetry)
				{
					_main->OutputList.AddString(_T("Packet Drop: KNIGHTS_ALLLIST_REQ"));
					return;
				}

				memset(sendBuff, 0, sizeof(sendBuff));
				memset(dbBuff, 0, sizeof(dbBuff));
				sendIndex = dbIndex = 0;
				count = 0;
			}
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.LoadKnightsAllList()");
		return;
	}

	// send remaining results
	if (count < maxBatchSize)
	{
		SetByte(sendBuff, KNIGHTS_ALLLIST_REQ, sendIndex);
		SetShort(sendBuff, -1, sendIndex);
		SetByte(sendBuff, count, sendIndex);
		SetString(sendBuff, dbBuff, dbIndex, sendIndex);

		retryCount = 0;
		do
		{
			if (_main->LoggerSendQueue.PutData(sendBuff, sendIndex) == 1)
				break;

			retryCount++;
		}
		while (retryCount < maxRetry);

		if (retryCount >= maxRetry)
		{
			_main->OutputList.AddString(_T("Packet Drop: KNIGHTS_ALLLIST_REQ"));
		}
	}
}

/// \brief Updates the IDC_DB_PROCESS text with the DB Process Number
/// \note I don't actually see this on the UI, and I'm not sure how practical it is under load.
void CDBAgent::DBProcessNumber(int number)
{
	CString strDBNum;
	strDBNum.Format(_T(" %4d "), number);

	_main->GetDlgItem(IDC_DB_PROCESS)->SetWindowText(strDBNum);
	_main->GetDlgItem(IDC_DB_PROCESS)->UpdateWindow();
}

/// \brief updates which nation won the war and which charId killed the commander
/// \returns true if update successful, otherwise false
bool CDBAgent::UpdateBattleEvent(const char* charId, int nation)
{
	std::string query = "UPDATE BATTLE SET byNation = ?, strUserName = ? WHERE sIndex = 1";
	try
	{
		DBProcessNumber(22);
		ReConnectODBC(_accountConn1.get());

		nanodbc::statement stmt(*_accountConn1->Conn, query);
		stmt.bind(0, &nation);
		stmt.bind(1, charId);

		nanodbc::result result = stmt.execute();
		// affected_rows will be -1 if unavailable should be 1 if available
		if (result.affected_rows() == 0)
		{
			LogFileWrite("UpdateBattleEvent(): No rows affected");
			return false;
		}
	}
	catch (const nanodbc::database_error& dbErr)
	{
		LogDatabaseError(dbErr, "DBProcess.UpdateBattleEvent()");
		return false;
	}

	return true;
}

/* TODO: Proc does not exist
BOOL CDBAgent::CheckCouponEvent(const char* accountId)
{
	SQLHSTMT		hstmt = nullptr;
	SQLRETURN		retcode;
	BOOL			bData = TRUE, retval = FALSE;
	TCHAR			szSQL[1024] = {};
	SQLINTEGER		Indexind = SQL_NTS;
	SQLSMALLINT		sRet = 0;

	wsprintf(szSQL, TEXT("{call CHECK_COUPON_EVENT (\'%hs\', ?)}"), accountId);

	hstmt = nullptr;

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, accountConn1.m_hdbc, &hstmt);
	if (retcode != SQL_SUCCESS)
		return FALSE;

	retcode = SQLBindParameter(hstmt, 1, SQL_PARAM_OUTPUT, SQL_C_SSHORT, SQL_SMALLINT, 0, 0, &sRet, 0, &Indexind);
	if (retcode != SQL_SUCCESS)
	{
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
		return FALSE;
	}

	retcode = SQLExecDirect(hstmt, (SQLTCHAR*) szSQL, SQL_NTS);
	if (retcode == SQL_SUCCESS
		|| retcode == SQL_SUCCESS_WITH_INFO)
	{
		if (sRet == 0)
			retval = TRUE;
		else
			retval = FALSE;
	}
	else
	{
		if (DisplayErrorMsg(hstmt) == -1)
		{
			accountConn1.Close();
			if (!accountConn1.IsOpen())
			{
				ReConnectODBC(&accountConn1, _main->m_strAccountDSN, _main->m_strAccountUID, _main->m_strAccountPWD);
				return FALSE;
			}
		}

		retval = FALSE;
	}

	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);

	return retval;
}*/

/*  TODO:  Proc does not exist
BOOL CDBAgent::UpdateCouponEvent(const char* accountId, char* charId, char* cpid, int itemId, int count)
{
	SQLHSTMT		hstmt = nullptr;
	SQLRETURN		retcode;
	BOOL			bData = TRUE, retval = FALSE;
	TCHAR			szSQL[1024] = {};
	SQLINTEGER		Indexind = SQL_NTS;
	SQLSMALLINT		sRet = 0;

	wsprintf(szSQL, TEXT("{call UPDATE_COUPON_EVENT (\'%hs\', \'%hs\', \'%hs\', %d, %d)}"), accountId, charId, cpid, itemId, count);

	hstmt = nullptr;

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, accountConn1.m_hdbc, &hstmt);
	if (retcode != SQL_SUCCESS)
		return FALSE;

	retcode = SQLExecDirect(hstmt, (SQLTCHAR*) szSQL, SQL_NTS);
	if (retcode == SQL_SUCCESS
		|| retcode == SQL_SUCCESS_WITH_INFO)
	{
		retval = TRUE;
		//if( sRet == 1 )	retval = TRUE;
		//else retval = FALSE;
	}
	else
	{
		if (DisplayErrorMsg(hstmt) == -1)
		{
			accountConn1.Close();
			if (!accountConn1.IsOpen())
			{
				ReConnectODBC(&accountConn1, _main->m_strAccountDSN, _main->m_strAccountUID, _main->m_strAccountPWD);
				return FALSE;
			}
		}

		retval = FALSE;
	}

	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	return retval;
}
*/

/* TODO: Proc doesn't exist
BOOL CDBAgent::DeleteChar(int index, char* id, char* charId, char* socno)
{
	SQLHSTMT		hstmt = nullptr;
	SQLRETURN		retcode;
	TCHAR			szSQL[1024] = {};
	SQLSMALLINT		sParmRet;
	SQLINTEGER		cbParmRet = SQL_NTS;

	wsprintf(szSQL, TEXT("{ call DELETE_CHAR ( \'%hs\', %d, \'%hs\', \'%hs\', ? )}"), id, index, charId, socno);

	DBProcessNumber(7);

	retcode = SQLAllocHandle(SQL_HANDLE_STMT, gameConn1.m_hdbc, &hstmt);
	if (retcode == SQL_SUCCESS)
	{
		retcode = SQLBindParameter(hstmt, 1, SQL_PARAM_OUTPUT, SQL_C_SSHORT, SQL_INTEGER, 0, 0, &sParmRet, 0, &cbParmRet);
		if (retcode == SQL_SUCCESS)
		{
			retcode = SQLExecDirect(hstmt, (SQLTCHAR*) szSQL, SQL_NTS);
			if (retcode == SQL_ERROR)
			{
				if (DisplayErrorMsg(hstmt) == -1)
				{
					gameConn1.Close();

					if (!gameConn1.IsOpen())
					{
						ReConnectODBC(&gameConn1, _main->m_strGameDSN, _main->m_strGameUID, _main->m_strGamePWD);
						return FALSE;
					}
				}

				SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
				return FALSE;
			}

			SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
			return sParmRet;
		}
	}

	return FALSE;
}*/
