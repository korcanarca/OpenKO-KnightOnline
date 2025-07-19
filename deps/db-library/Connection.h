#pragma once

#include <memory>

namespace nanodbc
{
	class connection;
}

namespace db
{
	struct DatasourceConfig;

	class Connection
	{
	public:
		std::shared_ptr<nanodbc::connection> Conn;
		std::shared_ptr<const DatasourceConfig> Config;
		long Timeout;

		Connection(
			const std::shared_ptr<nanodbc::connection>& conn,
			const std::shared_ptr<const DatasourceConfig>& config,
			long timeout = 0);

		/// \brief Checks to see if the connection is disconnected, and attempts
		/// to reconnect if appropriate.
		/// \returns -1 for failed connection, 0 for no-op, 1 for successful reconnect
		int8_t Reconnect();
	};
}
