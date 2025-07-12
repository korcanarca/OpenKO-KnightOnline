#include "DatabaseConnManager.h"
#include "Exceptions.h"

#include <nanodbc/nanodbc.h>
#include <assert.h>

namespace db
{
	DatabaseConnManager* DatabaseConnManager::s_instance = nullptr;

	void DatabaseConnManager::Create()
	{
		if (s_instance == nullptr)
			s_instance = new DatabaseConnManager();
	}

	void DatabaseConnManager::Destroy()
	{
		delete s_instance;
		s_instance = nullptr;
	}

	DatabaseConnManager::DatabaseConnManager()
	{
		assert(s_instance == nullptr);
		s_instance = this;
	}

	void DatabaseConnManager::SetDatasourceConfig(
		modelUtil::DbType dbType,
		const std::string_view datasourceName,
		const std::string_view datasourceUserName,
		const std::string_view datasourcePassword)
	{
		GetInstance().SetDatasourceConfigImpl(
			dbType,
			datasourceName,
			datasourceUserName,
			datasourcePassword);
	}

	/// \brief fetch the associated previously stored database config using the code-generated dbType
	std::shared_ptr<const DatabaseConnManager::DatasourceConfig>
		DatabaseConnManager::GetDatasourceConfig(modelUtil::DbType dbType)
	{
		return GetInstance().GetDatasourceConfigImpl(dbType);
	}

	/// \brief attempt a connection to the database using the code-generated dbType
	/// \throws std::runtime_error
	/// \throws nanodbc::database_error
	std::shared_ptr<nanodbc::connection> DatabaseConnManager::GetConnectionTo(modelUtil::DbType dbType) noexcept(false)
	{
		return GetInstance().GetConnectionToImpl(dbType);
	}

	void DatabaseConnManager::SetDatasourceConfigImpl(
		modelUtil::DbType dbType,
		const std::string_view datasourceName,
		const std::string_view datasourceUserName,
		const std::string_view datasourcePassword)
	{
		std::lock_guard<std::mutex> lock(_configLock);

		auto itr = _configMap.find(dbType);
		if (itr == _configMap.end())
		{
			auto config = std::make_shared<const DatasourceConfig>(
				datasourceName,
				datasourceUserName,
				datasourcePassword);

			_configMap.insert({ dbType, config });
		}
		else
		{
			auto config = std::const_pointer_cast<DatasourceConfig>(itr->second);

			config->DatasourceName = datasourceName,
			config->DatasourceUsername = datasourceUserName,
			config->DatasourcePassword = datasourcePassword;
		}
	}

	/// \brief fetch the associated previously stored database config using the code-generated dbType
	std::shared_ptr<const DatabaseConnManager::DatasourceConfig>
	DatabaseConnManager::GetDatasourceConfigImpl(modelUtil::DbType dbType) const
	{
		std::lock_guard<std::mutex> lock(_configLock);

		auto itr = _configMap.find(dbType);
		if (itr == _configMap.end())
			return nullptr;

		return itr->second;
	}

	/// \brief attempt a connection to the database using the code-generated dbType
	/// \throws std::runtime_error
	/// \throws nanodbc::database_error
	std::shared_ptr<nanodbc::connection> DatabaseConnManager::GetConnectionToImpl(modelUtil::DbType dbType) noexcept(false)
	{
		auto config = GetDatasourceConfig(dbType);
		if (config == nullptr)
			throw DatasourceConfigNotFoundException("Invalid database type: " + modelUtil::DbTypeString(dbType));

		return std::make_shared<nanodbc::connection>(
			config->DatasourceName,
			config->DatasourceUsername,
			config->DatasourcePassword);
	}

	DatabaseConnManager::~DatabaseConnManager()
	{
		assert(s_instance != nullptr);
		s_instance = nullptr;
	}
}
