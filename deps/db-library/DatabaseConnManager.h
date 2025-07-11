#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

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
		DatabaseConnManager();

		/// \brief fetch the associated previously stored database config using the code-generated databaseType string
		void SetDatasourceConfig(const std::string& databaseType, const std::string_view datasourceName, const std::string_view datasourceUserName, const std::string_view datasourcePassword);

		/// \brief fetch the associated previously stored database config using the code-generated databaseType string
		std::shared_ptr<const DatasourceConfig> GetDatasourceConfig(const std::string& databaseType) const;

		/// \brief attempt a connection to the database using the code-generated databaseType string
		/// \throws DatasourceConfigNotFoundException
		/// \throws nanodbc::database_error
		std::shared_ptr<nanodbc::connection> GetConnectionTo(const std::string& databaseType) noexcept(false);

		~DatabaseConnManager();

	protected:
		std::unordered_map<std::string, std::shared_ptr<const DatasourceConfig>> _configMap;
		mutable std::mutex _configLock;
	};

}
