#include "pch.h"
#include "Connection.h"
#include "DatasourceConfig.h"

namespace db
{

	Connection::Connection(
		const std::shared_ptr<nanodbc::connection>& conn,
		const std::shared_ptr<const DatasourceConfig>& config,
		long timeout)
		: Conn(conn), Config(config), Timeout(timeout)
	{
	}

	/// \brief Checks to see if the connection is disconnected, and attempts
	/// to reconnect if appropriate.
	/// \returns -1 for failed connection, 0 for no-op, 1 for successful reconnect
	int8_t Connection::Reconnect() noexcept(false)
	{
		if (Conn == nullptr)
		{
			return -1;
		}

		// only calling connect if we're currently disconnected/timed out
		// calling connect while connected would likely refresh a timeout, however
		// it also deallocs/allocs resources and may cause unknown side effects
		if (!Conn->connected())
		{
			// this can throw an exception
			Conn->connect(Config->DatasourceName, Config->DatasourceUsername, Config->DatasourcePassword, Timeout);
			return 1;
		}

		return 0;
	}
}
