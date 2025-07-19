#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <assert.h>

import ModelUtil;

namespace nanodbc
{
	class connection;
}

namespace db
{

	class Connection;
	struct DatasourceConfig;

	/// \brief manages connections to the database via nanodbc
	class ConnectionManager
	{
	public:
		static void Create();
		static void Destroy();

		/// \brief fetch the associated previously stored database config using the code-generated dbType
		static void SetDatasourceConfig(modelUtil::DbType dbType, const std::string_view datasourceName, const std::string_view datasourceUserName, const std::string_view datasourcePassword);

		/// \brief fetch the associated previously stored database config using the code-generated dbType
		static std::shared_ptr<const DatasourceConfig> GetDatasourceConfig(modelUtil::DbType dbType);

		/// \brief attempt a connection to the database using the code-generated dbType
		/// \throws DatasourceConfigNotFoundException
		/// \throws nanodbc::database_error
		static std::shared_ptr<Connection> GetConnectionTo(modelUtil::DbType dbType, long timeout = 0) noexcept(false);

		/// \brief returns the current ODBC connection string for a given DbType, or empty string if there is none
		static std::string OdbcConnString(modelUtil::DbType dbType);

	protected:
		static inline ConnectionManager& GetInstance()
		{
			assert(s_instance != nullptr);
			return *s_instance;
		}

		ConnectionManager();

		/// \brief fetch the associated previously stored database config using the code-generated dbType
		void SetDatasourceConfigImpl(modelUtil::DbType dbType, const std::string_view datasourceName, const std::string_view datasourceUserName, const std::string_view datasourcePassword);

		/// \brief fetch the associated previously stored database config using the code-generated dbType
		std::shared_ptr<const DatasourceConfig> GetDatasourceConfigImpl(modelUtil::DbType dbType) const;

		/// \brief attempt a connection to the database using the code-generated dbType
		/// \throws DatasourceConfigNotFoundException
		/// \throws nanodbc::database_error
		std::shared_ptr<Connection> GetConnectionToImpl(modelUtil::DbType dbType, long timeout = 0) noexcept(false);

		~ConnectionManager();

	protected:
		static ConnectionManager* s_instance;

		std::unordered_map<modelUtil::DbType, std::shared_ptr<const DatasourceConfig>> _configMap;
		mutable std::mutex _configLock;
	};

}
