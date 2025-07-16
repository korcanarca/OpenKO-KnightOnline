#include "ConnectionManager.h"
#include "Exceptions.h"

#include <nanodbc/nanodbc.h>
#include <assert.h>

namespace db
{
	ConnectionManager* ConnectionManager::s_instance = nullptr;

	void ConnectionManager::Create()
	{
		if (s_instance == nullptr)
			s_instance = new ConnectionManager();
	}

	void ConnectionManager::Destroy()
	{
		delete s_instance;
		s_instance = nullptr;
	}

	ConnectionManager::ConnectionManager()
	{
		assert(s_instance == nullptr);
		s_instance = this;
	}

	void ConnectionManager::SetDatasourceConfig(
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
	std::shared_ptr<const ConnectionManager::DatasourceConfig>
		ConnectionManager::GetDatasourceConfig(modelUtil::DbType dbType)
	{
		return GetInstance().GetDatasourceConfigImpl(dbType);
	}

	/// \brief attempt a connection to the database using the code-generated dbType
	/// \throws std::runtime_error
	/// \throws nanodbc::database_error
	std::shared_ptr<ConnectionManager::Connection> ConnectionManager::GetConnectionTo(modelUtil::DbType dbType, long timeout) noexcept(false)
	{
		return GetInstance().GetConnectionToImpl(dbType, timeout);
	}

	/// \brief returns the current ODBC connection string for a given DbType, or empty string if there is none
	std::string ConnectionManager::OdbcConnString(modelUtil::DbType dbType)
	{
		std::string out;
		std::shared_ptr<const DatasourceConfig> config = GetDatasourceConfig(dbType);
		if (config != nullptr)
		{
			out = "ODBC;DSN=" + config->DatasourceName + ";UID=" + config->DatasourceUsername + ";PWD=" + config->DatasourcePassword;
		}
		
		return out;
	}

	void ConnectionManager::SetDatasourceConfigImpl(
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
	std::shared_ptr<const ConnectionManager::DatasourceConfig>
	ConnectionManager::GetDatasourceConfigImpl(modelUtil::DbType dbType) const
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
	std::shared_ptr<ConnectionManager::Connection> ConnectionManager::GetConnectionToImpl(modelUtil::DbType dbType, long timeout) noexcept(false)
	{
		std::shared_ptr<const DatasourceConfig> config = GetDatasourceConfig(dbType);
		if (config == nullptr)
			throw DatasourceConfigNotFoundException("Invalid database type: " + modelUtil::DbTypeString(dbType));

		auto nanoconn = std::make_shared<nanodbc::connection>(
				config->DatasourceName,
				config->DatasourceUsername,
				config->DatasourcePassword,
				timeout);
		
		return std::make_shared<Connection>(nanoconn, config, timeout);
	}

	ConnectionManager::~ConnectionManager()
	{
		assert(s_instance != nullptr);
		s_instance = nullptr;
	}

	bool ConnectionManager::Connection::Reconnect() noexcept(false)
	{
		if (Conn == nullptr)
		{
			return false;
		}

		// only calling connect if we're currently disconnected/timed out
		// calling connect while connected would likely refresh a timeout, however
		// it also deallocs/allocs resources and may cause unknown side effects
		if (!Conn->connected())
		{
			// this can throw an exception
			Conn->connect(Config->DatasourceName, Config->DatasourceUsername, Config->DatasourcePassword, Timeout);
		}

		return true;
	}
}
