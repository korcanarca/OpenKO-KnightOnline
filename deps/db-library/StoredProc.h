﻿#pragma once

#include <memory>

#include "ConnectionManager.h"
#include "PoolConnection.h"
#include "hooks.h"

namespace db
{

	template <typename ProcType>
	class StoredProc : public ProcType
	{
	public:
		StoredProc()
		{
			auto poolConn = ConnectionManager::CreatePoolConnection(ProcType::DbType());
			if (poolConn == nullptr)
				return;

			this->_conn = *poolConn;
			this->_poolConn = poolConn;

			static_cast<Connection*>(*poolConn)->ReconnectIfDisconnected();
		}

	protected:
		std::shared_ptr<PoolConnection> _poolConn;
	};

}
