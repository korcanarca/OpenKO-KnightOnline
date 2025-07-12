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

	/// \brief manages connections to the database via nanodbc
	class DatabaseConnManager
	{
	protected:
		struct DatasourceConfig
		{
			std::string DatasourceName;
			std::string DatasourceUsername;
			std::string DatasourcePassword;

			// NOTE: This is only used internally, so no reason not to forward along the views here.
			DatasourceConfig(
				const std::string_view& name,
				const std::string_view& username,
				const std::string_view& password)
				: DatasourceName(name), DatasourceUsername(username), DatasourcePassword(password)
			{
			}
		};

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
		static std::shared_ptr<nanodbc::connection> GetConnectionTo(modelUtil::DbType dbType) noexcept(false);

	protected:
		static inline DatabaseConnManager& GetInstance()
		{
			assert(s_instance != nullptr);
			return *s_instance;
		}

		DatabaseConnManager();

		/// \brief fetch the associated previously stored database config using the code-generated dbType
		void SetDatasourceConfigImpl(modelUtil::DbType dbType, const std::string_view datasourceName, const std::string_view datasourceUserName, const std::string_view datasourcePassword);

		/// \brief fetch the associated previously stored database config using the code-generated dbType
		std::shared_ptr<const DatasourceConfig> GetDatasourceConfigImpl(modelUtil::DbType dbType) const;

		/// \brief attempt a connection to the database using the code-generated dbType
		/// \throws DatasourceConfigNotFoundException
		/// \throws nanodbc::database_error
		std::shared_ptr<nanodbc::connection> GetConnectionToImpl(modelUtil::DbType dbType) noexcept(false);

		~DatabaseConnManager();

	protected:
		static DatabaseConnManager* s_instance;

		std::unordered_map<modelUtil::DbType, std::shared_ptr<const DatasourceConfig>> _configMap;
		mutable std::mutex _configLock;
	};

}
