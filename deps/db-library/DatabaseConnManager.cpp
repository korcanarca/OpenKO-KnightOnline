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
		const std::string& databaseType,
		const std::string_view datasourceName,
		const std::string_view datasourceUserName,
		const std::string_view datasourcePassword)
	{
		GetInstance().SetDatasourceConfigImpl(
			databaseType,
			datasourceName,
			datasourceUserName,
			datasourcePassword);
	}

	/// \brief fetch the associated previously stored database config using the code-generated databaseType string
	std::shared_ptr<const DatabaseConnManager::DatasourceConfig>
		DatabaseConnManager::GetDatasourceConfig(const std::string& databaseType)
	{
		return GetInstance().GetDatasourceConfigImpl(databaseType);
	}

	/// \brief attempt a connection to the database using the code-generated databaseType string
	/// \throws std::runtime_error
	/// \throws nanodbc::database_error
	std::shared_ptr<nanodbc::connection> DatabaseConnManager::GetConnectionTo(const std::string& databaseType) noexcept(false)
	{
		return GetInstance().GetConnectionToImpl(databaseType);
	}

	void DatabaseConnManager::SetDatasourceConfigImpl(
		const std::string& databaseType,
		const std::string_view datasourceName,
		const std::string_view datasourceUserName,
		const std::string_view datasourcePassword)
	{
		std::lock_guard<std::mutex> lock(_configLock);

		auto itr = _configMap.find(databaseType);
		if (itr == _configMap.end())
		{
			auto config = std::make_shared<const DatasourceConfig>(
				datasourceName,
				datasourceUserName,
				datasourcePassword);

			_configMap.insert({ databaseType, config });
		}
		else
		{
			auto config = std::const_pointer_cast<DatasourceConfig>(itr->second);

			config->DatasourceName = datasourceName,
			config->DatasourceUsername = datasourceUserName,
			config->DatasourcePassword = datasourcePassword;
		}
	}

	/// \brief fetch the associated previously stored database config using the code-generated databaseType string
	std::shared_ptr<const DatabaseConnManager::DatasourceConfig>
	DatabaseConnManager::GetDatasourceConfigImpl(const std::string& databaseType) const
	{
		std::lock_guard<std::mutex> lock(_configLock);

		auto itr = _configMap.find(databaseType);
		if (itr == _configMap.end())
			return nullptr;

		return itr->second;
	}

	/// \brief attempt a connection to the database using the code-generated databaseType string
	/// \throws std::runtime_error
	/// \throws nanodbc::database_error
	std::shared_ptr<nanodbc::connection> DatabaseConnManager::GetConnectionToImpl(const std::string& databaseType) noexcept(false)
	{
		auto config = GetDatasourceConfig(databaseType);
		if (config == nullptr)
			throw DatasourceConfigNotFoundException("Invalid database type: " + databaseType);

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
